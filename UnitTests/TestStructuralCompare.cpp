#include "stdafx.h"
#include "CppUnitTest.h"
#include "StructuralCompare.h"
#include "TestSupport.h"
#include "format.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // A script in Sierra syntax: the header, a public block with the slots,
    // and the bodies.
    std::string MakeScript(int number, const std::vector<std::pair<std::string, int>> &exports, const std::string &bodies)
    {
        std::string text = ";;; Sierra Script 1.0 - (do not remove this comment)\r\n";
        text += fmt::format("(script# {0})\r\n(include sci.sh)\r\n(public\r\n", number);
        for (const auto &slot : exports)
        {
            text += fmt::format("\t{0} {1}\r\n", slot.first, slot.second);
        }
        text += ")\r\n(local\r\n\ta\r\n\tb\r\n)\r\n" + bodies;
        return text;
    }

    std::string Procedure(const std::string &name, const std::string &body)
    {
        return "(procedure (" + name + ")\r\n\t" + body + "\r\n)\r\n";
    }

    const char *AsmBody = "(asm\r\n\t\tlofsa    {a ) b}\r\n\t\t; a comment )\r\n\t\tldi      1\r\n\t\tret\r\n\t)";

    // Three folders (expected, actual, baseline) in the temp folder; the
    // destructor deletes them.
    struct CompareFolders
    {
        std::string root;
        CompareFolders()
        {
            root = (fs::temp_directory_path() / fmt::format("scic-compare-{0}", GetCurrentProcessId())).string();
            RemoveFolder(root);
            for (const char *side : { "expected", "actual", "baseline" })
            {
                fs::create_directories(fs::path(root) / side);
            }
        }
        ~CompareFolders() { RemoveFolder(root); }
        std::string Folder(const char *side) const { return (fs::path(root) / side).string(); }
        void Write(const char *side, const std::string &file, const std::string &text) const
        {
            WriteFileText((fs::path(root) / side / file).string(), text);
        }
    };

    // The rows by "script:function".
    std::map<std::string, FunctionCompareRow> RowsOf(const FolderCompareResult &result)
    {
        std::map<std::string, FunctionCompareRow> rows;
        for (const FunctionCompareRow &row : result.rows)
        {
            rows[fmt::format("{0}:{1}", row.script, row.display)] = row;
        }
        return rows;
    }

    std::string RowsText(const FolderCompareResult &result)
    {
        std::string text;
        for (const FunctionCompareRow &row : result.rows)
        {
            text += fmt::format("{0}:{1} [{2}] {3} {4} {5}\n", row.script, row.display, row.key, StructureVerdictName(row.verdict),
                row.hasBaseline ? StructureVerdictName(row.baselineVerdict) : "-", StructureChangeName(row.change));
        }
        for (const std::string &error : result.errors)
        {
            text += "error: " + error + "\n";
        }
        return text;
    }
}

