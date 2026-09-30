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
#include "StructuralCompare.h"
#include "ScriptOMAll.h"
#include "AstRewrite.h"
#include "DecompilerAstPasses.h"
#include "Operators.h"
#include "SCISourceCodeFormatter.h"
#include "CompileContext.h"
#include "CrystalScriptStream.h"
#include "ScriptText.h"
#include "SyntaxParser.h"
#include "format.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <map>
#include <set>

using namespace sci;
using namespace std;

// True when the statements hold a continue of this loop (not of a nested
// loop). A for loop's continue runs the step; a while loop's does not, so
// the two shapes are not the same when the body has one.
static bool ContainsLoopContinue(const SyntaxNodeVector &list)
{
    for (const unique_ptr<SyntaxNode> &s : list)
    {
        if (!s)
        {
            continue;
        }
        switch (s->GetNodeType())
        {
            case NodeTypeContinue:
                return true;
            case NodeTypeCodeBlock:
                if (ContainsLoopContinue(static_cast<CodeBlock *>(s.get())->GetStatements()))
                {
                    return true;
                }
                break;
            case NodeTypeIf:
            {
                IfStatement *if_ = static_cast<IfStatement *>(s.get());
                for (SyntaxNode *branch : { if_->GetStatement1(), if_->GetStatement2() })
                {
                    if (!branch)
                    {
                        continue;
                    }
                    if (branch->GetNodeType() == NodeTypeContinue)
                    {
                        return true;
                    }
                    CodeBlock *block = SafeSyntaxNode<CodeBlock>(branch);
                    if (block && ContainsLoopContinue(block->GetStatements()))
                    {
                        return true;
                    }
                }
                break;
            }
            case NodeTypeSwitch:
            {
                SwitchStatement *sw = static_cast<SwitchStatement *>(s.get());
                for (const unique_ptr<CaseStatement> &c : sw->_cases)
                {
                    if (ContainsLoopContinue(c->GetStatements()))
                    {
                        return true;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    return false;
}

namespace
{
    bool ReadWholeFile(const string &path, string &out)
    {
        ifstream file(path, ios::binary);
        if (!file)
        {
            return false;
        }
        stringstream ss;
        ss << file.rdbuf();
        out = ss.str();
        return true;
    }

    BinaryOperator SignedCompare(BinaryOperator op)
    {
        switch (op)
        {
        case BinaryOperator::UnsignedGreaterEqual: return BinaryOperator::GreaterEqual;
        case BinaryOperator::UnsignedGreaterThan: return BinaryOperator::GreaterThan;
        case BinaryOperator::UnsignedLessEqual: return BinaryOperator::LessEqual;
        case BinaryOperator::UnsignedLessThan: return BinaryOperator::LessThan;
        default: return op;
        }
    }

    // Shape: cond -> its lowered if; for -> init statements + while with the
    // step at the end of the body; an unsigned compare -> the signed one.
    class ShapeNormalizer : public AstPass
    {
    public:
        const char *Name() const override { return "ShapeNormalizer"; }
        RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
        {
            switch (slot->GetNodeType())
            {
            case NodeTypeCond:
            {
                CondStatement *cond = static_cast<CondStatement *>(slot.get());
                unique_ptr<SyntaxNode> inner = move(cond->GetStatement1Internal());
                if (!inner)
                {
                    return RewriteResult::None;
                }
                slot = move(inner);
                return RewriteResult::Replaced;
            }
            case NodeTypeForLoop:
            {
                ForLoop *forLoop = static_cast<ForLoop *>(slot.get());
                if (ContainsLoopContinue(forLoop->GetStatements()))
                {
                    return RewriteResult::None;
                }
                unique_ptr<WhileLoop> whileLoop = make_unique<WhileLoop>();
                whileLoop->SetPosition(forLoop->GetPosition());
                if (forLoop->GetCondition())
                {
                    whileLoop->SetCondition(move(const_cast<unique_ptr<ConditionalExpression> &>(forLoop->GetCondition())));
                }
                whileLoop->GetStatements() = move(forLoop->GetStatements());
                if (forLoop->_looper)
                {
                    for (unique_ptr<SyntaxNode> &step : forLoop->_looper->GetStatements())
                    {
                        whileLoop->GetStatements().push_back(move(step));
                    }
                }
                // A code block prints as its statements, so the init statements
                // and the loop read as siblings.
                unique_ptr<CodeBlock> block = make_unique<CodeBlock>();
                if (forLoop->GetInitializer())
                {
                    for (unique_ptr<SyntaxNode> &init : forLoop->GetInitializer()->GetStatements())
                    {
                        block->GetStatements().push_back(move(init));
                    }
                }
                block->GetStatements().push_back(move(whileLoop));
                slot = move(block);
                return RewriteResult::Replaced;
            }
            case NodeTypeBinaryOperation:
            {
                BinaryOp *op = static_cast<BinaryOp *>(slot.get());
                BinaryOperator mapped = SignedCompare(op->Operator);
                if (mapped != op->Operator)
                {
                    op->Operator = mapped;
                    return RewriteResult::Changed;
                }
                return RewriteResult::None;
            }
            case NodeTypeNaryOperation:
            {
                // The parser reads every comparison as an n-ary operation.
                NaryOp *op = static_cast<NaryOp *>(slot.get());
                BinaryOperator mapped = SignedCompare(op->Operator);
                if (mapped != op->Operator)
                {
                    op->Operator = mapped;
                    return RewriteResult::Changed;
                }
                return RewriteResult::None;
            }
            default:
                return RewriteResult::None;
            }
        }
    };

    void NormalizeValue(PropertyValueBase *value)
    {
        switch (value->GetType())
        {
        case ValueType::String:
        case ValueType::ResourceString:
            value->SetValue("s", ValueType::String);
            break;
        case ValueType::Said:
            value->SetValue("s", ValueType::Said);
            break;
        case ValueType::Pointer:
            value->SetValue("v", ValueType::Pointer);
            break;
        default:
            value->SetValue("v", ValueType::Token);
            break;
        }
    }

    void NormalizeSend(SendCall *send);

    // The walker does not enter an indexer, so an indexer's own values are
    // handled here, by node type.
    void NormalizeTree(SyntaxNode *node)
    {
        if (!node)
        {
            return;
        }
        switch (node->GetNodeType())
        {
        case NodeTypeValue:
        case NodeTypeComplexValue:
        {
            PropertyValueBase *value = static_cast<PropertyValueBase *>(node);
            if (node->GetNodeType() == NodeTypeComplexValue)
            {
                NormalizeTree(static_cast<ComplexPropertyValue *>(node)->GetIndexer());
            }
            NormalizeValue(value);
            break;
        }
        case NodeTypeLValue:
        {
            LValue *lvalue = static_cast<LValue *>(node);
            lvalue->SetName("v");
            NormalizeTree(const_cast<SyntaxNode *>(lvalue->GetIndexer()));
            break;
        }
        case NodeTypeBinaryOperation:
        {
            BinaryOp *op = static_cast<BinaryOp *>(node);
            op->Operator = SignedCompare(op->Operator);
            NormalizeTree(op->GetStatement1Internal().get());
            NormalizeTree(op->GetStatement2Internal().get());
            break;
        }
        case NodeTypeUnaryOperation:
            NormalizeTree(static_cast<UnaryOp *>(node)->GetStatement1Internal().get());
            break;
        case NodeTypeNaryOperation:
        {
            NaryOp *op = static_cast<NaryOp *>(node);
            op->Operator = SignedCompare(op->Operator);
            for (unique_ptr<SyntaxNode> &operand : op->GetStatements())
            {
                NormalizeTree(operand.get());
            }
            break;
        }
        case NodeTypeSendCall:
        {
            SendCall *send = static_cast<SendCall *>(node);
            NormalizeSend(send);
            NormalizeTree(send->GetStatement1Internal().get());
            for (const unique_ptr<SendParam> &param : send->GetParams())
            {
                for (unique_ptr<SyntaxNode> &arg : param->GetStatements())
                {
                    NormalizeTree(arg.get());
                }
            }
            break;
        }
        case NodeTypeProcedureCall:
        {
            ProcedureCall *call = static_cast<ProcedureCall *>(node);
            call->SetName("call");
            for (unique_ptr<SyntaxNode> &arg : call->GetStatements())
            {
                NormalizeTree(arg.get());
            }
            break;
        }
        case NodeTypeAssignment:
        {
            Assignment *assign = static_cast<Assignment *>(node);
            NormalizeTree(assign->_variable.get());
            NormalizeTree(assign->GetStatement1Internal().get());
            break;
        }
        default:
            break;
        }
    }

    void NormalizeSend(SendCall *send)
    {
        string target = send->GetTargetName();
        if (!target.empty() && (target != "self") && (target != "super"))
        {
            send->SetName("v");
        }
        if (send->_object3)
        {
            NormalizeTree(send->_object3.get());
        }
        for (const unique_ptr<SendParam> &param : send->GetParams())
        {
            param->SetIsMethod(true);
        }
    }

    // Names: every value, variable, define and literal becomes one token; a
    // send target (other than self and super) too; a procedure or kernel call
    // name becomes "call"; a send param keeps its selector name, as a method.
    class NamesNormalizer : public AstPass
    {
    public:
        const char *Name() const override { return "NamesNormalizer"; }
        RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
        {
            switch (slot->GetNodeType())
            {
            case NodeTypeValue:
            case NodeTypeComplexValue:
            case NodeTypeAssignment:
            case NodeTypeSendCall:
            case NodeTypeProcedureCall:
                NormalizeTree(slot.get());
                return RewriteResult::Changed;
            default:
                return RewriteResult::None;
            }
        }
    };

    // The printed function, without its header: the text after the signature
    // list "(name params &tmp ...)" up to the closing paren of the function.
    string FunctionBodyText(Script &script, FunctionBase &func, bool isMethod);

    // The function after its passes: rawText before any pass, exactText
    // after the shape passes, text after the names pass, and the skeleton.
    // An asm body gets no pass.
    void NormalizeInto(Script &script, FunctionBase &func, bool isMethod, StructuralFunction &f)
    {
        f.rawText = FunctionBodyText(script, func, isMethod);
        f.isAsm = (f.rawText.find(string("(") + AsmBlockMarker + ")") != string::npos);
        if (f.isAsm)
        {
            f.exactText = f.rawText;
            f.text = f.rawText;
            return;
        }
        ShapeNormalizer shape;
        vector<AstPass *> shapePasses = { &shape };
        RunPassesToFixpoint(func, shapePasses, 32, nullptr);
        AstPassOptions options;
        RunDecompilerAstPasses(func, options, nullptr);
        f.exactText = FunctionBodyText(script, func, isMethod);
        NamesNormalizer names;
        RunPassOnce(func, names);
        f.text = FunctionBodyText(script, func, isMethod);
        f.skeleton = StructureSkeleton(f.text);
    }

    string FunctionBodyText(Script &script, FunctionBase &func, bool isMethod)
    {
        stringstream ss;
        SourceCodeWriter writer(ss, &script);
        if (isMethod)
        {
            OutputSourceCode_SCI(static_cast<const MethodDefinition &>(func), writer);
        }
        else
        {
            OutputSourceCode_SCI(static_cast<const ProcedureDefinition &>(func), writer);
        }
        string text = ss.str();
        size_t first = text.find('(');
        size_t second = (first == string::npos) ? string::npos : text.find('(', first + 1);
        size_t headerEnd = (second == string::npos) ? string::npos : text.find(')', second + 1);
        if (headerEnd == string::npos)
        {
            return NormalizeWhitespace(text);
        }
        size_t last = text.rfind(')');
        string body = text.substr(headerEnd + 1, (last > headerEnd) ? (last - headerEnd - 1) : 0);
        return NormalizeWhitespace(body);
    }
}

set<string> UnusedProcedureNames(const string &text)
{
    // A golden line "(procedure (localproc_3 ...) ; UNUSED" is dead code the
    // decompiler never reaches, so it emits no such procedure.
    set<string> names;
    size_t pos = 0;
    while ((pos = text.find("(procedure (", pos)) != string::npos)
    {
        size_t nameStart = pos + 12;
        size_t nameEnd = text.find_first_of(" )", nameStart);
        size_t lineEnd = text.find('\n', pos);
        if ((nameEnd != string::npos) && (lineEnd != string::npos) && (text.find("; UNUSED", nameEnd) < lineEnd))
        {
            names.insert(text.substr(nameStart, nameEnd - nameStart));
        }
        pos = nameStart;
    }
    return names;
}

const char *const ClassKeyPrefix = "class:";

vector<StructuralFunction> NormalizeScriptForCompare(Script &script, const set<string> *skipProcedures)
{
    vector<StructuralFunction> functions;
    // An exported procedure is keyed by its export slot (from the public
    // block, or a proc<script>_<slot> name); the two sides order them
    // differently and name them differently. A local one is keyed by its
    // ordinal among the locals.
    map<string, int> exportSlots;
    for (auto &entry : script.GetExports())
    {
        exportSlots[entry->Name] = entry->Slot;
    }
    int localIndex = 0;
    for (auto &proc : script.GetProceduresNC())
    {
        const string &name = proc->GetName();
        if (skipProcedures && skipProcedures->count(name))
        {
            continue;
        }
        StructuralFunction f;
        auto slot = exportSlots.find(name);
        size_t underscore = name.rfind('_');
        if (exportSlots.empty())
        {
            // No public block (a unit test script): by ordinal.
            f.key = fmt::format("proc#{0}", localIndex++);
        }
        else if (slot != exportSlots.end())
        {
            f.key = fmt::format("export#{0}", slot->second);
        }
        else if ((name.compare(0, 4, "proc") == 0) && (underscore != string::npos) && (underscore + 1 < name.size()) &&
            (name.find_first_not_of("0123456789", underscore + 1) == string::npos))
        {
            f.key = "export#" + name.substr(underscore + 1);
        }
        else
        {
            f.key = fmt::format("local#{0}", localIndex++);
        }
        f.display = proc->GetName();
        NormalizeInto(script, *proc, false, f);
        functions.push_back(f);
    }
    // A method pairs by the name of its class (and the count of the classes
    // of that name before it); CompareScriptFolders pairs the classes that
    // one side names differently by their order.
    map<string, int> classNames;
    for (auto &classDef : script.GetClassesNC())
    {
        int occurrence = classNames[classDef->GetName()]++;
        for (auto &method : classDef->GetMethodsNC())
        {
            StructuralFunction f;
            f.key = fmt::format("{0}{1}#{2}::{3}", ClassKeyPrefix, classDef->GetName(), occurrence, method->GetName());
            f.display = classDef->GetName() + "::" + method->GetName();
            NormalizeInto(script, *method, true, f);
            functions.push_back(f);
        }
    }
    return functions;
}

static vector<string> CompareFunctionLists(const vector<StructuralFunction> &expected, const vector<StructuralFunction> &actual, string *outDetail)
{
    vector<string> differences;
    map<string, const StructuralFunction *> actualByKey;
    for (const StructuralFunction &f : actual)
    {
        actualByKey[f.key] = &f;
    }
    map<string, const StructuralFunction *> expectedByKey;
    for (const StructuralFunction &f : expected)
    {
        expectedByKey[f.key] = &f;
        auto it = actualByKey.find(f.key);
        if (it == actualByKey.end())
        {
            differences.push_back(f.display + " (only in expected)");
            continue;
        }
        if (f.text != it->second->text)
        {
            differences.push_back(f.display);
            if (outDetail)
            {
                *outDetail += "== " + f.display + "\n-- expected:\n" + f.text + "\n-- actual:\n" + it->second->text + "\n\n";
            }
        }
    }
    for (const StructuralFunction &f : actual)
    {
        if (expectedByKey.find(f.key) == expectedByKey.end())
        {
            differences.push_back(f.display + " (only in actual)");
        }
    }
    return differences;
}

vector<string> CompareScriptTexts(const string &expectedText, const string &actualText, SCIVersion version, string *outDetail)
{
    string error;
    unique_ptr<Script> expected = ParseScriptText(expectedText, version, &error);
    if (!expected)
    {
        return { "<unparsed> expected: " + error };
    }
    unique_ptr<Script> actual = ParseScriptText(actualText, version, &error);
    if (!actual)
    {
        return { "<unparsed> actual: " + error };
    }
    set<string> unused = UnusedProcedureNames(expectedText);
    return CompareFunctionLists(NormalizeScriptForCompare(*expected, &unused), NormalizeScriptForCompare(*actual), outDetail);
}

string StructuralCompareResult::Report() const
{
    string report = fmt::format("Common files: {0}, functions compared: {1}, differences: {2}, unparsed: {3}, only expected: {4}, only actual: {5}\n",
        commonFiles, functionsCompared, differences.size(), unparsed.size(), onlyExpected.size(), onlyActual.size());
    if (!differences.empty())
    {
        report += "\nDifferences:\n";
        for (const string &d : differences)
        {
            report += "  " + d + "\n";
        }
    }
    if (!unparsed.empty())
    {
        report += "\nUnparsed:\n";
        for (const string &u : unparsed)
        {
            report += "  " + u + "\n";
        }
    }
    if (!onlyExpected.empty())
    {
        report += "\nOnly in expected:\n";
        for (const string &f : onlyExpected)
        {
            report += "  " + f + "\n";
        }
    }
    if (!onlyActual.empty())
    {
        report += "\nOnly in actual:\n";
        for (const string &f : onlyActual)
        {
            report += "  " + f + "\n";
        }
    }
    return report;
}

static string SafeFileName(const string &name)
{
    string safe;
    for (char c : name)
    {
        safe.push_back((isalnum(static_cast<unsigned char>(c)) || (c == '_') || (c == '.') || (c == '-')) ? c : '_');
    }
    return safe;
}

StructuralCompareResult CompareStructural(const string &expectedDir, const string &actualDir, const string &outDir, SCIVersion version)
{
    StructuralCompareResult result;
    map<string, string> expectedFiles;
    map<string, string> actualFiles;
    error_code ec;
    filesystem::directory_iterator expectedEntries(expectedDir, ec);
    if (ec)
    {
        result.unparsed.push_back(expectedDir + " (expected folder): " + ec.message());
        return result;
    }
    for (const auto &entry : expectedEntries)
    {
        if (entry.is_regular_file() && (entry.path().extension() == ".sc"))
        {
            expectedFiles[entry.path().filename().string()] = entry.path().string();
        }
    }
    filesystem::directory_iterator actualEntries(actualDir, ec);
    if (ec)
    {
        result.unparsed.push_back(actualDir + " (actual folder): " + ec.message());
        return result;
    }
    for (const auto &entry : actualEntries)
    {
        if (entry.is_regular_file() && (entry.path().extension() == ".sc"))
        {
            actualFiles[entry.path().filename().string()] = entry.path().string();
        }
    }
    if (!outDir.empty())
    {
        filesystem::create_directories(outDir, ec);
    }

    for (const auto &pair : expectedFiles)
    {
        const string &name = pair.first;
        auto actualIt = actualFiles.find(name);
        if (actualIt == actualFiles.end())
        {
            result.onlyExpected.push_back(name);
            continue;
        }
        result.commonFiles++;
        string expectedText, actualText, error;
        if (!ReadWholeFile(pair.second, expectedText))
        {
            result.unparsed.push_back(name + " (expected): cannot read the file");
            continue;
        }
        if (!ReadWholeFile(actualIt->second, actualText))
        {
            result.unparsed.push_back(name + " (actual): cannot read the file");
            continue;
        }
        unique_ptr<Script> expected = ParseScriptText(expectedText, version, &error);
        if (!expected)
        {
            result.unparsed.push_back(name + " (expected): " + error);
            continue;
        }
        unique_ptr<Script> actual = ParseScriptText(actualText, version, &error);
        if (!actual)
        {
            result.unparsed.push_back(name + " (actual): " + error);
            continue;
        }
        set<string> unused = UnusedProcedureNames(expectedText);
        vector<StructuralFunction> expectedFunctions = NormalizeScriptForCompare(*expected, &unused);
        vector<StructuralFunction> actualFunctions = NormalizeScriptForCompare(*actual);
        result.functionsCompared += static_cast<int>(expectedFunctions.size());
        map<string, const StructuralFunction *> actualByKey;
        for (const StructuralFunction &f : actualFunctions)
        {
            if (!actualByKey.insert(make_pair(f.key, &f)).second)
            {
                result.differences.push_back(name + " :: " + f.display + " (duplicate key " + f.key + " in actual)");
            }
        }
        set<string> expectedKeys;
        for (const StructuralFunction &f : expectedFunctions)
        {
            if (!expectedKeys.insert(f.key).second)
            {
                result.differences.push_back(name + " :: " + f.display + " (duplicate key " + f.key + " in expected)");
                continue;
            }
            auto it = actualByKey.find(f.key);
            if (it == actualByKey.end())
            {
                result.differences.push_back(name + " :: " + f.display + " (only in expected)");
                continue;
            }
            if (f.text != it->second->text)
            {
                result.differences.push_back(name + " :: " + f.display);
                if (!outDir.empty())
                {
                    ofstream file(outDir + "\\" + SafeFileName(name + "." + f.display) + ".diff.txt", ios::binary);
                    file << "-- expected:\n" << f.text << "\n\n-- actual:\n" << it->second->text << "\n";
                }
            }
        }
    }
    for (const auto &pair : actualFiles)
    {
        if (expectedFiles.find(pair.first) == expectedFiles.end())
        {
            result.onlyActual.push_back(pair.first);
        }
    }
    if (!outDir.empty())
    {
        ofstream file(outDir + "\\_structural.txt", ios::binary);
        file << result.Report();
    }
    return result;
}

string NormalizeWhitespace(const string &text)
{
    string out;
    out.reserve(text.size());
    bool inSpace = false;
    for (char c : text)
    {
        if (c == '\r')
        {
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n')
        {
            inSpace = true;
            continue;
        }
        if (inSpace && !out.empty())
        {
            out.push_back(' ');
        }
        inSpace = false;
        out.push_back(c);
    }
    return out;
}

unique_ptr<Script> ParseScriptText(const string &text, SCIVersion version, string *outError)
{
    // A game session loads the grammar; a compare can come with no session.
    InitializeSyntaxParsers();
    ScriptText lines = SplitScriptText(text);
    CScriptStreamLimiter limiter(lines);
    CCrystalScriptStream stream(&limiter);
    unique_ptr<Script> script = make_unique<Script>();
    CompileLog log;
    if (!SyntaxParser_Parse(*script, stream, PreProcessorDefinesFromSCIVersion(version), &log))
    {
        if (outError)
        {
            string message = "Parse failed:";
            for (const CompileResult &r : log.Results())
            {
                message += "\n  " + r.GetMessage();
            }
            *outError = message;
        }
        return nullptr;
    }
    return script;
}

const char *const AsmBlockMarker = "scicCompareAsmBlock";

namespace
{
    // The index after the string that starts at start (at its opening
    // character), or the end of the text. A backslash escapes the next
    // character in each kind of string ("x\"y", {x\}y}, 'x\'y').
    size_t SkipString(const string &text, size_t start, char close)
    {
        for (size_t i = start + 1; i < text.size(); i++)
        {
            if (text[i] == '\\')
            {
                i++;
                continue;
            }
            if (text[i] == close)
            {
                return i + 1;
            }
        }
        return text.size();
    }

    // The index after a string or a comment that starts at i, or i when none
    // starts there.
    size_t SkipStringOrComment(const string &text, size_t i)
    {
        switch (text[i])
        {
        case ';':
        {
            size_t end = text.find('\n', i);
            return (end == string::npos) ? text.size() : end;
        }
        case '"':
            return SkipString(text, i, '"');
        case '\'':
            return SkipString(text, i, '\'');
        case '{':
            return SkipString(text, i, '}');
        default:
            return i;
        }
    }

    bool IsAsmBlockStart(const string &text, size_t i)
    {
        return (text[i] == '(') && (text.compare(i + 1, 3, "asm") == 0) &&
            ((i + 4 >= text.size()) || isspace(static_cast<unsigned char>(text[i + 4])) || (text[i + 4] == ')'));
    }
}

namespace
{
    // The index after the close of the balanced group that starts at i (a
    // '(' or a '['); npos when it has none.
    size_t GroupEnd(const string &text, size_t i)
    {
        int depth = 0;
        size_t j = i;
        while (j < text.size())
        {
            size_t after = SkipStringOrComment(text, j);
            if (after != j)
            {
                j = after;
                continue;
            }
            char c = text[j];
            if ((c == '(') || (c == '['))
            {
                depth++;
            }
            else if (((c == ')') || (c == ']')) && (--depth == 0))
            {
                return j + 1;
            }
            j++;
        }
        return string::npos;
    }
}

string UnwrapGroupedExpressions(const string &text)
{
    const char *const space = " \t\r\n";
    string out = text;
    for (bool changed = true; changed; )
    {
        changed = false;
        size_t i = 0;
        while (i < out.size())
        {
            size_t skipped = SkipStringOrComment(out, i);
            if (skipped != i)
            {
                i = skipped;
                continue;
            }
            if (out[i] == '(')
            {
                size_t inner = out.find_first_not_of(space, i + 1);
                if ((inner != string::npos) && ((out[inner] == '(') || (out[inner] == '[')))
                {
                    size_t innerEnd = GroupEnd(out, inner);
                    size_t close = (innerEnd != string::npos) ? out.find_first_not_of(space, innerEnd) : string::npos;
                    if ((close != string::npos) && (out[close] == ')'))
                    {
                        out[i] = ' ';
                        out[close] = ' ';
                        changed = true;
                    }
                }
            }
            i++;
        }
    }
    return out;
}

string ReplaceAsmBlocks(const string &text)
{
    string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size())
    {
        size_t skipped = SkipStringOrComment(text, i);
        if (skipped != i)
        {
            out.append(text, i, skipped - i);
            i = skipped;
            continue;
        }
        if (IsAsmBlockStart(text, i))
        {
            int depth = 0;
            size_t j = i;
            while (j < text.size())
            {
                size_t after = SkipStringOrComment(text, j);
                if (after != j)
                {
                    j = after;
                    continue;
                }
                if (text[j] == '(')
                {
                    depth++;
                }
                else if ((text[j] == ')') && (--depth == 0))
                {
                    j++;
                    break;
                }
                j++;
            }
            out += string("(") + AsmBlockMarker + ")";
            i = j;
            continue;
        }
        out.push_back(text[i]);
        i++;
    }
    return out;
}

string StructureSkeleton(const string &normalizedText)
{
    static const set<string> keywords = { "if", "else", "cond", "while", "repeat", "do", "for", "switch", "switchto",
        "break", "breakif", "continue", "contif", "return", "and", "or", "not" };
    struct Open
    {
        bool emitted;
        bool isSwitch;      // its paren children are cases
        bool valueFirst;    // a switch: its first child is the value
        int children;
    };
    vector<Open> stack;
    string out;
    const string &text = normalizedText;
    size_t i = 0;
    auto readWord = [&](size_t start) -> size_t
    {
        size_t end = start;
        while ((end < text.size()) && !isspace(static_cast<unsigned char>(text[end])) && (text[end] != '(') && (text[end] != ')'))
        {
            end++;
        }
        return end;
    };
    auto emit = [&](const string &token)
    {
        if (!out.empty() && (token != ")"))
        {
            out.push_back(' ');
        }
        out += token;
    };
    while (i < text.size())
    {
        char c = text[i];
        if (isspace(static_cast<unsigned char>(c)))
        {
            i++;
            continue;
        }
        // The child count of the paren around this token.
        bool isCase = false;
        if (!stack.empty())
        {
            Open &parent = stack.back();
            parent.children++;
            isCase = parent.isSwitch && !(parent.valueFirst && (parent.children == 1));
        }
        if (c == '(')
        {
            size_t headStart = i + 1;
            while ((headStart < text.size()) && isspace(static_cast<unsigned char>(text[headStart])))
            {
                headStart++;
            }
            size_t headEnd = readWord(headStart);
            string head = text.substr(headStart, headEnd - headStart);
            Open open = { false, false, false, 0 };
            if (keywords.count(head))
            {
                emit("(" + head);
                open.emitted = true;
                open.isSwitch = (head == "switch") || (head == "switchto");
                open.valueFirst = (head == "switch");
                // The head is not a child.
                i = headEnd;
            }
            else
            {
                if (isCase)
                {
                    emit("(case");
                    open.emitted = true;
                }
                i++;
            }
            stack.push_back(open);
            continue;
        }
        if (c == ')')
        {
            if (!stack.empty())
            {
                // The ")" ends its paren; it is not a child of it.
                if (stack.back().emitted)
                {
                    emit(")");
                }
                stack.pop_back();
            }
            i++;
            continue;
        }
        size_t end = readWord(i);
        string word = text.substr(i, end - i);
        if (word == "else")
        {
            emit("else");
        }
        i = end;
    }
    return out;
}
const char *StructureVerdictName(StructureVerdict verdict)
{
    switch (verdict)
    {
    case StructureVerdict::Same: return "SAME";
    case StructureVerdict::Names: return "NAMES";
    case StructureVerdict::Shape: return "SHAPE";
    case StructureVerdict::Diff: return "DIFF";
    case StructureVerdict::Asm: return "ASM";
    case StructureVerdict::Source: return "SOURCE";
    case StructureVerdict::BothAsm: return "BOTH-ASM";
    case StructureVerdict::OnlyExpected: return "ONLY-EXPECTED";
    case StructureVerdict::OnlyActual: return "ONLY-ACTUAL";
    case StructureVerdict::Neither: return "NEITHER";
    default: return "UNPARSED";
    }
}

const char *StructureChangeName(StructureChange change)
{
    switch (change)
    {
    case StructureChange::Fixed: return "FIXED";
    case StructureChange::Changed: return "CHANGED";
    case StructureChange::Regressed: return "REGRESSED";
    case StructureChange::Added: return "ADDED";
    case StructureChange::Removed: return "REMOVED";
    default: return "";
    }
}

namespace
{
    typedef map<uint16_t, vector<StructuralFunction>> ScriptFunctions;

    // Reads each .sc file of the folder into scripts, by the number of its
    // (script# N) line. A script that cannot be read or parsed, or whose
    // number two files have, goes into failed, with its error.
    void ReadScriptFolder(const string &dir, const char *side, SCIVersion version, ScriptFunctions &scripts, set<uint16_t> &failed, vector<string> &errors, const set<uint16_t> *onlyScripts)
    {
        error_code ec;
        filesystem::directory_iterator entries(dir, ec);
        if (ec)
        {
            errors.push_back(dir + " (" + side + "): " + ec.message());
            return;
        }
        static const regex scriptNumber(R"(\(script#\s*(\d+)\s*\))");
        map<uint16_t, string> fileOf;
        for (const auto &entry : entries)
        {
            error_code fileError;
            if (!entry.is_regular_file(fileError) || (_stricmp(entry.path().extension().string().c_str(), ".sc") != 0))
            {
                continue;
            }
            string name = entry.path().filename().string();
            string text;
            if (!ReadWholeFile(entry.path().string(), text))
            {
                errors.push_back(name + " (" + side + "): cannot read the file");
                continue;
            }
            smatch match;
            if (!regex_search(text, match, scriptNumber))
            {
                errors.push_back(name + " (" + side + "): no (script# N) line");
                continue;
            }
            unsigned long number = (match[1].length() <= 5) ? stoul(match[1].str()) : 0x10000;
            if (number > 0xffff)
            {
                errors.push_back(name + " (" + side + "): the script number is too big");
                continue;
            }
            uint16_t script = static_cast<uint16_t>(number);
            if (onlyScripts && !onlyScripts->count(script))
            {
                continue;
            }
            auto other = fileOf.find(script);
            if (other != fileOf.end())
            {
                errors.push_back(fmt::format("{0} ({1}): script {2} is also in {3}", name, side, script, other->second));
                failed.insert(script);
                scripts.erase(script);
                continue;
            }
            fileOf[script] = name;
            string error;
            string asmReplaced = ReplaceAsmBlocks(text);
            unique_ptr<Script> parsed = ParseScriptText(asmReplaced, version, &error);
            if (!parsed)
            {
                // Snuffer's grouped expressions; the error of the text as it
                // is stays when this does not parse either.
                parsed = ParseScriptText(UnwrapGroupedExpressions(asmReplaced), version, nullptr);
            }
            if (!parsed)
            {
                errors.push_back(name + " (" + side + "): " + error);
                failed.insert(script);
                continue;
            }
            set<string> unused = UnusedProcedureNames(text);
            scripts[script] = NormalizeScriptForCompare(*parsed, &unused);
        }
    }

    const StructuralFunction *FindFunction(const vector<StructuralFunction> *functions, const string &key)
    {
        if (functions)
        {
            for (const StructuralFunction &f : *functions)
            {
                if (f.key == key)
                {
                    return &f;
                }
            }
        }
        return nullptr;
    }

    const vector<StructuralFunction> *FunctionsOf(const ScriptFunctions &scripts, uint16_t script)
    {
        auto it = scripts.find(script);
        return (it == scripts.end()) ? nullptr : &it->second;
    }

    bool IsLocalProcedure(const StructuralFunction &f)
    {
        return f.key.compare(0, 6, "local#") == 0;
    }

    // How well two bodies pair: 3 for the same text, 2 for the same
    // skeleton, else 1.
    int PairScore(const StructuralFunction &a, const StructuralFunction &b)
    {
        if (a.isAsm || b.isAsm)
        {
            return (a.isAsm && b.isAsm && (a.rawText == b.rawText)) ? 3 : 1;
        }
        return (a.text == b.text) ? 3 : ((a.skeleton == b.skeleton) ? 2 : 1);
    }

    // The class of a method key ("class:Name#0"), or empty for a key of a
    // procedure.
    string ClassOfKey(const string &key)
    {
        if (key.compare(0, strlen(ClassKeyPrefix), ClassKeyPrefix) != 0)
        {
            return string();
        }
        size_t colons = key.find("::");
        return (colons == string::npos) ? key : key.substr(0, colons);
    }

    // The classes that one side names and the other does not pair in their
    // order: the methods of side get the class of the expected one.
    void PairUnnamedClasses(const vector<StructuralFunction> &expected, vector<StructuralFunction> &side)
    {
        auto classesOf = [](const vector<StructuralFunction> &functions)
        {
            vector<string> classes;
            for (const StructuralFunction &f : functions)
            {
                string c = ClassOfKey(f.key);
                if (!c.empty() && (std::find(classes.begin(), classes.end(), c) == classes.end()))
                {
                    classes.push_back(c);
                }
            }
            return classes;
        };
        vector<string> expectedClasses = classesOf(expected);
        vector<string> sideClasses = classesOf(side);
        vector<string> expectedOnly;
        vector<string> sideOnly;
        for (const string &c : expectedClasses)
        {
            if (std::find(sideClasses.begin(), sideClasses.end(), c) == sideClasses.end())
            {
                expectedOnly.push_back(c);
            }
        }
        for (const string &c : sideClasses)
        {
            if (std::find(expectedClasses.begin(), expectedClasses.end(), c) == expectedClasses.end())
            {
                sideOnly.push_back(c);
            }
        }
        // How well two classes pair: the PairScore of their methods of the
        // same name; 0 when they have no method name in common.
        auto score = [&](const string &e, const string &s)
        {
            int total = 0;
            for (const StructuralFunction &fe : expected)
            {
                if (ClassOfKey(fe.key) != e)
                {
                    continue;
                }
                for (const StructuralFunction &fs : side)
                {
                    if ((ClassOfKey(fs.key) == s) && (fs.key.substr(s.size()) == fe.key.substr(e.size())))
                    {
                        total += PairScore(fe, fs);
                    }
                }
            }
            return total;
        };
        // The alignment in order that pairs the best classes.
        size_t n = expectedOnly.size();
        size_t m = sideOnly.size();
        vector<vector<int>> best(n + 1, vector<int>(m + 1, 0));
        for (size_t i = 1; i <= n; i++)
        {
            for (size_t j = 1; j <= m; j++)
            {
                best[i][j] = (std::max)({ best[i - 1][j], best[i][j - 1], best[i - 1][j - 1] + score(expectedOnly[i - 1], sideOnly[j - 1]) });
            }
        }
        map<string, string> renamed;
        size_t i = n;
        size_t j = m;
        while ((i > 0) && (j > 0))
        {
            int pair = score(expectedOnly[i - 1], sideOnly[j - 1]);
            if ((pair > 0) && (best[i][j] == best[i - 1][j - 1] + pair))
            {
                renamed[sideOnly[j - 1]] = expectedOnly[i - 1];
                i--;
                j--;
            }
            else if (best[i][j] == best[i - 1][j])
            {
                i--;
            }
            else
            {
                j--;
            }
        }
        for (StructuralFunction &f : side)
        {
            auto found = renamed.find(ClassOfKey(f.key));
            if (found != renamed.end())
            {
                f.key = found->second + f.key.substr(found->first.size());
            }
        }
    }

    // The local procedures have no name that two tools share, so they pair
    // in order. One tool can have a procedure more (dead code), so each
    // local procedure of side gets the key of the expected one that it
    // pairs with in the alignment that pairs the best bodies (PairScore);
    // one with no pair gets a key that the expected side does not have.
    void AlignLocalProcedures(const vector<StructuralFunction> &expected, vector<StructuralFunction> &side)
    {
        vector<size_t> e;
        vector<size_t> a;
        for (size_t i = 0; i < expected.size(); i++)
        {
            if (IsLocalProcedure(expected[i]))
            {
                e.push_back(i);
            }
        }
        for (size_t j = 0; j < side.size(); j++)
        {
            if (IsLocalProcedure(side[j]))
            {
                a.push_back(j);
            }
        }
        size_t n = e.size();
        size_t m = a.size();
        vector<vector<int>> best(n + 1, vector<int>(m + 1, 0));
        for (size_t i = 1; i <= n; i++)
        {
            for (size_t j = 1; j <= m; j++)
            {
                best[i][j] = (std::max)({ best[i - 1][j], best[i][j - 1], best[i - 1][j - 1] + PairScore(expected[e[i - 1]], side[a[j - 1]]) });
            }
        }
        vector<string> keys(m);
        for (size_t j = 0; j < m; j++)
        {
            keys[j] = fmt::format("local#unpaired{0}", j);
        }
        size_t i = n;
        size_t j = m;
        while ((i > 0) && (j > 0))
        {
            if (best[i][j] == best[i - 1][j - 1] + PairScore(expected[e[i - 1]], side[a[j - 1]]))
            {
                keys[j - 1] = expected[e[i - 1]].key;
                i--;
                j--;
            }
            else if (best[i][j] == best[i - 1][j])
            {
                i--;
            }
            else
            {
                j--;
            }
        }
        for (size_t k = 0; k < m; k++)
        {
            side[a[k]].key = keys[k];
        }
    }

    // A baseline comes from the same tool as the actual side: its local
    // procedures pair with those of the same name.
    void MatchLocalProceduresByName(const vector<StructuralFunction> &actual, vector<StructuralFunction> &baseline)
    {
        for (StructuralFunction &f : baseline)
        {
            if (IsLocalProcedure(f))
            {
                string key = "local#baseline:" + f.display;
                for (const StructuralFunction &other : actual)
                {
                    if (IsLocalProcedure(other) && (other.display == f.display))
                    {
                        key = other.key;
                        break;
                    }
                }
                f.key = key;
            }
        }
    }

    StructureVerdict VerdictOf(const StructuralFunction *expected, const StructuralFunction *actual)
    {
        if (!actual)
        {
            return expected ? StructureVerdict::OnlyExpected : StructureVerdict::Neither;
        }
        if (!expected)
        {
            return StructureVerdict::OnlyActual;
        }
        if (expected->isAsm || actual->isAsm)
        {
            return (expected->isAsm && actual->isAsm) ? StructureVerdict::BothAsm : (actual->isAsm ? StructureVerdict::Asm : StructureVerdict::Source);
        }
        if (expected->exactText == actual->exactText)
        {
            return StructureVerdict::Same;
        }
        if (expected->text == actual->text)
        {
            return StructureVerdict::Names;
        }
        return (expected->skeleton == actual->skeleton) ? StructureVerdict::Shape : StructureVerdict::Diff;
    }

    StructureChange ChangeOf(const StructuralFunction *baseline, const StructuralFunction *actual)
    {
        if (!baseline)
        {
            return actual ? StructureChange::Added : StructureChange::None;
        }
        if (!actual)
        {
            return StructureChange::Removed;
        }
        if (baseline->isAsm || actual->isAsm)
        {
            return (baseline->isAsm && actual->isAsm) ? StructureChange::None : (actual->isAsm ? StructureChange::Regressed : StructureChange::Fixed);
        }
        return (baseline->rawText == actual->rawText) ? StructureChange::None : StructureChange::Changed;
    }
}

FolderCompareResult CompareScriptFolders(const string &expectedDir, const string &actualDir, const string &baselineDir, SCIVersion version, const set<uint16_t> *onlyScripts)
{
    FolderCompareResult result;
    ScriptFunctions expected;
    ScriptFunctions actual;
    ScriptFunctions baseline;
    set<uint16_t> expectedFailed;
    set<uint16_t> actualFailed;
    set<uint16_t> baselineFailed;
    ReadScriptFolder(expectedDir, "expected", version, expected, expectedFailed, result.errors, onlyScripts);
    ReadScriptFolder(actualDir, "actual", version, actual, actualFailed, result.errors, onlyScripts);
    bool hasBaseline = !baselineDir.empty();
    if (hasBaseline)
    {
        ReadScriptFolder(baselineDir, "baseline", version, baseline, baselineFailed, result.errors, onlyScripts);
    }
    // The functions of a script that failed are not known.
    for (auto side : { make_pair(&expected, &expectedFailed), make_pair(&actual, &actualFailed), make_pair(&baseline, &baselineFailed) })
    {
        for (uint16_t number : *side.second)
        {
            side.first->erase(number);
        }
    }

    // The local procedures pair after the order of their bodies (the
    // actual side names them by offset; the baseline side as the actual).
    for (auto &script : actual)
    {
        auto expectedScript = expected.find(script.first);
        auto baselineScript = baseline.find(script.first);
        if (expectedScript != expected.end())
        {
            AlignLocalProcedures(expectedScript->second, script.second);
            PairUnnamedClasses(expectedScript->second, script.second);
        }
        if (baselineScript != baseline.end())
        {
            MatchLocalProceduresByName(script.second, baselineScript->second);
            PairUnnamedClasses(script.second, baselineScript->second);
        }
    }

    set<uint16_t> numbers;
    for (const ScriptFunctions *side : { &expected, &actual, &baseline })
    {
        for (const auto &script : *side)
        {
            numbers.insert(script.first);
        }
    }
    for (const set<uint16_t> *side : { &expectedFailed, &baselineFailed })
    {
        numbers.insert(side->begin(), side->end());
    }
    for (uint16_t number : numbers)
    {
        // A script of scic that does not parse has no rows (it is an error).
        if (actualFailed.count(number))
        {
            continue;
        }
        bool expectedUnparsed = (expectedFailed.count(number) != 0);
        bool baselineUnparsed = (baselineFailed.count(number) != 0);
        const vector<StructuralFunction> *expectedFunctions = FunctionsOf(expected, number);
        const vector<StructuralFunction> *actualFunctions = FunctionsOf(actual, number);
        const vector<StructuralFunction> *baselineFunctions = FunctionsOf(baseline, number);
        // The keys: those of the actual side, then those that only the
        // expected side has, then those that only the baseline has. A key
        // that a side has twice counts once.
        vector<pair<string, string>> keys;
        set<string> seen;
        for (const vector<StructuralFunction> *side : { actualFunctions, expectedFunctions, baselineFunctions })
        {
            if (side)
            {
                for (const StructuralFunction &f : *side)
                {
                    if (seen.insert(f.key).second)
                    {
                        keys.emplace_back(f.key, f.display);
                    }
                }
            }
        }
        for (const auto &key : keys)
        {
            const StructuralFunction *expectedFunction = FindFunction(expectedFunctions, key.first);
            const StructuralFunction *actualFunction = FindFunction(actualFunctions, key.first);
            FunctionCompareRow row;
            row.script = number;
            row.key = key.first;
            row.display = key.second;
            row.verdict = expectedUnparsed ? StructureVerdict::Unparsed : VerdictOf(expectedFunction, actualFunction);
            if (hasBaseline)
            {
                const StructuralFunction *baselineFunction = FindFunction(baselineFunctions, key.first);
                row.hasBaseline = true;
                if (baselineUnparsed)
                {
                    row.baselineVerdict = StructureVerdict::Unparsed;
                    row.change = StructureChange::None;
                }
                else
                {
                    row.baselineVerdict = expectedUnparsed ? StructureVerdict::Unparsed : VerdictOf(expectedFunction, baselineFunction);
                    row.change = ChangeOf(baselineFunction, actualFunction);
                }
            }
            result.rows.push_back(row);
        }
    }
    return result;
}
