#pragma once
#include <initializer_list>

// Quits the app when one of these signals arrives. The handler only writes to a pipe;
// the main thread reads it and calls QApplication::quit(), so a signal that lands on the
// emulation thread can't deadlock it. A second signal kills the app the default way.
void installQuitSignals(std::initializer_list<int> sigs);