namespace UnitTests
{
    // The structure compare of decompiled scripts (scic dev
    // compare-structure). It needs no game.
    TEST_CLASS(TestStructuralCompare)
    {
        NoAppState _noAppState;

    public:
        // An asm block becomes the marker; a parenthesis in a {} string, a
        // "" string or a comment inside it does not end it. Text that only
        // starts with "(asm" stays, and so does "(asm" in a string.
        TEST_METHOD(ReplaceAsmBlocks_SkipsStringsAndComments)
        {
            std::string marker = std::string("(") + AsmBlockMarker + ")";
            Assert::AreEqual("(procedure (p) " + marker + ")", ReplaceAsmBlocks("(procedure (p) (asm\n lofsa {a ) b}\n ; c )\n lofsa \"x(\\\"y\"\n ret\n))"));
            Assert::AreEqual(std::string("(asmFoo 1) \"(asm )\" ; (asm\n"), ReplaceAsmBlocks("(asmFoo 1) \"(asm )\" ; (asm\n"));
            // A backslash escapes the close of a {} string and of a said string.
            Assert::AreEqual("(p " + marker + " (q))", ReplaceAsmBlocks("(p (asm lofsa {x \\} ) y} said 'a\\')' ret) (q))"));
        }

        // The skeleton keeps the control statements and their nesting: the
        // value of a switch is not a case, an else stays, the rest goes.
        TEST_METHOD(StructureSkeleton_ControlStatementsOnly)
        {
            Assert::AreEqual(std::string("(switch (case) (else (break)))"), StructureSkeleton("(switch v (v (= v v)) (else (break)))"));
            Assert::AreEqual(std::string("(switch (case (break)))"), StructureSkeleton("(switch (v x:) (v (break)))"));
            Assert::AreEqual(std::string("(switchto (case) (case))"), StructureSkeleton("(switchto (v) (v))"));
            Assert::AreEqual(std::string("(if (and) else (return))"), StructureSkeleton("(if (and v (v x:)) (v) else (return v))"));
            Assert::AreEqual(std::string("(while (not) (if (continue)))"), StructureSkeleton("(= v v) (while (not v) (if v (continue)) (v doit:))"));
        }

        // Each verdict and each change, with the scripts paired by number
        // (the files have other names), a script that only one side has,
        // and a local procedure that the actual side does not have.
        TEST_METHOD(CompareScriptFolders_VerdictsAndChanges)
        {
            CompareFolders folders;
            std::vector<std::pair<std::string, int>> exports = { { "pSame", 0 }, { "pNames", 1 }, { "pShape", 2 }, { "pDiff", 3 }, { "pAsm", 4 }, { "pSource", 5 }, { "pBoth", 6 } };
            std::string locals =
                Procedure("localproc_0", "(if a (= b 1))") +
                Procedure("localproc_1", "(while a (= b 1) (-- a))") +
                Procedure("localproc_2", "(switch a (1 (= b 1)) (else (= b 2)))");
            folders.Write("expected", "rm005.sc", MakeScript(5, exports,
                Procedure("pSame", "(if a (= b 1))") +
                Procedure("pNames", "(= a 1)") +
                Procedure("pShape", "(if a (= b 1))") +
                Procedure("pDiff", "(if a (= b 1))") +
                Procedure("pAsm", "(if a (= b 1))") +
                Procedure("pSource", AsmBody) +
                Procedure("pBoth", AsmBody) +
                Procedure("pOnlyExpected", "(= a 1)") + locals));
            // The actual side: another file name, no pOnlyExpected, one
            // slot more, and no localproc_1 (its other locals are named by
            // offset).
            std::string actualLocals =
                Procedure("localproc_0100", "(if a (= b 1))") +
                Procedure("localproc_0200", "(switch a (1 (= b 1)) (else (= b 2)))");
            std::string actualPublic = MakeScript(5, { { "pSame", 0 }, { "pNames", 1 }, { "pShape", 2 }, { "pDiff", 3 }, { "pAsm", 4 }, { "pSource", 5 }, { "pBoth", 6 }, { "pOnlyActual", 7 } }, "");
            auto actualScript = [&](const std::string &names, const std::string &diff, const std::string &asmBody)
            {
                return actualPublic +
                    Procedure("pSame", "(if a (= b 1))") +
                    Procedure("pNames", names) +
                    Procedure("pShape", "(if a (b doit:))") +
                    Procedure("pDiff", diff) +
                    Procedure("pAsm", asmBody) +
                    Procedure("pSource", "(= a 2)") +
                    Procedure("pBoth", AsmBody) +
                    Procedure("pOnlyActual", "(= a 1)") + actualLocals;
            };
            folders.Write("actual", "Five.sc", actualScript("(= b 2)", "(while a (= b 1))", AsmBody));
            // The baseline: pNames had another text, pDiff was asm, pAsm was
            // source.
            folders.Write("baseline", "Five.sc", actualScript("(= b 3)", AsmBody, "(if a (= b 1))") + Procedure("pGone", "(= a 3)"));
            // A script that only the baseline has.
            folders.Write("baseline", "rm010.sc", MakeScript(10, { { "pOld", 0 } }, Procedure("pOld", "(= a 1)")));
            // A script that only the actual side has.
            folders.Write("actual", "rm006.sc", MakeScript(6, { { "pNew", 0 } }, Procedure("pNew", "(= a 1)")));

            FolderCompareResult result = CompareScriptFolders(folders.Folder("expected"), folders.Folder("actual"), folders.Folder("baseline"), sciVersion1_1);
            std::string text = RowsText(result);
            Assert::IsTrue(result.errors.empty(), Wide(text).c_str());
            std::map<std::string, FunctionCompareRow> rows = RowsOf(result);
            struct Expect { const char *function; StructureVerdict verdict; StructureChange change; };
            for (const Expect &expect : {
                Expect{ "5:pSame", StructureVerdict::Same, StructureChange::None },
                Expect{ "5:pNames", StructureVerdict::Names, StructureChange::Changed },
                Expect{ "5:pShape", StructureVerdict::Shape, StructureChange::None },
                Expect{ "5:pDiff", StructureVerdict::Diff, StructureChange::Fixed },
                Expect{ "5:pAsm", StructureVerdict::Asm, StructureChange::Regressed },
                Expect{ "5:pSource", StructureVerdict::Source, StructureChange::None },
                Expect{ "5:pBoth", StructureVerdict::BothAsm, StructureChange::None },
                Expect{ "5:pOnlyExpected", StructureVerdict::OnlyExpected, StructureChange::None },
                Expect{ "5:pOnlyActual", StructureVerdict::OnlyActual, StructureChange::None },
                Expect{ "5:localproc_0100", StructureVerdict::Same, StructureChange::None },
                Expect{ "5:localproc_0200", StructureVerdict::Same, StructureChange::None },
                Expect{ "5:localproc_1", StructureVerdict::OnlyExpected, StructureChange::None },
                Expect{ "6:pNew", StructureVerdict::OnlyActual, StructureChange::Added },
                Expect{ "5:pGone", StructureVerdict::Neither, StructureChange::Removed },
                Expect{ "10:pOld", StructureVerdict::Neither, StructureChange::Removed } })
            {
                auto row = rows.find(expect.function);
                Assert::IsTrue(row != rows.end(), Wide(std::string(expect.function) + " is missing\n" + text).c_str());
                Assert::AreEqual(std::string(StructureVerdictName(expect.verdict)), std::string(StructureVerdictName(row->second.verdict)), Wide(std::string(expect.function) + "\n" + text).c_str());
                Assert::AreEqual(std::string(StructureChangeName(expect.change)), std::string(StructureChangeName(row->second.change)), Wide(std::string(expect.function) + "\n" + text).c_str());
            }
            Assert::AreEqual(std::string("ASM"), std::string(StructureVerdictName(rows["5:pDiff"].baselineVerdict)), L"the baseline verdict: baseline asm, expected source");
            Assert::AreEqual(std::string("NEITHER"), std::string(StructureVerdictName(rows["6:pNew"].baselineVerdict)), L"the baseline verdict: no expected and no baseline function");
            Assert::AreEqual((size_t)15, result.rows.size(), Wide(text).c_str());
        }

        // A file that does not parse, and a script number that two files of
        // a folder have, are errors, and their script has no rows.
        TEST_METHOD(CompareScriptFolders_ErrorsLeaveTheScriptOut)
        {
            CompareFolders folders;
            std::string good = MakeScript(7, { { "p", 0 } }, Procedure("p", "(= a 1)"));
            folders.Write("expected", "a.sc", good);
            folders.Write("actual", "a.sc", ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 7)\r\n(procedure (p)\r\n\t(= a\r\n");
            folders.Write("expected", "b.sc", MakeScript(8, { { "p", 0 } }, Procedure("p", "(= a 1)")));
            folders.Write("actual", "b.sc", MakeScript(8, { { "p", 0 } }, Procedure("p", "(= a 1)")));
            folders.Write("actual", "c.sc", MakeScript(8, { { "p", 0 } }, Procedure("p", "(= a 2)")));
            folders.Write("expected", "d.sc", MakeScript(9, { { "p", 0 } }, Procedure("p", "(= a 1)")));
            folders.Write("actual", "d.sc", MakeScript(9, { { "p", 0 } }, Procedure("p", "(= a 1)")));
            FolderCompareResult result = CompareScriptFolders(folders.Folder("expected"), folders.Folder("actual"), "", sciVersion1_1);
            std::string text = RowsText(result);
            Assert::AreEqual((size_t)2, result.errors.size(), Wide(text).c_str());
            Assert::AreEqual((size_t)1, result.rows.size(), Wide(text).c_str());
            Assert::AreEqual((uint16_t)9, result.rows[0].script, Wide(text).c_str());
            Assert::IsFalse(result.rows[0].hasBaseline, Wide(text).c_str());
        }

        // A script of the expected side that does not parse: its functions
        // get UNPARSED, and the change from the baseline is still there. A
        // baseline script that does not parse gives UNPARSED as the baseline
        // verdict and no change.
        TEST_METHOD(CompareScriptFolders_AnUnparsedSideKeepsTheOtherCompares)
        {
            CompareFolders folders;
            std::string broken = ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 12)\r\n(procedure (p)\r\n\t(= a\r\n";
            folders.Write("expected", "a.sc", broken);
            folders.Write("actual", "a.sc", MakeScript(12, { { "p", 0 } }, Procedure("p", "(= a 1)")));
            folders.Write("baseline", "a.sc", MakeScript(12, { { "p", 0 } }, Procedure("p", AsmBody)));
            folders.Write("expected", "b.sc", MakeScript(13, { { "p", 0 } }, Procedure("p", "(= a 1)")));
            folders.Write("actual", "b.sc", MakeScript(13, { { "p", 0 } }, Procedure("p", "(= a 2)")));
            folders.Write("baseline", "b.sc", std::string(broken).replace(broken.find("12"), 2, "13"));
            FolderCompareResult result = CompareScriptFolders(folders.Folder("expected"), folders.Folder("actual"), folders.Folder("baseline"), sciVersion1_1);
            std::string text = RowsText(result);
            Assert::AreEqual((size_t)2, result.errors.size(), Wide(text).c_str());
            std::map<std::string, FunctionCompareRow> rows = RowsOf(result);
            Assert::AreEqual((size_t)2, rows.size(), Wide(text).c_str());
            Assert::AreEqual(std::string("UNPARSED"), std::string(StructureVerdictName(rows["12:p"].verdict)), Wide(text).c_str());
            Assert::AreEqual(std::string("UNPARSED"), std::string(StructureVerdictName(rows["12:p"].baselineVerdict)), Wide(text).c_str());
            Assert::AreEqual(std::string("FIXED"), std::string(StructureChangeName(rows["12:p"].change)), Wide(text).c_str());
            Assert::AreEqual(std::string("NAMES"), std::string(StructureVerdictName(rows["13:p"].verdict)), Wide(text).c_str());
            Assert::AreEqual(std::string("UNPARSED"), std::string(StructureVerdictName(rows["13:p"].baselineVerdict)), Wide(text).c_str());
            Assert::AreEqual(std::string(), std::string(StructureChangeName(rows["13:p"].change)), Wide(text).c_str());
        }

        // Snuffer's groups of one expression lose their parentheses; a send
        // to a call, a call with an argument, a string and a comment stay.
        TEST_METHOD(UnwrapGroupedExpressions_OneExpressionInAGroup)
        {
            Assert::AreEqual(std::string(" (= fp (Foo new:)) "), UnwrapGroupedExpressions("((= fp (Foo new:)))"));
            Assert::AreEqual(std::string("( [p i]  isKindOf: C)"), UnwrapGroupedExpressions("(([p i]) isKindOf: C)"));
            Assert::AreEqual(std::string("(= g  (ScriptID 204) )"), UnwrapGroupedExpressions("(= g ((ScriptID 204)))"));
            for (const char *same : { "((ScriptID 1 2) x:)", "(foo (a))", "{((a))} ; ((b))\n", "\"((c))\"",
                "(cond ((b doit:)) (else (= a 1)))", "(switch a ((b)) (else 2))" })
            {
                Assert::AreEqual(std::string(same), UnwrapGroupedExpressions(same));
            }
            // The init and the step of a for are syntax; a group in its body
            // is one expression.
            Assert::AreEqual(std::string("(for ((= i 0)) (< i 9) ((++ i)) (= a  (b new:) ))"),
                UnwrapGroupedExpressions("(for ((= i 0)) (< i 9) ((++ i)) (= a ((b new:))))"));
            // A statement list as a value becomes a call of the block marker;
            // a cond clause and the init of a for stay.
            Assert::AreEqual("(or a (" + std::string(BlockMarker) + " (= b 1) (c)))", UnwrapGroupedExpressions("(or a ((= b 1) (c)))"));
            Assert::AreEqual(std::string("(cond ((a) (b)))"), UnwrapGroupedExpressions("(cond ((a) (b)))"));
            Assert::AreEqual(std::string("(for ((= i 0) (= j 0)) (< i 9) ((++ i)) (b))"), UnwrapGroupedExpressions("(for ((= i 0) (= j 0)) (< i 9) ((++ i)) (b))"));
        }

        // A class that the two texts name differently pairs by its methods,
        // also in the compare of two texts.
        TEST_METHOD(CompareScriptTexts_ClassesWithOtherNamesPair)
        {
            auto instance = [](const std::string &name)
            {
                return "(instance " + name + " of Obj\r\n\t(method (doit)\r\n\t\t(while a (-- a))\r\n\t)\r\n)\r\n";
            };
            std::string expected = MakeScript(16, { { "p", 0 } }, Procedure("p", "(= a 1)") + instance("One"));
            std::string actual = MakeScript(16, { { "p", 0 } }, Procedure("p", "(= a 1)") + instance("obj_1"));
            std::string detail;
            std::vector<std::string> differences = CompareScriptTexts(expected, actual, sciVersion1_1, &detail);
            std::string text;
            for (const std::string &d : differences)
            {
                text += d + "\n";
            }
            Assert::IsTrue(differences.empty(), Wide(text).c_str());
        }

        // A Snuffer script with grouped expressions parses (with no error),
        // so its functions get a verdict.
        TEST_METHOD(CompareScriptFolders_SnufferGroupsParse)
        {
            CompareFolders folders;
            folders.Write("expected", "a.sc", MakeScript(14, { { "p", 0 } }, Procedure("p", "(= a ((b new:))) (for ((= a 0)) (< a 10) ((++ a)) (= b a))")));
            folders.Write("expected", "b.sc", MakeScript(15, { { "q", 0 } }, Procedure("q", "(or a ((= b 1) (c)))")));
            folders.Write("actual", "b.sc", MakeScript(15, { { "q", 0 } }, Procedure("q", "(if (not a) (= b 1) (c))")));
            folders.Write("actual", "a.sc", MakeScript(14, { { "p", 0 } }, Procedure("p", "(= a (b new:)) (for ((= a 0)) (< a 10) ((++ a)) (= b a))")));
            FolderCompareResult result = CompareScriptFolders(folders.Folder("expected"), folders.Folder("actual"), "", sciVersion1_1);
            std::string text = RowsText(result);
            Assert::IsTrue(result.errors.empty(), Wide(text).c_str());
            std::map<std::string, FunctionCompareRow> rows = RowsOf(result);
            Assert::AreEqual(std::string("SAME"), std::string(StructureVerdictName(rows["14:p"].verdict)), Wide(text).c_str());
            // A statement list as a value parses, so the function has a
            // verdict (another shape than the if of the actual side).
            Assert::IsTrue(rows.count("15:q") > 0, Wide(text).c_str());
            Assert::AreNotEqual(std::string("UNPARSED"), std::string(StructureVerdictName(rows["15:q"].verdict)), Wide(text).c_str());
        }

        // Methods pair by the name of their class: a class that the actual
        // side does not have shifts nothing. Classes that the two sides name
        // differently pair in their order.
        TEST_METHOD(CompareScriptFolders_MethodsPairByClassName)
        {
            auto instance = [](const std::string &name, const std::string &body)
            {
                return "(instance " + name + " of Obj\r\n\t(method (doit)\r\n\t\t" + body + "\r\n\t)\r\n)\r\n";
            };
            CompareFolders folders;
            folders.Write("expected", "a.sc", MakeScript(15, { { "p", 0 } }, Procedure("p", "(= a 1)") +
                instance("One", "(= a 1)") + instance("Two", "(= a 2)") + instance("Three", "(if a (= b 3))") + instance("Named", "(while a (-- a))")));
            folders.Write("actual", "a.sc", MakeScript(15, { { "p", 0 } }, Procedure("p", "(= a 1)") +
                instance("One", "(= a 1)") + instance("Three", "(if a (= b 3))") + instance("obj_4", "(while a (-- a))")));
            FolderCompareResult result = CompareScriptFolders(folders.Folder("expected"), folders.Folder("actual"), "", sciVersion1_1);
            std::string text = RowsText(result);
            Assert::IsTrue(result.errors.empty(), Wide(text).c_str());
            std::map<std::string, FunctionCompareRow> rows = RowsOf(result);
            Assert::AreEqual(std::string("SAME"), std::string(StructureVerdictName(rows["15:Three::doit"].verdict)), Wide(text).c_str());
            Assert::AreEqual(std::string("ONLY-EXPECTED"), std::string(StructureVerdictName(rows["15:Two::doit"].verdict)), Wide(text).c_str());
            Assert::AreEqual(std::string("SAME"), std::string(StructureVerdictName(rows["15:obj_4::doit"].verdict)), Wide(text).c_str());
        }
    };
}
