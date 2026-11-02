#include "doctest/doctest.h"
#include "RemoteServer.hpp"
#include "nlohmann/json.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QWebSocket>

using namespace corrupt;
using json = nlohmann::json;

namespace
{

constexpr const char *kToken = "test-token-123";

template <class F> bool wait_for(F done, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct FakeControl : remote::Control
{
    std::vector<std::string> calls;
    std::string set_active(bool on) override
    {
        calls.push_back(on ? "on" : "off");
        return {};
    }
    void back_to_normal() override { calls.push_back("back"); }
    void reroll() override { calls.push_back("reroll"); }
    void mark_crash() override { calls.push_back("crash"); }
    void set_chaos(double) override { calls.push_back("chaos"); }
    void set_rule_enabled(size_t, bool) override { calls.push_back("rule"); }
    void set_rule_strength(size_t, double) override { calls.push_back("strength"); }
    void press_hotkey(int n) override { calls.push_back("hotkey " + std::to_string(n)); }
    std::string paint(Target, const std::vector<uint32_t> &, uint8_t) override
    {
        calls.push_back("paint");
        return {};
    }
    std::vector<std::string> starter_names = { "Gentle noise", "Code drip" };
    std::string starter_error;
    size_t starter_count() override { return starter_names.size(); }
    std::string starter_name(size_t i) override { return starter_names.at(i); }
    std::string load_starter(size_t i) override
    {
        calls.push_back("starter " + std::to_string(i));
        return starter_error;
    }
    size_t rule_count() override { return 1; }
    size_t memory_size(Target t) override { return t == Target::WRAM ? 0x20000 : 0; }
};

struct Fixture
{
    FakeControl control;
    bool active = false;
    RemoteServer server{ control,
                         { [this] {
                              remote::StatusView s;
                              s.game_loaded = true;
                              s.game = "TEST";
                              s.active = active;
                              return s;
                          },
                           [](Target, uint32_t offset, uint32_t length) {
                               std::vector<uint8_t> b(length);
                               for (uint32_t i = 0; i < length; i++)
                                   b[i] = uint8_t(offset + i);
                               return b;
                           } } };
    QNetworkAccessManager net;

    Fixture()
    {
        server.setIntervals(20, 20);
        REQUIRE(server.start(0, kToken));
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(server.port()); }

    struct Got
    {
        int status = 0;
        QByteArray body;
        QNetworkReply *reply = nullptr;
    };
    Got get(const QString &path)
    {
        QNetworkReply *r = net.get(QNetworkRequest(QUrl(base() + path)));
        REQUIRE(wait_for([&] { return r->isFinished(); }));
        Got g{ r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), r->readAll(), r };
        return g;
    }
};

// A browser-like client: Origin set, messages collected.
struct Client
{
    QWebSocket ws;
    std::vector<json> texts;
    std::vector<QByteArray> binaries;
    bool closed = false;

    explicit Client(const QString &origin, const QByteArray &email = {}) : ws(origin)
    {
        QObject::connect(&ws, &QWebSocket::textMessageReceived,
                         [this](const QString &t) { texts.push_back(json::parse(t.toStdString())); });
        QObject::connect(&ws, &QWebSocket::binaryMessageReceived, [this](const QByteArray &b) { binaries.push_back(b); });
        QObject::connect(&ws, &QWebSocket::disconnected, [this] { closed = true; });
        this->email = email;
    }
    QByteArray email;
    bool open(const QString &url)
    {
        QNetworkRequest req{ QUrl(url) };
        if (!email.isEmpty())
            req.setRawHeader("Cf-Access-Authenticated-User-Email", email);
        ws.open(req);
        return wait_for([&] { return ws.state() == QAbstractSocket::ConnectedState || closed; });
    }
    void send(const json &j) { ws.sendTextMessage(QString::fromStdString(j.dump())); }
    bool has(const char *key)
    {
        for (auto &t : texts)
            if (t.contains(key))
                return true;
        return false;
    }
    json last(const char *key)
    {
        for (auto it = texts.rbegin(); it != texts.rend(); ++it)
            if (it->contains(key))
                return *it;
        return {};
    }
};

} // namespace

