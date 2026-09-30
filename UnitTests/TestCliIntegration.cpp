#include "stdafx.h"
#include "CppUnitTest.h"
#include "IntegrationHarness.h"
#include "Helper.h"
#include "TestSupport.h"
#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace IntegrationHarness;
namespace fs = std::filesystem;

namespace UnitTests
{
    // scic.exe as a process. It must start with MFC, find its data folder
    // next to it, print the list to stdout, and exit with the code of the
    // command.
    TEST_CLASS(TestCliIntegration)
    {
        GameCopy _game;

        // scic.exe next to the tests; an assert fails when it is not there.
        static std::string ScicPath()
        {
            std::string scic = GetTestModuleDirectory() + "\\scic.exe";
            Assert::IsTrue(fs::exists(scic), L"setup: scic.exe is next to the tests");
            return scic;
        }

    public:
        BEGIN_TEST_METHOD_ATTRIBUTE(ScicExe_ScriptList)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ScicExe_ScriptList)
        {
            ChildOutput out = RunChildReadStdout("\"" + ScicPath() + "\" script list \"" + _game.Make(TemplateSci0) + "\"", 60000);
            Assert::IsTrue(out.launched && out.reachedEof, L"scic.exe runs and ends");
            Assert::AreEqual(0UL, out.exitCode);
            Assert::IsTrue(out.text.find("Main") != std::string::npos, Wide(out.text).c_str());

            ChildOutput usage = RunChildReadStdout("\"" + ScicPath() + "\" script list", 60000);
            Assert::AreEqual(2UL, usage.exitCode, L"a usage error is exit code 2");
        }

        // script decompile --stdout prints the source to the stdout of
        // scic.exe, and writes nothing.
        BEGIN_TEST_METHOD_ATTRIBUTE(ScicExe_DecompileToStdout)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ScicExe_DecompileToStdout)
        {
            _game.Make(TemplateSci0);
            RemoveFolder(_game.Path("src"));
            ChildOutput out = RunChildReadStdout("\"" + ScicPath() + "\" script decompile \"" + _game.Folder() + "\" door --stdout", 60000);
            Assert::IsTrue(out.launched && out.reachedEof, L"scic.exe runs and ends");
            Assert::AreEqual(0UL, out.exitCode);
            Assert::IsTrue(out.text.find("(script# 974)") != std::string::npos, Wide(out.text).c_str());
            Assert::IsFalse(_game.Has("src"), L"no src folder");
        }

        // The crash handling (plan section 6.6): one line and exit code 1,
        // also for abort(), std::terminate() and a bad parameter to a C
        // runtime function. Without the SIGABRT handler, the first two end
        // the process with exit code 3 and no line (with the abort behaviour
        // that InstallCrashHandling sets). Without the invalid-parameter
        // handler, the third ends it with 0xC0000409 and no line. The test
        // hook SCIC_TEST_CRASH makes scic.exe fail in each way.
        BEGIN_TEST_METHOD_ATTRIBUTE(ScicExe_ACrashIsOneLineAndExitCode1)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ScicExe_ACrashIsOneLineAndExitCode1)
        {
            std::string scic = ScicPath();
            for (const char *kind : { "access", "abort", "terminate", "invalid" })
            {
                SetEnvironmentVariableA("SCIC_TEST_CRASH", kind);
                // cmd puts stderr into the output that the harness reads.
                ChildOutput out = RunChildReadStdout("cmd /s /c \"\"" + scic + "\" --version 2>&1\"", 60000);
                SetEnvironmentVariableA("SCIC_TEST_CRASH", nullptr);
                std::wstring wideText = Wide(std::string(kind) + ": " + out.text);
                Assert::IsTrue(out.launched && out.reachedEof, wideText.c_str());
                Assert::AreEqual(1UL, out.exitCode, wideText.c_str());
                Assert::IsTrue(out.text.find("scic: crash") != std::string::npos, wideText.c_str());
                Assert::IsTrue(out.text.find("while the crash test") != std::string::npos, wideText.c_str());
            }
        }
    };
}
