#include "doctest/doctest.h"
#include "QuitSignals.hpp"

#include <QApplication>
#include <QTimer>

#include <csignal>
#include <thread>

namespace
{

// Runs the event loop until something quits it; a timeout gives 1 instead of hanging.
int runLoop()
{
    QTimer::singleShot(3000, qApp, [] { QApplication::exit(1); });
    return QApplication::exec();
}

bool isDefault(int sig)
{
    struct sigaction now = {};
    sigaction(sig, nullptr, &now);
    return now.sa_handler == SIG_DFL;
}

} // namespace

TEST_CASE("a signal quits the app from the main thread, wherever it lands")
{
    // SIGUSR1 and SIGUSR2 stand in for SIGTERM and SIGINT, so the runner stays safe.
    installQuitSignals({ SIGUSR1, SIGUSR2 });

    SUBCASE("on another thread, like the emulation thread")
    {
        // The old handler called QApplication::quit() right there and could deadlock.
        QTimer::singleShot(0, qApp, [] { std::thread([] { raise(SIGUSR1); }).join(); });
        CHECK(runLoop() == 0);
        CHECK(isDefault(SIGUSR1)); // a second one kills a stuck shutdown
    }

    SUBCASE("on the main thread")
    {
        QTimer::singleShot(0, qApp, [] { raise(SIGUSR2); });
        CHECK(runLoop() == 0);
        CHECK(isDefault(SIGUSR2));
    }
}