TEST_SUITE("remote server")
{
    TEST_CASE("it listens on this computer only")
    {
        Fixture f;
        CHECK(f.server.running());
        CHECK(f.server.port() != 0);
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, f.server.port());
        CHECK(probe.waitForConnected(2000));
        // Nothing listens on the other addresses: the server never binds to them.
        CHECK(f.server.address() == QHostAddress(QHostAddress::LocalHost));
    }

    TEST_CASE("it serves the page with strict headers, and nothing else")
    {
        Fixture f;
        auto page = f.get("/");
        CHECK(page.status == 200);
        CHECK(page.body.contains("<title>"));
        CHECK(page.reply->rawHeader("Content-Security-Policy").contains("default-src 'self'"));
        CHECK(page.reply->rawHeader("X-Frame-Options") == "DENY");
        CHECK(page.reply->rawHeader("Referrer-Policy") == "no-referrer");
        CHECK(f.get("/remote.js").status == 200);
        CHECK(f.get("/remote.css").status == 200);
        CHECK(f.get("/etc/passwd").status == 404);
        CHECK(f.get("/../CMakeLists.txt").status == 404);
        CHECK(f.get("/ws").status == 404); // plain HTTP: no
        CHECK(f.get("/remote/index.html").status == 404);
    }

    TEST_CASE("the right token gets the welcome and the status; actions reach the session")
    {
        Fixture f;
        Client c(f.base(), "erik@example.com");
        REQUIRE(c.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port())));
        QString who, what;
        QObject::connect(&f.server, &RemoteServer::didSomething, [&](const QString &w, const QString &d) {
            who = w;
            what = d;
        });
        c.send({ { "hello", 1 }, { "token", kToken } });
        REQUIRE(wait_for([&] { return c.has("welcome") && c.has("status"); }));
        CHECK(c.last("status")["status"]["game"] == "TEST");
        CHECK(f.server.clients() == QStringList{ "erik@example.com" });
        CHECK(c.last("status")["status"]["clients"][0] == "erik@example.com");

        c.send({ { "do", "hotkey" }, { "key", 2 }, { "id", 5 } });
        REQUIRE(wait_for([&] { return c.has("done"); }));
        CHECK(f.control.calls == std::vector<std::string>{ "hotkey 2" });
        CHECK(who == "erik@example.com");
        CHECK(what == "pressed button 2");

        // A change in the session reaches the page by itself.
        f.active = true;
        REQUIRE(wait_for([&] { return c.last("status")["status"]["active"] == true; }));
    }

    TEST_CASE("a watched memory window arrives as binary")
    {
        Fixture f;
        Client c(f.base());
        REQUIRE(c.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port())));
        c.send({ { "hello", 1 }, { "token", kToken } });
        REQUIRE(wait_for([&] { return c.has("welcome"); }));
        CHECK(f.server.clients() == QStringList{ "this computer" });
        c.send({ { "do", "watch" }, { "memory", "wram" }, { "offset", 16 }, { "length", 32 }, { "id", 1 } });
        REQUIRE(wait_for([&] { return !c.binaries.empty(); }));
        auto &m = c.binaries.front();
        REQUIRE(m.size() == 4 + 1 + 4 + 32);
        CHECK(m.left(4) == "S9XM");
        CHECK(uint8_t(m[4]) == 0);  // WRAM
        CHECK(uint8_t(m[5]) == 16); // offset
        CHECK(uint8_t(m[9]) == 16); // the first byte
    }

    TEST_CASE("a wrong token is closed; so is a socket that never says hello in time, or sends binary")
    {
        Fixture f;
        Client c(f.base());
        REQUIRE(c.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port())));
        c.send({ { "hello", 1 }, { "token", "nope" } });
        REQUIRE(wait_for([&] { return c.closed; }));
        CHECK(c.last("error")["error"] == "wrong token");
        CHECK(f.server.clients().isEmpty());

        Client b(f.base());
        REQUIRE(b.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port())));
        b.ws.sendBinaryMessage("hello");
        CHECK(wait_for([&] { return b.closed; }));
        CHECK(f.control.calls.empty());
    }

    TEST_CASE("another site's page can't connect: the Origin must be this host")
    {
        Fixture f;
        Client evil("https://evil.example");
        evil.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port()));
        CHECK(evil.ws.state() != QAbstractSocket::ConnectedState);
        Client none{ QString() };
        none.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port()));
        CHECK(none.ws.state() != QAbstractSocket::ConnectedState);
        Client wrong_path(f.base());
        wrong_path.open(QString("ws://127.0.0.1:%1/other").arg(f.server.port()));
        CHECK(wrong_path.ws.state() != QAbstractSocket::ConnectedState);
    }

    TEST_CASE("stopping closes everyone; a taken port is refused")
    {
        Fixture f;
        Client c(f.base());
        REQUIRE(c.open(QString("ws://127.0.0.1:%1/ws").arg(f.server.port())));
        c.send({ { "hello", 1 }, { "token", kToken } });
        REQUIRE(wait_for([&] { return c.has("welcome"); }));
        quint16 port = f.server.port();
        FakeControl other_control;
        RemoteServer other(other_control, {});
        CHECK(!other.start(port, kToken));
        CHECK(!other.running());
        f.server.stop();
        CHECK(wait_for([&] { return c.closed; }));
        CHECK(!f.server.running());
    }
}
