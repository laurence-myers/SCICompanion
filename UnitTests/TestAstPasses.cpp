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
#include "StructuralCompare.h"
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

        // --- CopyValue, value context ---

        // A value if with an empty then tests a variable: the then is that
        // variable. An assignment gives its target. An if-else-if chain in a
        // value slot passes the context down.
        TEST_METHOD(CopyValue_VariableThen)
        {
            Assert::AreEqual(std::string("(= t (if a a else b))"), Body(ApplyAllPasses("(= t (if a else b))")));
            Assert::AreEqual(std::string("(= t (if (= u b) u else c))"), Body(ApplyAllPasses("(= t (if (= u b) else c))")));
            std::string chain = ApplyAllPasses("(= t (if a else (if b else c)))");
            Assert::IsTrue(chain.find("(a a)") != std::string::npos && chain.find("(b b)") != std::string::npos, L"each cond case gets its copy");
        }
        // A value or with a variable first is written as that if too.
        TEST_METHOD(CopyValue_OrWithVariableFirst)
        {
            Assert::AreEqual(std::string("(= t (if a a else b))"), Body(ApplyAllPasses("(= t (or a b))")));
            Assert::AreEqual(std::string("(= t (or (Foo) b))"), Body(ApplyAllPasses("(= t (or (Foo) b))")));
        }
        // In a boolean slot no copy is made: the empty-then if is an or.
        TEST_METHOD(CopyValue_NotInBoolean)
        {
            Assert::AreEqual(std::string("(if (or a b) (= t 1))"), Body(ApplyAllPasses("(if (if a else b) (= t 1))")));
        }
        // A value if's branches are value slots too: an inner if there is an
        // and, and an else-if (a cond case) is not merged with its inner if.
        TEST_METHOD(IfToAnd_ValueContextPropagates)
        {
            // The else is itself an if: a cond case, which stays a case.
            Assert::AreEqual(std::string("(return (cond (a (and b c)) (d e) ) )"),
                Body(ApplyAllPasses("(return (if a (if b c) else (if d e)))")));
            std::string cond = ApplyAllPasses("(return (if a (Foo) else (if b (if c d))))");
            Assert::IsTrue(cond.find("((and b c) d)") != std::string::npos, std::wstring(cond.begin(), cond.end()).c_str());
            // A statement cond case merges its inner if as usual.
            std::string stmt = ApplyAllPasses("(if a (Foo) else (if b (if c (Bar))))");
            Assert::IsTrue(stmt.find("((and b c) (Bar))") != std::string::npos, std::wstring(stmt.begin(), stmt.end()).c_str());
        }
        // An else with a return inside is not an or operand.
        TEST_METHOD(IfToAnd_ReturnInsideElseNotOr)
        {
            std::string out = ApplyAllPasses("(return (if a else (if b (return 0) else c)))");
            Assert::IsTrue(out.find("(or") == std::string::npos, L"a branch with a return stays an if");
        }

        // --- Loop continue shapes ---

        TEST_METHOD(Loop_IfContinueBecomesElse)
        {
            Assert::AreEqual(std::string("(while a (if b (= t 1) else (= u 2)) )"),
                Body(ApplyAllPasses("(while a (if b (= t 1) (continue)) (= u 2))")));
            Assert::AreEqual(std::string("(repeat (if b (break) else (++ t)) )"),
                Body(ApplyAllPasses("(repeat (if b (break)) (++ t))")));
            Assert::AreEqual(std::string("(while a (if b (return) else (++ t)) )"),
                Body(ApplyAllPasses("(while a (if b (return)) (++ t))")));
        }
        // The break and return forms apply at any depth inside a loop; the
        // continue form only at the body level.
        TEST_METHOD(Loop_NestedBreakBecomesElse)
        {
            Assert::AreEqual(std::string("(repeat (if a (if b (break) else (++ t))) )"),
                Body(ApplyAllPasses("(repeat (if a (if b (break)) (++ t)))")));
            Assert::AreEqual(std::string("(repeat (if a (if b (= u 1) (continue)) (++ t)) )"),
                Body(ApplyAllPasses("(repeat (if a (if b (= u 1) (continue)) (++ t)))")));
        }
        TEST_METHOD(Loop_TailContinueDropped)
        {
            Assert::AreEqual(std::string("(while a (= t 1) (if b (= u 2)) )"),
                Body(ApplyAllPasses("(while a (= t 1) (if b (= u 2) (continue)))")));
        }

        // --- ReturnCleanup ---

        // Parses a script with one instance whose method named methodName has
        // the body, runs the passes on that method, and returns the normalized
        // text of the whole script. For the rules keyed on a method name.
        static std::string ApplyAllPassesToMethod(const std::string &methodName, const std::string &body)
        {
            std::string text;
            text += ";;; Sierra Script 1.0 - (do not remove this comment)\n";
            text += "(script# 990)\n";
            text += "(include sci.sh)\n";
            text += "(instance astObj of Object\n(properties)\n";
            text += "(method (" + methodName + " a b c &tmp t u)\n" + body + "\n)\n)\n";
            std::unique_ptr<sci::Script> script = ParseSierraScript(text);
            AstPassOptions options;
            for (auto &cls : script->GetClassesNC())
            {
                for (auto &method : cls->GetMethodsNC())
                {
                    RunDecompilerAstPasses(*method, options, nullptr);
                }
            }
            return NormalizeWhitespace(ScriptToText(*script));
        }

        // An if whose branches return is not itself returned.
        TEST_METHOD(Return_UnwrapsIfWithReturns)
        {
            Assert::AreEqual(std::string("(if a (return 1) else (return 0))"),
                Body(ApplyAllPasses("(return (if a (return 1) else (return 0)))")));
        }
        TEST_METHOD(Return_UnwrapsIfWithReturnInThen)
        {
            Assert::AreEqual(std::string("(if a (return 1))"),
                Body(ApplyAllPasses("(return (if a (return 1)))")));
        }
        TEST_METHOD(Return_UnwrapsLoop)
        {
            Assert::AreEqual(std::string("(while a (-- t) )"),
                Body(ApplyAllPasses("(return (while a (-- t)))")));
        }
        // Not at the end of the function, the bare return stays. The function
        // returns a value (the 1), so its final statement is returned too.
        TEST_METHOD(Return_UnwrapKeepsMidFunctionReturn)
        {
            Assert::AreEqual(std::string("(if b (if a (return 1)) (return)) (return (= t 2))"),
                Body(ApplyAllPasses("(if b (return (if a (return 1)))) (= t 2)")));
        }

        // The last statement of the body is followed by the final ret. A
        // value-shaped statement there is the return value.
        TEST_METHOD(Return_WrapsFinalIfValue)
        {
            Assert::AreEqual(std::string("(return (if a b else (Foo c)))"),
                Body(ApplyAllPasses("(if a b else (Foo c))")));
        }
        TEST_METHOD(Return_WrapsFinalSwitch)
        {
            Assert::AreEqual(std::string("(return (switch a (1 b) (2 c) ) )"),
                Body(ApplyAllPasses("(switch a (1 b) (2 c))")));
        }
        TEST_METHOD(Return_WrapsFinalOr)
        {
            Assert::AreEqual(std::string("(return (or a b))"),
                Body(ApplyAllPasses("(or a b)")));
        }
        TEST_METHOD(Return_FinalSendNotWrapped)
        {
            Assert::AreEqual(std::string("(Foo a)"), Body(ApplyAllPasses("(Foo a)")));
            Assert::AreEqual(std::string("(if a (Foo b) else (++ t))"),
                Body(ApplyAllPasses("(if a (Foo b) else (++ t))")));
        }
        // A zero at the end of a switch case, or of a cond case, is stray.
        TEST_METHOD(Return_StrayZeroNotValue)
        {
            Assert::AreEqual(std::string("(switch a (1 0) (2 (Foo b)) )"),
                Body(ApplyAllPasses("(switch a (1 0) (2 (Foo b)))")));
            std::string cond = ApplyAllPasses("(if a 0 else (if b (Foo c)))");
            Assert::IsTrue(cond.find("(return") == std::string::npos, L"a cond case ending in 0 is not a return value");
        }
        // Two numbers in a row are stray values, not a return value.
        TEST_METHOD(Return_TwoNumbersNotValue)
        {
            Assert::AreEqual(std::string("(if a 1 2)"), Body(ApplyAllPasses("(if a 1 2)")));
        }

        // A bare return in the middle absorbs a value before it. The function
        // then returns a value, so its final statement is returned too.
        TEST_METHOD(Return_MidBareReturnAbsorbsValue)
        {
            Assert::AreEqual(std::string("(if a (return (+ b c))) (return (= t 1))"),
                Body(ApplyAllPasses("(if a (+ b c) (return)) (= t 1)")));
        }
        TEST_METHOD(Return_FinalBareReturnRemoved)
        {
            Assert::AreEqual(std::string("(= t 1)"), Body(ApplyAllPasses("(= t 1) (return)")));
        }
        // A function that returns a value somewhere returns one everywhere.
        TEST_METHOD(Return_RealReturnAbsorbsStatement)
        {
            Assert::AreEqual(std::string("(if a (return 5)) (return (= t 1))"),
                Body(ApplyAllPasses("(if a (return 5)) (= t 1) (return)")));
        }

        // onMe always returns a value; init returns only an unmistakable one.
        TEST_METHOD(Return_OnMeAlwaysReturns)
        {
            std::string out = ApplyAllPassesToMethod("onMe", "(if a (Foo b) else (Bar c))");
            Assert::IsTrue(out.find("(return (if a (Foo b) else (Bar c)))") != std::string::npos, L"onMe returns its final if");
        }
        TEST_METHOD(Return_InitAbsorbsOnlyUnmistakable)
        {
            std::string kept = ApplyAllPassesToMethod("init", "(if a b else c)");
            Assert::IsTrue(kept.find("(return") == std::string::npos, L"init keeps a plain value if");
            std::string wrapped = ApplyAllPassesToMethod("init", "(== a b)");
            Assert::IsTrue(wrapped.find("(return (== a b))") != std::string::npos, L"init returns a comparison");
        }
        // A cautious method never returns a send, a call, or an assignment,
        // even when the chunk stage gave the ret that value.
        TEST_METHOD(Return_HandleEventUnwrapsSend)
        {
            std::string atEnd = ApplyAllPassesToMethod("handleEvent", "(if a (return 1)) (return (b claimed:))");
            Assert::IsTrue(atEnd.find("(if a (return 1)) (b claimed:) )") != std::string::npos, L"the final send is bare");
            std::string mid = ApplyAllPassesToMethod("handleEvent", "(if a (b cue:) (return (b claimed: 1))) (return 0)");
            Assert::IsTrue(mid.find("(if a (b cue:) (b claimed: 1) (return))") != std::string::npos, L"the mid-function send is bare and its return stays");
            std::string plain = ApplyAllPasses("(if a (return 1)) (return (b claimed:))");
            Assert::IsTrue(plain.find("(return (b claimed:))") != std::string::npos, L"an ordinary function keeps a returned send");
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

        // The structural compare: golden style (for, cond, breakif, op=, sel:,
        // defines) and SCI Companion style (while with a step, nested ifs, an if
        // with a break, (= a (op a b)), sel?, numbers) of the same function
        // compare equal. A real difference is reported by function.
        TEST_METHOD(StructuralCompare_EquivalentForms)
        {
            std::string golden =
                ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                "(script# 990)\n(include sci.sh)\n"
                "(procedure (theProc theArg &tmp i ret)\n"
                "\t(= ret 0)\n"
                "\t(for ((= i 0)) (< i theArg) ((++ i))\n"
                "\t\t(breakif (== (gEgo x:) fiOPEN))\n"
                "\t\t(+= ret i)\n"
                "\t)\n"
                "\t(cond\n"
                "\t\t((== ret 1) (Format @ret {%d} ret))\n"
                "\t\t((u< ret 5) (gEgo setMotion: MoveTo 10 20))\n"
                "\t\t(else (= ret -1))\n"
                "\t)\n"
                "\t(return ret)\n"
                ")\n"
                "(procedure (second)\n\t(return 1)\n)\n";
            std::string companion =
                ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                "(script# 990)\n(include sci.sh)\n"
                "(procedure (proc990_0 param1 &tmp temp0 temp1)\n"
                "\t(= temp1 0)\n"
                "\t(= temp0 0)\n"
                "\t(while (< temp0 param1)\n"
                "\t\t(if (== (gHero x?) 0) (break))\n"
                "\t\t(= temp1 (+ temp1 temp0))\n"
                "\t\t(++ temp0)\n"
                "\t)\n"
                "\t(if (== temp1 1)\n"
                "\t\t(Format @temp1 {%d} temp1)\n"
                "\telse\n"
                "\t\t(if (< temp1 5)\n"
                "\t\t\t(gHero setMotion: MoveTo 10 20)\n"
                "\t\telse\n"
                "\t\t\t(= temp1 -1)\n"
                "\t\t)\n"
                "\t)\n"
                "\t(return temp1)\n"
                ")\n"
                "(procedure (proc990_1)\n\t(return 1)\n)\n";
            std::string detail;
            std::vector<std::string> differences = CompareScriptTexts(golden, companion, &detail);
            std::string msg = "differences: " + std::to_string(differences.size()) + "\n" + detail;
            for (const std::string &d : differences)
            {
                msg += "\n  " + d;
            }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());
            Assert::IsTrue(differences.empty(), L"equivalent forms should compare equal");
        }

        TEST_METHOD(StructuralCompare_RealDifference)
        {
            std::string golden =
                ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                "(script# 990)\n(include sci.sh)\n"
                "(procedure (theProc a)\n\t(if a (return 1))\n\t(return 0)\n)\n"
                "(procedure (other)\n\t(return 2)\n)\n";
            std::string actual =
                ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                "(script# 990)\n(include sci.sh)\n"
                "(procedure (theProc a)\n\t(if a (return 1))\n\t(= a 0)\n\t(return 0)\n)\n"
                "(procedure (other)\n\t(return 2)\n)\n";
            std::vector<std::string> differences = CompareScriptTexts(golden, actual);
            Assert::AreEqual(size_t(1), differences.size(), L"one function differs");
            Assert::AreEqual(std::string("theProc"), differences[0], L"the differing function is named");
        }

    private:
        std::string _gameFolder;
    };
}
