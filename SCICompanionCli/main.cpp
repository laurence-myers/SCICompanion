// scic.exe: the command line of SCI Companion (docs/scic-cli/plan.md). The
// work is in the core library (Src\Cli); this is its main.

#include <SDKDDKVer.h>
#include "Cli.h"

int main(int argc, char *argv[])
{
    return cli::CliMain(argc, argv);
}