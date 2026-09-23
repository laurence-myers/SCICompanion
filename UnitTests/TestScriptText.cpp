#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScriptText.h"
#include "CCrystalTextBuffer.h"
#include "CrystalScriptStream.h"
#include "Helper.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // Plan step B2. Before it, the engine read scripts and headers only
    // through the script editor's buffer (CCrystalTextBuffer, CrystalEdit).
    // LoadScriptText must split lines exactly as that buffer does, or the
    // line numbers of diagnostics change.
    TEST_CLASS(TestScriptText)
    {
        std::string _folder;

        static std::wstring Wide(const std::string &text)
        {
            return std::wstring(text.begin(), text.end());
        }

        static std::vector<std::string> EditorLines(const std::string &path)
        {
            CCrystalTextBuffer buffer;
            Assert::IsTrue(!!buffer.LoadFromFile(path.c_str()), Wide(path).c_str());
            std::vector<std::string> lines;
            for (int i = 0; i < buffer.GetLineCount(); i++)
            {
                PCTSTR chars = buffer.GetLineChars(i);
                int length = buffer.GetLineLength(i);
                lines.push_back((chars && (length > 0)) ? std::string(chars, length) : std::string());
            }
            buffer.FreeAll();
            return lines;
        }

        static void AssertSameLines(const std::string &path)
        {
            sci::Result<ScriptText> text = LoadScriptText(path);
            Assert::IsTrue(text.has_value(), Wide(path).c_str());
            std::vector<std::string> expected = EditorLines(path);
            Assert::AreEqual(expected.size(), text->lines.size(), Wide(path + ": line count").c_str());
            for (size_t i = 0; i < expected.size(); i++)
            {
                if (expected[i] != text->lines[i])
                {
                    Assert::Fail(Wide(path + ": line " + std::to_string(i + 1) + " differs").c_str());
                }
            }
        }

        std::string WriteTestFile(const std::string &name, const std::string &contents)
        {
            std::string path = _folder + "\\" + name;
            std::ofstream file(path, std::ios::binary);
            file.write(contents.data(), contents.size());
            return path;
        }

        static std::string LowerExtension(const std::filesystem::path &path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) { return (char)tolower((uint8_t)c); });
            return extension;
        }

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            std::filesystem::path folder = std::filesystem::temp_directory_path() / ("ScriptTextTest_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()));
            std::filesystem::create_directories(folder);
            _folder = folder.string();
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            std::error_code ec;
            std::filesystem::remove_all(_folder, ec);
        }

        TEST_METHOD(TemplateScriptsAndHeaders_SameLinesAsTheEditor)
        {
            std::vector<std::string> folders =
            {
                GetTestModuleDirectory() + "\\TemplateGame\\SCI0\\src",
                GetTestModuleDirectory() + "\\TemplateGame\\SCI1.1\\src",
                GetTestModuleDirectory() + "\\include",
            };
            int fileCount = 0;
            for (const std::string &folder : folders)
            {
                for (const auto &entry : std::filesystem::directory_iterator(folder))
                {
                    std::string extension = LowerExtension(entry.path());
                    if ((extension == ".sc") || (extension == ".sh"))
                    {
                        AssertSameLines(entry.path().string());
                        fileCount++;
                    }
                }
            }
            Assert::IsTrue(fileCount > 50, std::to_wstring(fileCount).c_str());
        }

        TEST_METHOD(CraftedFiles_SameLinesAsTheEditor)
        {
            const char nulText[] = "(a)\0junk\r\n(b)\r\n";
            const std::pair<const char *, std::string> cases[] =
            {
                { "crlf.sc", "(a)\r\n(b)\r\n" },
                { "lf.sc", "(a)\n(b)\n(c)" },
                { "cr.sc", "(a)\r(b)\r" },
                { "mixed.sc", "(a)\r\n(b)\n(c)\r\n(d)\r(e)\r\n" },
                { "lf-then-crlf.sc", "(a)\n(b)\r\n(c)" },
                { "lfcr.sc", "(a)\n\r(b)\n\r" },
                { "crcrlf.sc", "(a)\r\r\n(b)\r\n" },
                { "no-final-break.sc", "(a)\r\n(b)" },
                { "empty.sc", "" },
                { "only-break.sc", "\r\n" },
                { "nul.sc", std::string(nulText, sizeof(nulText) - 1) },
                { "late-lf.sc", std::string(40000, ';') + "\n(a)\r\n(b)\n" },
            };
            for (const auto &entry : cases)
            {
                AssertSameLines(WriteTestFile(entry.first, entry.second));
            }
        }

        TEST_METHOD(SplitScriptText_MixedBreaks_FollowTheFirstStyle)
        {
            // The first break is CR LF, so a bare LF and a bare CR end no line.
            ScriptText text = SplitScriptText("(a)\r\n(b)\n(c)\r\n(d)\r(e)");
            Assert::AreEqual(size_t(3), text.lines.size());
            Assert::AreEqual(std::string("(a)"), text.lines[0]);
            Assert::AreEqual(std::string("(b)\n(c)"), text.lines[1]);
            Assert::AreEqual(std::string("(d)\r(e)"), text.lines[2]);
        }

        TEST_METHOD(LoadScriptText_MissingFile_ReturnsNotFound)
        {
            std::string path = _folder + "\\missing.sc";
            sci::Result<ScriptText> text = LoadScriptText(path);
            Assert::IsFalse(text.has_value());
            Assert::AreEqual(std::string("not-found"), std::string(sci::ErrorCodeName(text.error().code)));
            std::string message = text.error().ToString();
            Assert::IsTrue(message.find("missing.sc") != std::string::npos, Wide(message).c_str());
        }

        TEST_METHOD(StreamLimiter_FromScriptText_SameAsFromTheEditor)
        {
            std::string path = WriteTestFile("stream.sc", "(script# 5)\r\n\r\n(instance a of b)\r\n\t(properties x 1)");
            CCrystalTextBuffer buffer;
            Assert::IsTrue(!!buffer.LoadFromFile(path.c_str()));
            CScriptStreamLimiter fromEditor(&buffer);
            sci::Result<ScriptText> text = LoadScriptText(path);
            Assert::IsTrue(text.has_value());
            CScriptStreamLimiter fromText(*text);

            Assert::AreEqual(fromEditor.GetLineCount(), fromText.GetLineCount());
            for (int i = 0; i < fromEditor.GetLineCount(); i++)
            {
                int length = fromEditor.GetLineLength(i);
                Assert::AreEqual(length, fromText.GetLineLength(i));
                if (length > 0)
                {
                    Assert::AreEqual(std::string(fromEditor.GetLineChars(i), length), std::string(fromText.GetLineChars(i), length));
                }
            }
            Assert::AreEqual(fromEditor.GetLimit().line, fromText.GetLimit().line);
            Assert::AreEqual(fromEditor.GetLimit().column, fromText.GetLimit().column);
            buffer.FreeAll();
        }
    };
}
