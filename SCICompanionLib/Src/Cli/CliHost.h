#pragma once

// The process settings of scic.exe (plan sections 6.6 and 7): no crash
// dialog, one line for a crash, and Ctrl+C. Only scic.exe's main installs
// them: a test that runs RunCli in its own process does not.

#include <atomic>
#include <string>

namespace cli
{
    // No crash dialog. An unhandled exception prints one line, for example
    // "scic: crash 0xC0000005 while compiling script 110", and ends the
    // process with exit code 1.
    void InstallCrashHandling();

    // The item that this thread works on now, for the crash line. An empty
    // text removes it.
    void SetCurrentItem(const std::string &item);

    // Ctrl+C (and Ctrl+Break) sets the cancel flag; a second one ends the
    // process at once, with exit code 7.
    void InstallCancelHandler();

    // The flag that Ctrl+C sets. A command passes it to the batch.
    std::atomic<bool> &CancelFlag();
}
