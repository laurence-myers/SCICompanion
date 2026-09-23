// scic.exe: the command line of SCI Companion (docs/scic-cli/plan.md). The
// work is in the library (Src\Cli); this is its main.

#define VC_EXTRALEAN
#include <SDKDDKVer.h>
#include <afxwin.h>
#include <cstdio>
#include "Cli.h"

int main(int argc, char *argv[])
{
    // The library links MFC, so MFC must start first (plan section 7).
    if (!AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0))
    {
        fputs("scic: error: MFC did not start\n", stderr);
        return 1;
    }
    return cli::CliMain(argc, argv);
}
