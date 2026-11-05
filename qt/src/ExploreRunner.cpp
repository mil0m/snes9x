#include "ExploreRunner.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

ExploreRunner::ExploreRunner(QObject *parent) : QObject(parent) {}

ExploreRunner::~ExploreRunner()
{
    if (process && process->state() != QProcess::NotRunning)
    {
        process->terminate(); // it saves what it found on the way out
        if (!process->waitForFinished(3000))
            process->kill();
    }
}

QString ExploreRunner::findTool()
{
    QString env = qEnvironmentVariable("SNES9X_EXPLORE");
    if (!env.isEmpty() && QFileInfo(env).isExecutable())
        return env;
    QDir app(QCoreApplication::applicationDirPath());
    for (auto rel : { "snes9x-explore", "../tests/snes9x-explore", "../../tests/snes9x-explore",
                      "../../../tests/snes9x-explore" })
        if (QFileInfo f(app.filePath(rel)); f.isExecutable())
            return f.canonicalFilePath();
    return {};
}

QString ExploreRunner::start(const std::filesystem::path &rom, const std::filesystem::path &moment,
                             const QString &moment_name, const std::filesystem::path &library, const QString &user,
                             double minutes)
{
    if (st.running)
        return tr("It's already exploring “%1”. Stop that first.").arg(st.moment);
    QString program = tool.isEmpty() ? findTool() : tool;
    if (program.isEmpty())
        return tr("The explorer (snes9x-explore) wasn't found beside the app. Build it with ./build-mac.sh test, "
                  "or set SNES9X_EXPLORE to where it is.");
    if (rom.empty())
        return tr("No game loaded");
    if (library.empty())
        return tr("Choose a library folder first: explorations go in it");
    st = {};
    st.running = true;
    st.moment = moment_name;
    st.minutes = minutes;
    st.started_ms = QDateTime::currentMSecsSinceEpoch();
    pending.clear();
    delete process;
    process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = { QString::fromStdString(rom.string()), QString::fromStdString(moment.string()),
                         "--library", QString::fromStdString(library.string()), "--user", user, "--minutes",
                         QString::number(minutes) };
#ifndef _WIN32
    // Low priority: the game you play comes first.
    if (QFileInfo("/usr/bin/nice").isExecutable() && tool.isEmpty())
    {
        args.prepend(program);
        args.prepend("10");
        args.prepend("-n");
        program = "/usr/bin/nice";
    }
#endif
    connect(process, &QProcess::readyRead, this, &ExploreRunner::readLines);
    connect(process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readLines();
        st.running = false;
        if (status != QProcess::NormalExit || code != 0)
            st.error = st.done.isEmpty() ? tr("The explorer stopped unexpectedly (exit %1)").arg(code) : st.done;
        if (changed)
            changed();
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        st.running = false;
        st.error = tr("The explorer couldn't start: %1").arg(process->errorString());
        if (changed)
            changed();
    });
    process->start(program, args);
    if (changed)
        changed();
    return {};
}

void ExploreRunner::stop()
{
    if (process && process->state() != QProcess::NotRunning)
        process->terminate();
}

void ExploreRunner::readLines()
{
    if (!process)
        return;
    pending += process->readAll();
    bool any = false;
    int nl;
    while ((nl = pending.indexOf('\n')) >= 0)
    {
        QString line = QString::fromUtf8(pending.left(nl)).trimmed();
        pending.remove(0, nl + 1);
        any = true;
        if (line.startsWith("progress "))
        {
            auto p = line.split(' ');
            if (p.size() >= 5)
            {
                st.tried = p[1].toUInt();
                st.alive = p[2].toUInt();
                st.visible = p[3].toUInt();
                st.best = p[4].toDouble();
            }
        }
        else if (line.startsWith("folder "))
            st.folder = line.mid(7).trimmed();
        else if (line.startsWith("done "))
            st.done = line.mid(5).trimmed();
        else if (!line.startsWith("knows ") && !line.startsWith("check ") && !line.isEmpty())
            st.done = line; // what it said last (an error, when it fails)
    }
    if (any && changed)
        changed();
}
