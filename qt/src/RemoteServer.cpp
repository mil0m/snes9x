#include "RemoteServer.hpp"

#include <QDateTime>
#include <QFile>
#include <QHttpHeaders>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QUrl>

using namespace corrupt;

namespace
{

constexpr qint64 kHelloWithinMs = 10000; // a socket that doesn't say hello by then is closed

QHttpServerResponse page_file(const char *path, const char *mime)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
    return QHttpServerResponse(mime, f.readAll());
}

// "https://host:port" -> "host:port", lower case; the Host header's form.
QByteArray origin_host(QByteArrayView origin)
{
    QUrl url(QString::fromLatin1(origin.toByteArray()));
    if (!url.isValid() || url.host().isEmpty())
        return {};
    QByteArray host = url.host().toLower().toLatin1();
    if (url.port() != -1)
        host += ":" + QByteArray::number(url.port());
    return host;
}

} // namespace

RemoteServer::RemoteServer(remote::Control &control, Source source, QObject *parent)
    : QObject(parent), control(control), source(std::move(source))
{
    connect(&status_timer, &QTimer::timeout, this, &RemoteServer::pushStatus);
    connect(&memory_timer, &QTimer::timeout, this, &RemoteServer::pushMemory);
    setIntervals(250, 200);
}

RemoteServer::~RemoteServer()
{
    stop();
}

void RemoteServer::setIntervals(int status_ms, int memory_ms)
{
    status_timer.setInterval(status_ms);
    memory_timer.setInterval(memory_ms);
}

bool RemoteServer::start(quint16 port, const std::string &token_)
{
    stop();
    token = token_;
    http = std::make_unique<QHttpServer>();
    // The page and nothing else: a fixed map, no file system.
    http->route("/", QHttpServerRequest::Method::Get,
                [] { return page_file(":/remote/index.html", "text/html; charset=utf-8"); });
    http->route("/remote.js", QHttpServerRequest::Method::Get,
                [] { return page_file(":/remote/remote.js", "text/javascript; charset=utf-8"); });
    http->route("/remote.css", QHttpServerRequest::Method::Get,
                [] { return page_file(":/remote/remote.css", "text/css; charset=utf-8"); });
    http->setMissingHandler(this, [](const QHttpServerRequest &, QHttpServerResponder &responder) {
        responder.write(QHttpServerResponder::StatusCode::NotFound);
    });
    http->addAfterRequestHandler(this, [](const QHttpServerRequest &, QHttpServerResponse &response) {
        QHttpHeaders h = response.headers();
        h.replaceOrAppend("Content-Security-Policy",
                          "default-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'");
        h.replaceOrAppend("X-Frame-Options", "DENY");
        h.replaceOrAppend("Referrer-Policy", "no-referrer");
        h.replaceOrAppend("X-Content-Type-Options", "nosniff");
        h.replaceOrAppend("Cache-Control", "no-store");
        response.setHeaders(std::move(h));
    });
    // The WebSocket: only at /ws, and only from a page served by this same host (so another
    // site open in a browser on this computer can't drive the app through 127.0.0.1).
    http->addWebSocketUpgradeVerifier(this, [](const QHttpServerRequest &request) {
        if (request.url().path() != "/ws")
            return QHttpServerWebSocketUpgradeResponse::deny();
        const auto &h = request.headers();
        QByteArray origin = origin_host(h.value("Origin"));
        QByteArray host = h.value("X-Forwarded-Host").toByteArray().toLower();
        if (host.isEmpty())
            host = h.value("Host").toByteArray().toLower();
        if (origin.isEmpty() || origin != host)
            return QHttpServerWebSocketUpgradeResponse::deny();
        return QHttpServerWebSocketUpgradeResponse::accept();
    });
    connect(http.get(), &QAbstractHttpServer::newWebSocketConnection, this, &RemoteServer::accept);

    // The HTTP server owns the listener once bound (and it dies with it).
    auto *listener = new QTcpServer(http.get());
    if (!listener->listen(QHostAddress::LocalHost, port) || !http->bind(listener))
    {
        stop();
        return false;
    }
    tcp = listener;
    status_timer.start();
    memory_timer.start();
    return true;
}

