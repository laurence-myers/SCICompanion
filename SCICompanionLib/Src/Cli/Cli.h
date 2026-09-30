#pragma once

// The command line of scic (plan sections 4 and 7). It is in the library,
// so that a test runs it in its own process; scic.exe's main calls
// CliMain.

#include <string>
#include <vector>

namespace cli
{
    class ICliConsole;

    // Parses the arguments (with no program name), opens the game, runs the
    // command, and prints its report. Returns the exit code (plan section
    // 8). It installs no handler for the process.
    int RunCli(const std::vector<std::string> &args, ICliConsole &console);

    // The main of scic.exe: the crash handling, the Ctrl+C handler, the
    // console, and RunCli.
    int CliMain(int argc, char *argv[]);
}
