#pragma once
#include "corrupt/remote_api.h"

#include <QHttpServer>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QWebSocket>

#include <functional>
#include <memory>
#include <vector>

// Remote control from a browser (roadmap #53): serves the remote page and its WebSocket on
// 127.0.0.1 only. Whatever makes it reachable from elsewhere (a tunnel with an access gate
// in front) lives outside the app and its code. Plan: docs/corruption/2026-11-01-remote-plan.md.
// Lives on the GUI thread.
class RemoteServer : public QObject
{
    Q_OBJECT

  public:
    struct Source
    {
        std::function<corrupt::remote::StatusView()> status;
        // A memory window as it is now; empty if there's none.
        std::function<std::vector<uint8_t>(corrupt::Target, uint32_t offset, uint32_t length)> read;
    };

    RemoteServer(corrupt::remote::Control &control, Source source, QObject *parent = nullptr);
    ~RemoteServer() override;

    // Listens on 127.0.0.1:port (0: any free port). False if the port is taken.
    bool start(quint16 port, const std::string &token);
    void stop();
    bool running() const { return tcp != nullptr; }
    quint16 port() const;
    QHostAddress address() const; // where it listens: always 127.0.0.1
    QStringList clients() const; // who is connected and past the token
    void setIntervals(int status_ms, int memory_ms);

  signals:
    void didSomething(const QString &who, const QString &what);
    void clientsChanged();

  private:
    struct Client
    {
        QPointer<QWebSocket> socket;
        std::unique_ptr<corrupt::remote::Connection> conn;
        QString who;
        std::string last_status;
        qint64 opened_ms = 0;
    };

    void accept();
    void received(Client *c, const QString &text);
    void pushStatus();
    void pushMemory();
    void drop(Client *c);

    corrupt::remote::Control &control;
    Source source;
    std::string token;
    corrupt::remote::Gate gate;
    std::unique_ptr<QHttpServer> http;
    QTcpServer *tcp = nullptr;
    std::vector<std::unique_ptr<Client>> clients_;
    QTimer status_timer, memory_timer;
};
