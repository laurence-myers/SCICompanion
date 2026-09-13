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
#include "AstPassHelper.h"
#include "Helper.h"
#include "ScriptOMAll.h"
#include "AstRewrite.h"
#include "DecompilerAstPasses.h"
#include "Operators.h"
#include "SCISourceCodeFormatter.h"
#include "format.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>

using namespace sci;
using namespace std;

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

    void NormalizeFunction(FunctionBase &func)
    {
        ShapeNormalizer shape;
        vector<AstPass *> shapePasses = { &shape };
        RunPassesToFixpoint(func, shapePasses, 32, nullptr);
        AstPassOptions options;
        RunDecompilerAstPasses(func, options, nullptr);
        NamesNormalizer names;
        RunPassOnce(func, names);
    }

    // The printed function, without its header: the text after the signature
    // list "(name params &tmp ...)" up to the closing paren of the function.
    string FunctionBodyText(Script &script, FunctionBase &func, bool isMethod)
    {
        stringstream ss;
        SourceCodeWriter writer(ss, LangSyntaxSCI, &script);
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

vector<StructuralFunction> NormalizeScriptForCompare(Script &script)
{
    vector<StructuralFunction> functions;
    int procedureIndex = 0;
    for (auto &proc : script.GetProceduresNC())
    {
        NormalizeFunction(*proc);
        StructuralFunction f;
        f.key = fmt::format("proc#{0}", procedureIndex++);
        f.display = proc->GetName();
        f.text = FunctionBodyText(script, *proc, false);
        functions.push_back(f);
    }
    int classIndex = 0;
    for (auto &classDef : script.GetClassesNC())
    {
        for (auto &method : classDef->GetMethodsNC())
        {
            NormalizeFunction(*method);
            StructuralFunction f;
            f.key = fmt::format("class#{0}::{1}", classIndex, method->GetName());
            f.display = classDef->GetName() + "::" + method->GetName();
            f.text = FunctionBodyText(script, *method, true);
            functions.push_back(f);
        }
        classIndex++;
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

vector<string> CompareScriptTexts(const string &expectedText, const string &actualText, string *outDetail)
{
    string error;
    unique_ptr<Script> expected = TryParseSierraScript(expectedText, &error);
    if (!expected)
    {
        return { "<unparsed> expected: " + error };
    }
    unique_ptr<Script> actual = TryParseSierraScript(actualText, &error);
    if (!actual)
    {
        return { "<unparsed> actual: " + error };
    }
    return CompareFunctionLists(NormalizeScriptForCompare(*expected), NormalizeScriptForCompare(*actual), outDetail);
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

StructuralCompareResult CompareStructural(const string &expectedDir, const string &actualDir, const string &outDir)
{
    StructuralCompareResult result;
    map<string, string> expectedFiles;
    map<string, string> actualFiles;
    error_code ec;
    for (const auto &entry : filesystem::directory_iterator(expectedDir, ec))
    {
        if (entry.is_regular_file() && (entry.path().extension() == ".sc"))
        {
            expectedFiles[entry.path().filename().string()] = entry.path().string();
        }
    }
    for (const auto &entry : filesystem::directory_iterator(actualDir, ec))
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
        ReadWholeFile(pair.second, expectedText);
        ReadWholeFile(actualIt->second, actualText);
        unique_ptr<Script> expected = TryParseSierraScript(expectedText, &error);
        if (!expected)
        {
            result.unparsed.push_back(name + " (expected): " + error);
            continue;
        }
        unique_ptr<Script> actual = TryParseSierraScript(actualText, &error);
        if (!actual)
        {
            result.unparsed.push_back(name + " (actual): " + error);
            continue;
        }
        vector<StructuralFunction> expectedFunctions = NormalizeScriptForCompare(*expected);
        vector<StructuralFunction> actualFunctions = NormalizeScriptForCompare(*actual);
        result.functionsCompared += static_cast<int>(expectedFunctions.size());
        map<string, const StructuralFunction *> actualByKey;
        for (const StructuralFunction &f : actualFunctions)
        {
            actualByKey[f.key] = &f;
        }
        for (const StructuralFunction &f : expectedFunctions)
        {
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
