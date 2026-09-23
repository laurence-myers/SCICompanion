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
    };
}
