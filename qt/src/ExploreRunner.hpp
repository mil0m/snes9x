#pragma once
#include <QObject>
#include <QString>

#include <filesystem>
#include <functional>

class QProcess;

// Runs the headless explorer (#45, snes9x-explore) on a kept moment as a separate,
// low-priority process: the Snes9x core is one per process, and the game you play keeps the
// app's. Its progress lines are read as they come; Stop asks it to finish (it keeps what it
// found). Lives on the GUI thread.
class ExploreRunner : public QObject
{
  public:
    struct State
    {
        bool running = false;
        QString moment;          // the moment's name
        QString folder;          // where it writes, once it says
        unsigned tried = 0, alive = 0, visible = 0;
        unsigned normal_done = 0, normal_of = 0; // measuring normal play first (#82): runs done of how many
        double best = 0;
        double minutes = 0;      // how long it was asked to run
        qint64 started_ms = 0;   // when (QDateTime ms)
        QString error;           // why it couldn't start, or what it said when it failed
        QString done;            // its last line once finished
    };

    explicit ExploreRunner(QObject *parent = nullptr);
    ~ExploreRunner() override;

    // Where snes9x-explore is: $SNES9X_EXPLORE, beside the app, or the dev build's tests folder.
    // Empty: not found.
    static QString findTool();
    // For the tests: the program to run instead (a script that prints progress lines).
    void setTool(const QString &path) { tool = path; }

    // Starts exploring `moment` for `minutes`. Error or empty.
    QString start(const std::filesystem::path &rom, const std::filesystem::path &moment, const QString &moment_name,
                  const std::filesystem::path &library, const QString &user, double minutes);
    void stop();
    const State &state() const { return st; }

    std::function<void()> changed; // progress, a finish, an error

  private:
    void readLines();

    QString tool;
    QProcess *process = nullptr;
    State st;
    QByteArray pending;
};
