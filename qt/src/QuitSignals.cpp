#include "QuitSignals.hpp"

#include <QApplication>
#include <QSocketNotifier>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace
{

int pipe_fds[2] = { -1, -1 };

// Only async-signal-safe calls here: the signal can land on any thread, mid-anything.
void onSignal(int sig)
{
    signal(sig, SIG_DFL); // a second one kills a shutdown that hangs
    char byte = 1;
    [[maybe_unused]] auto written = write(pipe_fds[1], &byte, 1);
}

} // namespace

void installQuitSignals(std::initializer_list<int> sigs)
{
    if (pipe_fds[0] < 0)
    {
        if (pipe(pipe_fds) != 0)
            return;
        for (int fd : pipe_fds)
        {
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            fcntl(fd, F_SETFL, O_NONBLOCK);
        }
        auto notifier = new QSocketNotifier(pipe_fds[0], QSocketNotifier::Read, qApp);
        QObject::connect(notifier, &QSocketNotifier::activated, [] {
            char bytes[16];
            while (read(pipe_fds[0], bytes, sizeof(bytes)) > 0)
                ;
            QApplication::quit();
        });
    }

    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int s : sigs)
        sigaction(s, &action, nullptr);
}

#else

void installQuitSignals(std::initializer_list<int>)
{
}

#endif
