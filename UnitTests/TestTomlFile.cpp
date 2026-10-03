#include "stdafx.h"
#include "CppUnitTest.h"
#include "TestSupport.h"
#include "TomlFile.h"
#include <filesystem>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // ParseTomlFile reads Decompiler.ini and the phoneme maps.
    TEST_CLASS(TestTomlFile)
    {
        std::string _folder;

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            _folder = (std::filesystem::temp_directory_path() / ("scicompanion-toml-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()))).string();
            std::filesystem::create_directories(_folder);
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            RemoveFolder(_folder);
        }

        TEST_METHOD(ValidToml_GivesTheTable)
        {
            std::string path = _folder + "\\valid.ini";
            WriteFileText(path,
                "[first]\n"
                "number = 12\n"
                "names = [ \"a\", \"b\" ]\n");

            sci::Result<toml::table> table = ParseTomlFile(path);

            AssertOk(table, "parse");
            Assert::AreEqual((int64_t)12, (*table)["first"]["number"].value_or((int64_t)0));
            const toml::array *names = (*table)["first"]["names"].as_array();
            Assert::IsNotNull(names);
            Assert::AreEqual((size_t)2, names->size());
        }

        TEST_METHOD(MissingFile_IsNotFound)
        {
            std::string path = _folder + "\\missing.ini";

            sci::Result<toml::table> table = ParseTomlFile(path);

            Assert::IsFalse(table.has_value());
            Assert::IsTrue(table.error().code == sci::ErrorCode::NotFound, Wide(table.error().ToString()).c_str());
            Assert::IsTrue(table.error().message.find(path) != std::string::npos, L"the error names the file");
        }

        TEST_METHOD(InvalidToml_IsFormatWithTheLineAndColumn)
        {
            std::string path = _folder + "\\invalid.ini";
            WriteFileText(path,
                "[first]\n"
                "number = = 12\n");

            sci::Result<toml::table> table = ParseTomlFile(path);

            Assert::IsFalse(table.has_value());
            const sci::Error &error = table.error();
            Assert::IsTrue(error.code == sci::ErrorCode::Format, Wide(error.ToString()).c_str());
            Assert::AreEqual(path, error.where.file);
            Assert::AreEqual(2, error.where.line);
            Assert::IsTrue(error.where.column > 0, Wide(error.ToString()).c_str());
            Assert::IsFalse(error.message.empty());
        }

        // The game folder is a path in the ANSI code page, as the rest of the
        // program has it; toml::parse_file would take it as UTF-8.
        TEST_METHOD(AnsiPath_IsRead)
        {
            if (GetACP() != 1252)
            {
                Logger::WriteMessage("Skipped: the test needs the ANSI code page 1252.");
                return;
            }
            std::string folder = _folder + "\\caf\xe9";
            std::filesystem::create_directories(folder);
            std::string path = folder + "\\valid.ini";
            WriteFileText(path, "number = 12\n");

            sci::Result<toml::table> table = ParseTomlFile(path);

            AssertOk(table, "parse");
            Assert::AreEqual((int64_t)12, (*table)["number"].value_or((int64_t)0));
        }
    };
}