void RemoteServer::stop()
{
    status_timer.stop();
    memory_timer.stop();
    bool had = !clients_.empty();
    for (auto &c : clients_)
        if (c->socket)
        {
            c->socket->disconnect(this);
            c->socket->close();
            c->socket->deleteLater();
        }
    clients_.clear();
    if (tcp)
        tcp->close();
    tcp = nullptr;
    http.reset();
    if (had)
        emit clientsChanged();
}

quint16 RemoteServer::port() const
{
    return tcp ? tcp->serverPort() : 0;
}

QHostAddress RemoteServer::address() const
{
    return tcp ? tcp->serverAddress() : QHostAddress();
}

QStringList RemoteServer::clients() const
{
    QStringList out;
    for (auto &c : clients_)
        if (c->conn->authorised())
            out << c->who;
    return out;
}

void RemoteServer::accept()
{
    while (http && http->hasPendingWebSocketConnections())
    {
        auto socket = http->nextPendingWebSocketConnection();
        if (!socket)
            continue;
        auto c = std::make_unique<Client>();
        c->socket = socket.release();
        c->socket->setParent(this);
        c->conn = std::make_unique<remote::Connection>(control, token, gate);
        // The access gate in front says who logged in; without one, it's this computer.
        QByteArray email = c->socket->request().rawHeader("Cf-Access-Authenticated-User-Email");
        c->who = email.isEmpty() ? tr("this computer") : QString::fromUtf8(email);
        c->opened_ms = QDateTime::currentMSecsSinceEpoch();
        Client *raw = c.get();
        connect(c->socket, &QWebSocket::textMessageReceived, this,
                [this, raw](const QString &text) { received(raw, text); });
        connect(c->socket, &QWebSocket::binaryMessageReceived, this, [this, raw] { drop(raw); });
        connect(c->socket, &QWebSocket::disconnected, this, [this, raw] { drop(raw); });
        clients_.push_back(std::move(c));
    }
}

void RemoteServer::received(Client *c, const QString &text)
{
    if (text.size() > 256 * 1024)
    {
        drop(c);
        return;
    }
    bool was = c->conn->authorised();
    auto reply = c->conn->handle(text.toStdString(), remote::Clock::now());
    if (!reply.text.empty() && c->socket)
        c->socket->sendTextMessage(QString::fromStdString(reply.text));
    if (reply.close)
    {
        if (c->socket)
            c->socket->flush();
        drop(c);
        return;
    }
    if (reply.welcomed && !was)
    {
        emit clientsChanged();
        c->last_status.clear();
        pushStatus();
    }
    if (!reply.did.empty())
        emit didSomething(c->who, QString::fromStdString(reply.did));
    if (reply.watch_changed)
        pushMemory();
}

void RemoteServer::drop(Client *c)
{
    for (auto it = clients_.begin(); it != clients_.end(); ++it)
        if (it->get() == c)
        {
            bool counted = c->conn->authorised();
            if (c->socket)
            {
                c->socket->disconnect(this);
                c->socket->close();
                c->socket->deleteLater();
            }
            clients_.erase(it);
            if (counted)
                emit clientsChanged();
            return;
        }
}

void RemoteServer::pushStatus()
{
    // Sockets that never said hello don't linger.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::vector<Client *> late;
    for (auto &c : clients_)
        if (!c->conn->authorised() && now - c->opened_ms > kHelloWithinMs)
            late.push_back(c.get());
    for (auto *c : late)
        drop(c);

    if (!source.status)
        return;
    bool any = false;
    for (auto &c : clients_)
        any = any || c->conn->authorised();
    if (!any)
        return;
    auto view = source.status();
    for (auto &who : clients())
        view.clients.push_back(who.toStdString());
    std::string text = remote::status_json(view);
    for (auto &c : clients_)
        if (c->conn->authorised() && c->socket && c->last_status != text)
        {
            c->socket->sendTextMessage(QString::fromStdString(text));
            c->last_status = text;
        }
}

void RemoteServer::pushMemory()
{
    if (!source.read)
        return;
    for (auto &c : clients_)
    {
        auto &w = c->conn->watch();
        if (!c->conn->authorised() || !w || !c->socket)
            continue;
        auto bytes = source.read(w->target, w->offset, w->length);
        if (bytes.empty())
            continue;
        auto message = remote::memory_message(w->target, w->offset, bytes);
        c->socket->sendBinaryMessage(QByteArray(reinterpret_cast<const char *>(message.data()), qsizetype(message.size())));
    }
}
