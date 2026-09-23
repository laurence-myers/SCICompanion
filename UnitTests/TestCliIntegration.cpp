#include "stdafx.h"
#include "CppUnitTest.h"
#include "IntegrationHarness.h"
#include "Helper.h"
#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace IntegrationHarness;
namespace fs = std::filesystem;

namespace UnitTests
{
    // Plan step C1: scic.exe as a process. It must start with MFC, find its
    // data folder next to it, print the list to stdout, and exit with the
    // code of the command.
    TEST_CLASS(TestCliIntegration)
    {
    public:
        BEGIN_TEST_METHOD_ATTRIBUTE(ScicExe_ScriptList)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ScicExe_ScriptList)
        {
            std::string scic = GetTestModuleDirectory() + "\\scic.exe";
            Assert::IsTrue(fs::exists(scic), L"setup: scic.exe is next to the tests");
            std::string game = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            ChildOutput out = RunChildReadStdout("\"" + scic + "\" script list \"" + game + "\"", 60000);
            std::error_code ec;
            fs::remove_all(game, ec);
            Assert::IsTrue(out.launched && out.reachedEof, L"scic.exe runs and ends");
            Assert::AreEqual(0UL, out.exitCode);
            Assert::IsTrue(out.text.find("Main") != std::string::npos, std::wstring(out.text.begin(), out.text.end()).c_str());

            ChildOutput usage = RunChildReadStdout("\"" + scic + "\" script list", 60000);
            Assert::AreEqual(2UL, usage.exitCode, L"a usage error is exit code 2");
        }

        // Plan step C2: script decompile --stdout prints the source to the
        // stdout of scic.exe, and writes nothing.
        BEGIN_TEST_METHOD_ATTRIBUTE(ScicExe_DecompileToStdout)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ScicExe_DecompileToStdout)
        {
            std::string scic = GetTestModuleDirectory() + "\\scic.exe";
            Assert::IsTrue(fs::exists(scic), L"setup: scic.exe is next to the tests");
            std::string game = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            std::error_code ec;
            fs::remove_all(fs::path(game) / "src", ec);
            ChildOutput out = RunChildReadStdout("\"" + scic + "\" script decompile \"" + game + "\" door --stdout", 60000);
            bool wroteNothing = !fs::exists(fs::path(game) / "src");
            fs::remove_all(game, ec);
            Assert::IsTrue(out.launched && out.reachedEof, L"scic.exe runs and ends");
            Assert::AreEqual(0UL, out.exitCode);
            Assert::IsTrue(out.text.find("(script# 974)") != std::string::npos, std::wstring(out.text.begin(), out.text.end()).c_str());
            Assert::IsTrue(wroteNothing, L"no src folder");
        }

        // The crash handling (plan section 6.6): one line and exit code 1.
        // C1 review: before, abort() and std::terminate() ended the process
        // with exit code 3 and no line, and a bad parameter to a C runtime
        // function with 0xC0000409 and no line. The test hook SCIC_TEST_CRASH
        // makes scic.exe fail in each way.
        BEGIN_TEST_METHOD_ATTRIBUTE(ScicExe_ACrashIsOneLineAndExitCode1)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ScicExe_ACrashIsOneLineAndExitCode1)
        {
            std::string scic = GetTestModuleDirectory() + "\\scic.exe";
            Assert::IsTrue(fs::exists(scic), L"setup: scic.exe is next to the tests");
            for (const char *kind : { "access", "abort", "terminate", "invalid" })
            {
                SetEnvironmentVariableA("SCIC_TEST_CRASH", kind);
                // cmd puts stderr into the output that the harness reads.
                ChildOutput out = RunChildReadStdout("cmd /s /c \"\"" + scic + "\" --version 2>&1\"", 60000);
                SetEnvironmentVariableA("SCIC_TEST_CRASH", nullptr);
                std::string text = std::string(kind) + ": " + out.text;
                std::wstring wideText(text.begin(), text.end());
                Assert::IsTrue(out.launched && out.reachedEof, wideText.c_str());
                Assert::AreEqual(1UL, out.exitCode, wideText.c_str());
                Assert::IsTrue(out.text.find("scic: crash") != std::string::npos, wideText.c_str());
                Assert::IsTrue(out.text.find("while the crash test") != std::string::npos, wideText.c_str());
            }
        }
    };
}
