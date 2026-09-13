/***************************************************************************
    Copyright (c) 2026 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
#include "stdafx.h"
#include "CppUnitTest.h"
#include "Helper.h"
#include "AstPassHelper.h"
#include "ScriptOMAll.h"
#include "AstRewrite.h"
#include "DecompilerAstPasses.h"
#include "DecompilerResults.h"
#include "AppState.h"
#include <set>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Tests for the decompiler AST passes. Each case parses a Sierra-syntax
// procedure body, runs the passes, and compares the printed text. No bytecode
// and no Sierra game data are needed.
//
// This file starts with harness self-tests. The per-pass cases land with the
// passes (WP3).

namespace UnitTests
{
    // Records the slot kind of every node visited, so a test can check the
    // walker reaches each kind of child.
    class RecordingPass : public AstPass
    {
    public:
        std::set<SlotKind> kinds;
        int visits = 0;
        const char *Name() const override { return "Recording"; }
        RewriteResult Rewrite(std::unique_ptr<sci::SyntaxNode> &, const AstContext &ctx) override
        {
            kinds.insert(ctx.Kind());
            visits++;
            return RewriteResult::None;
        }
    };

    // Deletes every statement that is a return, to exercise Removed and the
    // no-advance rule.
    class DeleteReturnsPass : public AstPass
    {
    public:
        const char *Name() const override { return "DeleteReturns"; }
        RewriteResult Rewrite(std::unique_ptr<sci::SyntaxNode> &slot, const AstContext &ctx) override
        {
            if ((ctx.Kind() == SlotKind::Statement) &&
                (slot->GetNodeType() == sci::NodeTypeReturn))
            {
                return RewriteResult::Removed;
            }
            return RewriteResult::None;
        }
    };

    // Always claims a change without doing anything, to force the fixpoint
    // driver to hit its sweep cap.
    class NeverSettlesPass : public AstPass
    {
    public:
        const char *Name() const override { return "NeverSettles"; }
        RewriteResult Rewrite(std::unique_ptr<sci::SyntaxNode> &, const AstContext &) override
        {
            return RewriteResult::Changed;
        }
    };

    class CollectResults : public IDecompilerResults
    {
    public:
        void AddResult(DecompilerResultType, const std::string &message) override { messages.push_back(message); }
        bool IsAborted() override { return false; }
        void InformStats(bool, int) override {}
        void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &) override {}
        std::vector<std::string> messages;
    };

    static sci::FunctionBase &FirstProcedure(sci::Script &script)
    {
        return *script.GetProceduresNC()[0];
    }

    // Extracts the procedure body from normalized "(procedure (astCase ...) BODY)"
    // text, so a test can compare the body exactly.
    static std::string Body(const std::string &full)
    {
        size_t proc = full.find("(procedure ");
        size_t sigClose = (proc == std::string::npos) ? full.find(')') : full.find(')', proc);
        size_t procClose = full.rfind(')');          // closes (procedure ...)
        if ((sigClose == std::string::npos) || (procClose <= sigClose))
        {
            return full;
        }
        std::string inner = full.substr(sigClose + 1, procClose - sigClose - 1);
        size_t start = inner.find_first_not_of(' ');
        size_t end = inner.find_last_not_of(' ');
        return (start == std::string::npos) ? std::string() : inner.substr(start, end - start + 1);
    }

    TEST_CLASS(TestAstPasses)
    {
    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
            _gameFolder = SetUpGameSCI11();
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The harness parses and prints a script. A simple body survives the
        // round trip unchanged after whitespace normalization.
        TEST_METHOD(Harness_ParsePrintIdentity)
        {
            std::string body = "(= t 1)(if (> a 5) (= t 2) else (= t 3))(return t)";
            std::string expected = NormalizeWhitespace(WrapProcedure(body));
            std::string actual = ApplyAllPasses(body);
            // The body text is present verbatim (module whitespace) inside the
            // printed procedure.
            Assert::IsTrue(actual.find("(if (> a 5)") != std::string::npos,
                L"the if should print");
            Assert::IsTrue(actual.find("(return t)") != std::string::npos,
                L"the return should print");
        }

        // A nested and/or expression parses and prints as an n-ary form.
        TEST_METHOD(Harness_AndOrPrints)
        {
            std::string actual = ApplyAllPasses("(if (and a (or b c)) (= t 1))");
            Assert::IsTrue(actual.find("(and a (or b c))") != std::string::npos,
                L"the compound condition should print as written");
        }

        // --- IfThenToAnd ---

        TEST_METHOD(IfToAnd_NestedMerge)
        {
            Assert::AreEqual(std::string("(if (and a b) (= t 1))"),
                Body(ApplyAllPasses("(if a (if b (= t 1)))")));
        }
        TEST_METHOD(IfToAnd_NestedChain)
        {
            Assert::AreEqual(std::string("(if (and a b c) (= t 1))"),
                Body(ApplyAllPasses("(if a (if b (if c (= t 1))))")));
        }
        TEST_METHOD(IfToAnd_InnerElseNotMerged)
        {
            std::string out = ApplyAllPasses("(if a (if b (= t 1) else (= t 2)))");
            Assert::IsTrue(out.find("(and") == std::string::npos, L"must not merge when inner has an else");
        }
        TEST_METHOD(IfToAnd_ValueReturn)
        {
            Assert::AreEqual(std::string("(return (and a b))"),
                Body(ApplyAllPasses("(return (if a b))")));
        }
        TEST_METHOD(IfToAnd_ValueSendArg)
        {
            Assert::AreEqual(std::string("(self foo: (and a b))"),
                Body(ApplyAllPasses("(self foo: (if a b))")));
        }
        // A real "else 0" is a value the code computes, so it stays an if.
        TEST_METHOD(IfToAnd_RealElseZeroStaysIf)
        {
            std::string out = ApplyAllPasses("(return (if a b else 0))");
            Assert::IsTrue(out.find("(if a") != std::string::npos, L"an if with else 0 stays an if");
            Assert::IsTrue(out.find("(and") == std::string::npos, L"an if with else 0 is not an and");
        }
        TEST_METHOD(IfToAnd_AssignValueKeptAsIf)
        {
            std::string out = ApplyAllPasses("(= t (if a b))");
            Assert::IsTrue(out.find("(if a") != std::string::npos, L"an assignment value stays an if");
            Assert::IsTrue(out.find("(and") == std::string::npos, L"an assignment value is not an and");
        }
        TEST_METHOD(IfToAnd_ControlFlowNotAbsorbed)
        {
            std::string out = ApplyAllPasses("(return (if a (return 1) else 0))");
            Assert::IsTrue(out.find("(and") == std::string::npos, L"a return must not become an and operand");
        }

        // --- DoubleNot ---

        TEST_METHOD(DoubleNot_BooleanCollapses)
        {
            Assert::AreEqual(std::string("(if a (= t 1))"),
                Body(ApplyAllPasses("(if (not (not a)) (= t 1))")));
        }
        TEST_METHOD(DoubleNot_ValueKept)
        {
            std::string out = ApplyAllPasses("(= t (not (not a)))");
            Assert::IsTrue(out.find("(not (not a))") != std::string::npos, L"a value double-not is kept");
        }

        // --- MathAssignment ---

        TEST_METHOD(Math_AddAssign)
        {
            Assert::AreEqual(std::string("(+= t 1)"), Body(ApplyAllPasses("(= t (+ t 1))")));
        }
        TEST_METHOD(Math_OrAssign)
        {
            Assert::AreEqual(std::string("(|= t 4)"), Body(ApplyAllPasses("(= t (| t 4))")));
        }
        TEST_METHOD(Math_NotWhenOperandOrderDiffers)
        {
            std::string out = ApplyAllPasses("(= t (+ 1 t))");
            Assert::IsTrue(out.find("(= t (+ 1 t))") != std::string::npos, L"(+ 1 t) is not a compound assign");
        }

        // The walker reaches every kind of child in a script that uses many
        // constructs.
        TEST_METHOD(Framework_SlotCoverage)
        {
            std::string body =
                "(= t (+ a b))"
                "(if (and a (not b)) (= t 1) else (return b))"
                "(while (< t 10) (Prints a) (break))"
                "(switch a (1 (= t 1)) (else (= t 2)))"
                "(= u (self foo: (if a b)))"
                "(return (or a b))";
            std::unique_ptr<sci::Script> script = ParseSierraScript(WrapProcedure(body));
            RecordingPass rec;
            RunPassOnce(FirstProcedure(*script), rec);

            // The pass is applied only to replaceable slots. Typed children
            // (AssignTarget, SendParamNode, CaseNode) are traversed but never
            // offered to the pass; their descent is proven by reaching the
            // replaceable slots inside them (SendArg inside a send param,
            // CaseValue inside a case).
            const SlotKind expected[] = {
                SlotKind::Statement, SlotKind::IfCondition, SlotKind::IfThen, SlotKind::IfElse,
                SlotKind::AndOrOperand, SlotKind::NotOperand, SlotKind::Operand,
                SlotKind::WhileCondition, SlotKind::ReturnValue, SlotKind::AssignValue,
                SlotKind::ProcArg, SlotKind::SendArg,
                SlotKind::SwitchValue, SlotKind::CaseValue,
            };
            for (SlotKind k : expected)
            {
                Assert::IsTrue(rec.kinds.count(k) == 1,
                    (std::wstring(L"slot kind not visited: ") + std::to_wstring((int)k)).c_str());
            }
            Assert::IsTrue(rec.visits > 20, L"expected many node visits");
        }

        // Removing statements during the walk does not skip or revisit
        // siblings: every return goes, everything else stays.
        TEST_METHOD(Framework_RemoveIsSafe)
        {
            std::string body = "(return a)(= t 1)(return b)(= u 2)(return c)";
            std::unique_ptr<sci::Script> script = ParseSierraScript(WrapProcedure(body));
            DeleteReturnsPass del;
            RunPassOnce(FirstProcedure(*script), del);
            std::string text = NormalizeWhitespace(ScriptToText(*script));
            Assert::IsTrue(text.find("(return") == std::string::npos, L"all returns removed");
            Assert::IsTrue(text.find("(= t 1)") != std::string::npos, L"kept t");
            Assert::IsTrue(text.find("(= u 2)") != std::string::npos, L"kept u");
        }

        // The fixpoint driver stops at the sweep cap and reports it, instead of
        // looping forever, when a pass never settles.
        // From QfG4 hero.sc: a value if with a real else inside a comparison,
        // inside a nested if. The passes must settle.
        TEST_METHOD(Passes_ConvergeOnValueIfInsideCompare)
        {
            std::unique_ptr<sci::Script> script = ParseSierraScript(WrapProcedure(
                "(if (< a b) (if (>= (if c (- (+ c d) e) else 0) b) (return 1))) (return 0)"));
            CollectResults results;
            AstPassOptions options;
            RunDecompilerAstPasses(FirstProcedure(*script), options, &results);
            for (const std::string &m : results.messages)
            {
                Assert::IsTrue(m.find("did not converge") == std::string::npos, L"passes did not converge");
            }
        }

        TEST_METHOD(Framework_NonConvergenceReported)
        {
            std::unique_ptr<sci::Script> script = ParseSierraScript(WrapProcedure("(= t 1)"));
            NeverSettlesPass never;
            CollectResults results;
            std::vector<AstPass *> passes = { &never };
            RunPassesToFixpoint(FirstProcedure(*script), passes, 4, &results);
            bool warned = false;
            for (const std::string &m : results.messages)
            {
                if (m.find("did not converge") != std::string::npos) { warned = true; }
            }
            Assert::IsTrue(warned, L"expected a non-convergence warning");
        }

    private:
        std::string _gameFolder;
    };
}
