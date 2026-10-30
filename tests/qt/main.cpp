// Qt widget tests for the corruption panel. They run on Qt's offscreen platform,
// so they need no screen; set CORRUPT_SHOTS_DIR to also write PNGs of the dialogs.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <QApplication>
#include <cstdlib>

int main(int argc, char **argv)
{
    setenv("QT_QPA_PLATFORM", "offscreen", 0);
    QApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
}
