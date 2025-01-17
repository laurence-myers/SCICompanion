#include "stdafx.h"
#include "ScriptOMAll.h"
#include "StudioSyntaxParser.h"
#include "SCISyntaxParser.h"
#include "ParserActions.h"
#include "Operators.h"
#include "OperatorTables.h"
#include "format.h"
#ifdef ENABLE_FOREACH
#include "ScriptMakerHelper.h"
#endif
#ifdef ENABLE_GETPOLY
#include "Polygon.h"
#include "AppState.h"
#endif

using namespace sci;
using namespace std;

void NoCaseE(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (!match.Result())
	{
		pContext->ReportError("Expected case value or 'else' keyword.", stream);
	}
}



template<typename _It>
bool IntegerNonZeroP(const ParserSCI *pParser, SyntaxContext *pContext, _It &stream)
{
	bool fRet = IntegerExpandedP(pParser, pContext, stream);
	if (fRet)
	{
		fRet = (pContext->Integer != 0);
		if (!fRet)
		{
			pContext->ReportError("Expected non-zero integer.", stream);
		}
	}
	return fRet;
}

// Selectors, as far as I can tell, look like this:
// A-Za-z0-9_-
// But they must have at least one letter and not start with a number
template<typename _It>
bool SelectorP(const ParserSCI *pParser, SyntaxContext *pContext, _It &stream)
{
	bool fRet = false;
	std::string &str = pContext->ScratchString();
	str.clear();
	char ch = *stream;
	bool hadAlpha = !!isalpha(ch);
	if (hadAlpha || (ch == '_') || (ch == '-'))	 // First character must be a letter or _ or -
	{
		fRet = true;
		str += ch;
		ch = *(++stream);
		while (isalnum(ch) || (ch == '_') || (ch == '-'))  // Then any alphanumeric character is fine.
		{
			hadAlpha = hadAlpha || isalpha(ch);
			fRet = true;
			str += ch;
			ch = *(++stream);
		}
	}
	return fRet && hadAlpha;
}

// For better error reports:
vector<string> SCIStatementKeywords =
{
	"if",
	"asm",
	"break",
	"breakif",
	"continue",
	"contif",
	"repeat",
	"switch",
	"switchto",
	"for",
	"return",
	"cond",
	"while",
	"define",
};

vector<string> SCIKeywords =
{
#ifdef ENABLE_EXISTS
	"&exists",
#endif
#ifdef ENABLE_GETPOLY
	"&getpoly",
#endif
	"&tmp",
	"&rest",
	"&sizeof",
	"and",
	"asm",
	"break",
	"breakif",
	"class#"		// ** In classdef
	"classdef"	  // **
	"cond",
	"continue",
	"contif",
	"define",
	"else",
	"extern"		// ** For linking public procedures
	"file#"		 // ** Procedure forward declarations
	"for",
#ifdef ENABLE_FOREACH
	"foreach",
#endif
	"global"		// ** For global var declarations
	"if",
	"method",
	"methods"	   // ** Method forward declarations (also methods in classdef)
	"mod",
	"not",
	"of",
	"or",
	"procedure",
	"properties",
	"public",
	"repeat",
	"return",
	"script#",
	"selectors"	 // ** For the selector list
	"super#"		// ** In classdef
	"super",
	"switch",
	"switchto",
	"text#",
#ifdef ENABLE_VERBS
	"verbs",
#endif
	"while",
	// "scriptNumber",  // This is special, because it's a value. So don't count it as a banned keyword.
	// neg, send, case, do, default, export are also keywords, but for the Studio language.

	// **: Original Sierra constructs that are not supported yet.
};

template<typename _It, typename _TContext>
bool AlphanumPNoKeywordOrTerm(const ParserSCI *pParser, _TContext *pContext, _It &stream)
{
	bool fRet = SelectorP(pParser, pContext, stream);
	if (fRet)
	{
		char chTerm = *stream;
		fRet = (chTerm != ':') && (chTerm != '?');
		if (fRet)
		{
			std::string &str = pContext->ScratchString();
			fRet = std::find(SCIKeywords.begin(), SCIKeywords.end(), str) == SCIKeywords.end();
			if (fRet && pContext->extraKeywords)
			{
				fRet = pContext->extraKeywords->find(str) == pContext->extraKeywords->end();
			}
		}
	}
	return fRet;
}

// Same as above, but ignores extra keywords.
template<typename _It, typename _TContext>
bool AlphanumPNoKeywordOrTerm2(const ParserSCI *pParser, _TContext *pContext, _It &stream)
{
	bool fRet = SelectorP(pParser, pContext, stream);
	if (fRet)
	{
		char chTerm = *stream;
		fRet = (chTerm != ':') && (chTerm != '?');
		if (fRet)
		{
			std::string &str = pContext->ScratchString();
			fRet = std::find(SCIKeywords.begin(), SCIKeywords.end(), str) == SCIKeywords.end();
		}
	}
	return fRet;
}

template<typename _It, typename _TContext>
bool AlphanumPSendTokenOrTerm(const ParserSCI *pParser, _TContext *pContext, _It &stream)
{
	bool fRet = SelectorP(pParser, pContext, stream);
	if (fRet)
	{
		char chTerm = *stream;
		fRet = (chTerm != ':') && (chTerm != '?');
		if (fRet)
		{
			std::string &str = pContext->ScratchString();
			if (str != "send" && str != "super")
			{
				fRet = std::find(SCIKeywords.begin(), SCIKeywords.end(), str) == SCIKeywords.end();
				if (fRet && pContext->extraKeywords)
				{
					fRet = pContext->extraKeywords->find(str) == pContext->extraKeywords->end();
				}
			}
		}
	}
	return fRet;
}

// foo: or foo?, no whitespace allowed.
template<char terminator, typename _It>
bool SelectorP_Term(const ParserSCI *pParser, SyntaxContext *pContext, _It &stream)
{
	bool fRet = SelectorP(pParser, pContext, stream);
	fRet = fRet && (*(stream) == terminator);
	if (fRet)
	{
		++stream; // Skip over our terminator
	}
	return fRet;
}

template<typename _TOpType>
_TOpType SCINameToOperator(const std::string &name);

template<>
BinaryOperator SCINameToOperator<BinaryOperator>(const std::string &name)
{
	return NameToOperator(name, sciNameToBinaryOp);
}

template<>
AssignmentOperator SCINameToOperator<AssignmentOperator>(const std::string &name)
{
	return NameToOperator(name, sciNameToAssignmentOp);
}

template<>
UnaryOperator SCINameToOperator<UnaryOperator>(const std::string &name)
{
	return NameToOperator(name, sciNameToUnaryOp);
}

// Different from Studio syntax, since the operator must be followed by whitespace.
// That is, ++i is not allowed. It must be (++ i)
template<typename _It, typename _TContext, typename _CommentPolicy>
bool SCIOperatorP(const ParserBase<_TContext, _It, _CommentPolicy> *pParser, _TContext *pContext, _It &stream)
{
	bool fRet = false;
	const char *psz = pParser->_psz;
	while (*psz && (*stream == *psz))
	{
		++stream;
		++psz;
	}
	if (*psz == 0)
	{
		pContext->ScratchString() = pParser->_psz;
		return !!isspace(*stream);
	}
	else
	{
		return false;
	}
}


template<typename _T, typename _TOperator>
void SetOperatorA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->CreateSyntaxNode<_T>(stream);
		_TOperator op = SCINameToOperator<_TOperator>(pContext->ScratchString());
		assert(op != _TOperator::None);
		pContext->GetSyntaxNode<_T>()->Operator = op;
	}
}

void SetRepeatStatementA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->CreateSyntaxNode<WhileLoop>(stream);
		pContext->GetSyntaxNode<WhileLoop>()->SetPosition(stream.GetPosition());
		// A repeat loop is just a while (TRUE) loop
		// 1 = TRUE
		pContext->GetSyntaxNode<WhileLoop>()->SetCondition(make_unique<ConditionalExpression>(make_unique<PropertyValue>(1)));
	}
}

template<typename _TBreakContinue>
void FinishBreakContinueIfA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// We should have an If statement. Add the current thing to its conditional.
		pContext->GetSyntaxNode<IfStatement>()->SetCondition(make_unique<ConditionalExpression>(move(pContext->StatementPtrReturn)));
		// And its contents should be a CodeBlock with a single Break/Continue statement.
		auto codeBlock = make_unique<CodeBlock>();
		auto breakOrContinue = make_unique<_TBreakContinue>();
		breakOrContinue->SetPosition(stream.GetPosition());
		codeBlock->AddStatement(move(breakOrContinue));
		pContext->GetSyntaxNode<IfStatement>()->SetStatement1(move(codeBlock));
	}
}

template<typename _TBreakContinue>
_TBreakContinue *GetBreakContinueInIf(SyntaxContext *pContext)
{
	// Get a break inside an if then clause
	auto codeBlock = SafeSyntaxNode<CodeBlock>(pContext->GetSyntaxNode<IfStatement>()->GetStatement1());
	return SafeSyntaxNode<_TBreakContinue>(codeBlock->GetStatements()[0].get());
}

template<typename _TBreakContinue>
_TBreakContinue *GetBreakContinue(SyntaxContext *pContext)
{
	return pContext->GetSyntaxNode<_TBreakContinue>();
}

template<typename _TBreakContinue, _TBreakContinue *(*_TGetFunc)(SyntaxContext *)>
void SetLevelsA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		_TGetFunc(pContext)->Levels = pContext->Integer;
	}
}

void FinishClassProcedureA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// Add it to the script's procedures, not the class' methods.
		auto proc = pContext->GetFunctionAsProcedure();
		proc->SetClass(pContext->ClassPtr.get()->GetName());
		pContext->Script().AddProcedure(move(proc));
	}
	else
	{
		pContext->ReportError("Expected method declaration.", stream);
	}
}

template<typename _T>
void SetStatementAsConditionA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// Take the current statement, and put into the next statements conditional expression
		pContext->GetSyntaxNode<_T>()->SetCondition(make_unique<ConditionalExpression>(move(pContext->StatementPtrReturn)));
	}
}

template<typename _T>
void AddCodeBlockA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// Time to add a code block.  First, get it.
		pContext->GetSyntaxNode<_T>()->SetCodeBlock(pContext->StealStatementReturn<CodeBlock>());
	}
}
void AddLooperCodeBlockA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// Time to add a code block.  First, get it.
		pContext->GetSyntaxNode<ForLoop>()->SetLooper(pContext->StealStatementReturn<CodeBlock>());
	}
}

template<typename _T>
void SetCaseA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// This is the variable description we need to add to the assignment thing:
		pContext->GetSyntaxNode<_T>()->AddCase(pContext->StealStatementReturn<CaseStatement>());
	}
}

#ifdef ENABLE_EXISTS
void ExistsA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// Turn &exists foobar into:
		// (> argc SOME_NUM_TO_BE_DETERMINED)
		pContext->GetSyntaxNode<BinaryOp>()->Operator = BinaryOperator::GreaterThan;
		pContext->GetSyntaxNode<BinaryOp>()->SetStatement1(make_unique<PropertyValue>("paramTotal", ValueType::Token)); // Does it need to be ComplexPropertyValue??
		pContext->GetSyntaxNode<BinaryOp>()->SetStatement2(make_unique<PropertyValue>(pContext->ScratchString(), ValueType::ParameterIndex));
	}
}
#endif

#ifdef ENABLE_VERBS
unique_ptr<FunctionSignature> _CreateVerbHandlerSignature()
{
	unique_ptr<FunctionSignature> signature = make_unique<FunctionSignature>();
	signature->AddParam("theVerb");
	return signature;
}
unique_ptr<FunctionSignature> _CreateIsVerbHandlerSignature()
{
	unique_ptr<FunctionSignature> signature = make_unique<FunctionSignature>();
	signature->AddParam("theVerb");
	return signature;
}
void SyntaxContext::CreateVerbHandler()
{
	FunctionPtr = std::make_unique<sci::VerbHandlerDefinition>();
	FunctionPtr->AddSignature(_CreateVerbHandlerSignature());
}
void CreateVerbHandlerA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->CreateVerbHandler();
		pContext->FunctionPtr->SetOwnerClass(pContext->ClassPtr.get());
	}
}
void FinishVerbHandlerA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->ClassPtr->AddVerbHandler(move(std::unique_ptr<sci::VerbHandlerDefinition>(static_cast<sci::VerbHandlerDefinition*>(pContext->FunctionPtr.release()))));
	}
	else
	{
		pContext->ReportError("Expected verb handler declaration.", stream);
	}
}
void VerbClauseVerbA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<VerbClauseStatement>()->Verbs.push_back(PropertyValue(pContext->ScratchString(), ValueType::Token));
	}
}
#endif

void InitEnumStartA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result()) { pContext->Integer = 0; }
}
void CreateEnumDefineA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		if (pContext->ifDefDefineState != IfDefDefineState::False)
		{
			auto define = make_unique<Define>(pContext->ScratchString(), pContext->Integer++);
			define->SetScript(&pContext->Script());
			define->SetPosition(stream.GetPosition());
			pContext->Script().AddDefine(move(define));
		}
	}
}
void RestructureNaryAssociativeOpA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		// Restructure (a b c d e) into (a (b (c (d e))))
		// We want evaluation to procede left to right
		auto naryOp = pContext->StealSyntaxNode<NaryOp>();
		auto &statements = naryOp->GetStatements();
		size_t operandCount = statements.size();
		assert(operandCount >= 2);
		auto binaryOp = make_unique<BinaryOp>(naryOp->Operator);
		binaryOp->SetPosition(naryOp->GetPosition());
		auto it = statements.rbegin();
		binaryOp->SetStatement2(move(*it));
		++it;
		for (; it != statements.rend(); ++it)
		{
			if (!binaryOp->GetStatement1())
			{
				binaryOp->SetStatement1(move(*it));
			}
			else
			{
				// Make a new op.
				auto newBinaryOp = make_unique<BinaryOp>(naryOp->Operator);
				newBinaryOp->SetStatement2(move(binaryOp));
				binaryOp = move(newBinaryOp);
				binaryOp->SetPosition(naryOp->GetPosition());
				binaryOp->SetStatement1(move(*it));
			}
		}

		// Now set this back in as the current syntax node.
		pContext->ReplaceSyntaxNode(move(binaryOp));
	}
}

const size_t MaxOpLength = 4;

// Takes the operators, sorts them, puts them into a double-null terminated string.
std::vector<char> GenerateOperatorStringWorker(const set<std::string> &ops)
{
	std::vector<char> result;
	// Organize these by group
	std::map<char, std::set<std::string>> temp;
	bool hasEmpty = false;
	for (auto op : ops)
	{
		assert(op.length() <= MaxOpLength);
		// TODO: Deal with empties
		hasEmpty = hasEmpty || !op[0];
		if (op[0])
		{
			temp[op[0]].insert(op.substr(1));
		}
	}
	// The size of our current level, plus an extra for "empty" (op that terminates at this level) and a sentinel indicating the end of this level.
	size_t relativeOffsetToNext = (temp.size() + (hasEmpty ? 1 : 0)) * 2 + 1;
	std::vector<std::vector<char>> subEntries;
	for (auto &entry : temp)
	{
		std::vector<char> subEntry = GenerateOperatorStringWorker(entry.second);
		result.push_back(entry.first);  // The char
		assert(relativeOffsetToNext < 128);
		result.push_back((char)relativeOffsetToNext);
		relativeOffsetToNext += subEntry.size();
		subEntries.push_back(subEntry);
	}
	if (hasEmpty)
	{
		// Deal with it last.
		result.push_back(' ');  // A space, indicating the end
		result.push_back(0);	// Zero, indicating no offset to next char list.
	}
	// Finally, the sentinel value indicating the end:
	result.push_back(0);
	// Now copy the sub entries
	for (auto &subEntry : subEntries)
	{
		copy(subEntry.begin(), subEntry.end(), back_inserter(result));
	}
	return result;
}

std::string GenerateOperatorString(const set<std::string> &ops)
{
	auto result = GenerateOperatorStringWorker(ops);
	return std::string(result.begin(), result.end());
}

template<typename _TContext>
bool SCIOptimizedOperatorP(const ParserSCI *pParser, _TContext *pContext, streamIt &stream)
{
	const char *currentdb = pParser->_psz;
	char finalOperator[MaxOpLength + 1];
	char *buildOperator = finalOperator;
	char ch;
	while (ch = *stream)
	{
		const char *currentDbStart = currentdb;

		//	not at end	 not equal to char	  and isn't end of op
		while (*currentdb && (ch != *currentdb) && (*currentdb != ' ' || !isspace(ch)))
		{
			currentdb += 2;
		}
		if (*currentdb)
		{
			if (*currentdb == ' ')
			{
				*buildOperator = 0;
				pContext->ScratchString() = finalOperator;
				return true;
			}
			*buildOperator++ = ch;
			assert((buildOperator - finalOperator) <= MaxOpLength);
			// Adjust offset and continue with the next character
			int offset = *(currentdb + 1);
			assert(offset);
			currentdb = currentDbStart + offset;
		}
		else
		{
			// Not a match
			return false;
		}
		++stream;
	}
	return false;
}


ParserSCI SCISyntaxParser::char_p(const char *psz) { return ParserSCI(CharP, psz); }
ParserSCI SCISyntaxParser::operator_p(const char *psz) { return ParserSCI(SCIOptimizedOperatorP, psz); }
ParserSCI SCISyntaxParser::keyword_p(const char *psz) { return ParserSCI(KeywordP, psz); }
ParserSCI SCISyntaxParser::generateSyntaxNodeD()
{
	ParserSCI syntaxnode(SyntaxNodeD);
	return syntaxnode[EndSyntaxNodeA];
}


template<typename _It, typename _TContext, typename _CommentPolicy>
bool QuotedStringSCIP(const ParserBase<_TContext, _It, _CommentPolicy> *pParser, _TContext *pContext, _It &stream)
{
	return _ReadStringSCI<_TContext, _It, '"', '"'>(pContext, stream, pContext->ScratchString());
}

template<typename _It, typename _TContext, typename _CommentPolicy>
bool BraceStringSCIP(const ParserBase<_TContext, _It, _CommentPolicy> *pParser, _TContext *pContext, _It &stream)
{
	return _ReadStringSCI<_TContext, _It, '{', '}'>(pContext, stream, pContext->ScratchString());
}

template<typename _It, typename _TContext, typename _CommentPolicy>
void ValueErrorE(MatchResult &match, const ParserBase<_TContext, _It, _CommentPolicy> *pParser, _TContext *pContext, const _It &stream)
{
	if (!match.Result())
	{
		pContext->ReportError("Expected an expression.", stream);
	}
}

void CreateSynA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->CreateSynonym();
		pContext->SynonymPtr->MainWord = pContext->ScratchString();
		pContext->SynonymPtr->SetPosition(stream.GetPosition());
	}
	else
	{
		pContext->ReportError("Expected word.", stream);
	}
}
void AddSynA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->SynonymPtr->Synonyms.push_back(pContext->ScratchString());
	}
	else
	{
		pContext->ReportError("Expected word.", stream);
	}
}
void FinishSynA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->Script().AddSynonym(std::move(pContext->SynonymPtr));
	}
}

void SaveScratchStringA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->ScratchString2() = pContext->ScratchString();
	}
}
void SaveGlobalVarIndexA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->Integer2 = pContext->Integer;
		pContext->PropertyValueWasSet = false;  // Also reset this.
	}
}
void AddExternDeclA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		auto externDecl = make_unique<ExternDeclaration>();
		externDecl->SetName(pContext->ScratchString2());
		assert(pContext->PropertyValueWasSet);
		externDecl->ScriptNumber = pContext->PropertyValue;
		externDecl->Index = pContext->Integer;
		externDecl->SetPosition(stream.GetPosition());
		pContext->Script().Externs.push_back(move(externDecl));
	}
}
void AddSelectorDeclA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		auto selectorDecl = make_unique<SelectorDeclaration>();
		selectorDecl->SetName(pContext->ScratchString());
		selectorDecl->Index = pContext->Integer;
		selectorDecl->SetPosition(stream.GetPosition());
		pContext->Script().Selectors.push_back(move(selectorDecl));
	}
}

void AddClassDefDeclA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->Script().ClassDefs.push_back(pContext->StealSyntaxNode<ClassDefDeclaration>());
	}
}
void SetScriptNumberA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<ClassDefDeclaration>()->ScriptNumber = pContext->Integer;
	}
	else
	{
		pContext->ReportError(errInteger, stream);
	}
}
void SetClassNumberA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<ClassDefDeclaration>()->ClassNumber = pContext->Integer;
	}
	else
	{
		pContext->ReportError(errInteger, stream);
	}
}
void SetSuperNumberA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<ClassDefDeclaration>()->SuperNumber = pContext->Integer;
	}
	else
	{
		pContext->ReportError(errInteger, stream);
	}
}
void SetFileA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<ClassDefDeclaration>()->File = pContext->ScratchString();
	}
	else
	{
		pContext->ReportError(errFileNameString, stream);
	}
}
void AddClassDefPropA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result()) { pContext->GetSyntaxNode<ClassDefDeclaration>()->Properties.push_back({ pContext->ScratchString(), pContext->Integer }); }
}
void AddClassDefMethodA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result()) { pContext->GetSyntaxNode<ClassDefDeclaration>()->Methods.push_back(pContext->ScratchString()); }
}
void AddGlobalEntryA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		auto globalEntry = std::make_unique<GlobalDeclaration>();
		globalEntry->Index = pContext->Integer2;
		if (pContext->PropertyValueWasSet)
		{
			globalEntry->InitialValue = std::make_unique<PropertyValue>(pContext->PropertyValue);
		}
		globalEntry->SetName(pContext->ScratchString2());
		pContext->Script().Globals.push_back(move(globalEntry));
	}
}
void AddMethodFwdA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->ClassPtr->MethodForwards.push_back(pContext->ScratchString());
	}
}
void AddProcedureFwdA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->Script().ProcedureForwards.push_back(pContext->ScratchString());
	}
}

#ifdef ENABLE_FOREACH
void SetIterationVariableA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<ForEachLoop>()->IterationVariable = pContext->ScratchString();
	}
}

#ifdef ENABLE_LDMSTM
void SetIsReferenceA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->GetSyntaxNode<ForEachLoop>()->IsReference = true;
	}
}
#endif
#endif

#ifdef ENABLE_LDMSTM
void DerefLValueA(MatchResult &match, const ParserSCI *pParser, SyntaxContext *pContext, const streamIt &stream)
{
	if (match.Result())
	{
		pContext->CreateSyntaxNode<LValue>(stream);
		pContext->GetSyntaxNode<LValue>()->IsDeref = true;
		pContext->GetSyntaxNode<LValue>()->SetPosition(stream.GetPosition());
		pContext->GetSyntaxNode<LValue>()->SetName(pContext->ScratchString());
	}
}
#endif

SCISyntaxParser::SCISyntaxParser() :
	oppar(char_p("(")),
	clpar(char_p(")")),
	opbracket(char_p("[")),
	clbracket(char_p("]")),
	pound(char_p("#")),
	atsign(char_p("@")),
	comma(char_p(",")),
	colon(char_p(":")),
	equalSign(char_p("=")),
	question(char_p("?")),
#ifdef ENABLE_LDMSTM
	period(char_p("*")),
	ampersand(char_p("&")),
#endif
	alphanumAsmLabel_p(AlphanumP),
	selector_send_p(SelectorP_Term<':'>),
	propget_p(SelectorP_Term<'?'>),
	filename_p(FilenameP),
	asmInstruction_p(AsmInstructionP),
	alphanumNK_p(AlphanumPNoKeywordOrTerm),
	alphanumNK_p2(AlphanumPNoKeywordOrTerm2),
	alphanumSendToken_p(AlphanumPSendTokenOrTerm),
	alphanum_p(AlphanumP),
	alwaysmatch_p(AlwaysMatchP),
	bracestring_p(BraceStringSCIP),
	squotedstring_p(SQuotedStringP),
	quotedstring_p(QuotedStringSCIP),
	integer_p(IntegerExpandedP),
	integerNonZero_p(IntegerNonZeroP),
	syntaxnode_d(generateSyntaxNodeD())
{
}

//
// The constructor sets up the parse tree
//
void SCISyntaxParser::Load()
{
	if (_fLoaded)
	{
		return;
	}
	_fLoaded = true;

	// Some defaults.
	ParseACChannels ch1StartStatement = SetChannel(NoChannels, ParseAutoCompleteChannel::One, ParseAutoCompleteContext::StartStatementExtras);
	ParseACChannels ch1Block = SetChannel(NoChannels, ParseAutoCompleteChannel::One, ParseAutoCompleteContext::Block);
	ParseACChannels ch2PureValue = SetChannel(NoChannels, ParseAutoCompleteChannel::Two, ParseAutoCompleteContext::PureValue);
	ParseACChannels ch2Block = SetChannel(NoChannels, ParseAutoCompleteChannel::Two, ParseAutoCompleteContext::Block);
	ParseACChannels ch3Selector = SetChannel(NoChannels, ParseAutoCompleteChannel::Three, ParseAutoCompleteContext::Selector);
	ParseACChannels ch3Keyword = SetChannel(NoChannels, ParseAutoCompleteChannel::Three, ParseAutoCompleteContext::ClassLevelKeyword);
	ParseACChannels ch3Block = SetChannel(NoChannels, ParseAutoCompleteChannel::Three, ParseAutoCompleteContext::Block);
	ParseACChannels ch4Rest = SetChannel(NoChannels, ParseAutoCompleteChannel::Four, ParseAutoCompleteContext::Rest);
	ParseACChannels ch4Else = SetChannel(NoChannels, ParseAutoCompleteChannel::Four, ParseAutoCompleteContext::Else);
	ParseACChannels ch4Block = SetChannel(NoChannels, ParseAutoCompleteChannel::Four, ParseAutoCompleteContext::Block);

	// (xxx			 -> procname, pure value or keyword
	// (something xxx   -> pure value or selector name
	ParseACChannels acStartStatement = ch1StartStatement | ch2PureValue | ch3Keyword | ch4Block;
	ParseACChannels acJustAValue = ch1Block | ch2PureValue; // Don't block channel 3, because parent parser might want to add in selectors, not sure about ch4
	ParseACChannels acInSendOrProcCall = ch1Block | ch2PureValue | ch3Selector | ch4Rest;
	// The else in an if statement is special because it doesn't need to be enclosed in ()
	ParseACChannels acElseAddin = ch4Else;
	// We could do more for else here, like removing it from the keywords list, and then manually adding
	// it back for case statements in switch or cond statements.

	// An integer or plain alphanumeric token
	immediateValue = integer_p[PropValueIntA<errInteger>] | alphanumNK_p[PropValueStringA<ValueType::Token, errNoKeywordOrSelector>];

	string_immediateValue_NoResourceString = integer_p[PropValueIntA<errInteger>] | alphanumNK_p[PropValueStringA<ValueType::Token, errNoKeywordOrSelector>] | quotedstring_p[{PropValueStringA<ValueType::String>, ParseAutoCompleteContext::Block}] | bracestring_p[PropValueStringA<ValueType::String>] | squotedstring_p[{PropValueStringA<ValueType::Said>, ParseAutoCompleteContext::Block}] | size_of[PropValueStringA<ValueType::ArraySize>];

	// Top level constructs
	include = keyword_p("include") >> (quotedstring_p | filename_p)[{AddIncludeA, ParseAutoCompleteContext::Block}];
	use = keyword_p("use") >> (quotedstring_p | filename_p)[{AddUseA, ParseAutoCompleteContext::ScriptName}];
	scriptNum = keyword_p("script#") >> immediateValue[{ScriptNumberA, ParseAutoCompleteContext::DefineValue }];
	genText = keyword_p("text#") >> immediateValue[{GenTextA, ParseAutoCompleteContext::DefineValue }];

	define = keyword_p("define")[CreateDefineA] >> alphanumNK_p[DefineLabelA] >> integer_p[DefineValueA];

	enumStatement = keyword_p("enum")[InitEnumStartA] >> -integer_p[ErrorA<errInteger>] >> *alphanumNK_p[CreateEnumDefineA];

	general_token = (alphanumNK_p)[{nullptr, ParseAutoCompleteContext::None, "TOKEN"}];

	// Matches #posn
	// Also matches #posn? or #posn: for backwards compatibility.
	selector_literal = pound >> (alphanumNK_p2[{nullptr, ParseAutoCompleteContext::Selector}] | selector_send_p);

	size_of = keyword_p("&sizeof") >> alphanumNK_p[{nullptr, ParseAutoCompleteContext::PureValue}];

#ifdef ENABLE_EXISTS
	exists_statement = keyword_p("&exists")[SetStatementA<BinaryOp>] >> alphanumNK_p[{ExistsA, ParseAutoCompleteContext::PureValue}];
#endif

	pointer = atsign;

	export_entry = general_token[{nullptr, ParseAutoCompleteContext::Export}] >> integer_p[AddExportA];
	exports = keyword_p("public") >> *export_entry;

	synonym_entry = oppar >> alphanum_p[CreateSynA] >> *alphanum_p[AddSynA] >> clpar;
	synonyms = keyword_p("synonyms") >> *synonym_entry[FinishSynA];

	rvalue_variable =
		(opbracket >> general_token[ComplexValueStringA<ValueType::Token, errVarName>] >> statement[ComplexValueIndexerA] >> clbracket) |
		general_token[ComplexValueStringA<ValueType::Token>];

	value =
		alwaysmatch_p[SetStatementA<ComplexPropertyValue>]
		>> (integer_p[ComplexValueIntA]
		| keyword_p("argc")[ComplexValueParamTotalA]
		| quotedstring_p[{ComplexValueStringA<ValueType::ResourceString>, ParseAutoCompleteContext::Block}]
		| squotedstring_p[{ComplexValueStringA<ValueType::Said>, ParseAutoCompleteContext::Block}]
		| bracestring_p[{ComplexValueStringA<ValueType::String>, ParseAutoCompleteContext::Block}]
		| (-pointer[ComplexValuePointerA] >> rvalue_variable)
#ifdef ENABLE_LDMSTM
		| (period >> general_token[ComplexValueStringA<ValueType::Deref>])
#endif
		| selector_literal[ComplexValueStringA<ValueType::Selector>]
		| size_of[ComplexValueStringA<ValueType::ArraySize>]);

	repeat_statement =
		keyword_p("repeat")[SetRepeatStatementA]
		>> *statement[AddStatementA<WhileLoop>];

	return_statement =
		keyword_p("return")[SetStatementA<ReturnStatement>]
		>> -statement[StatementBindTo1stA<ReturnStatement, nullptr>];

	break_statement =
		keyword_p("break")[SetStatementA<BreakStatement>] >>
		-integerNonZero_p[SetLevelsA<BreakStatement, GetBreakContinue<BreakStatement>>];

	breakif_statement =
		keyword_p("breakif")[SetStatementA<IfStatement>]
		>> statement[FinishBreakContinueIfA<BreakStatement>] >>
		-integerNonZero_p[SetLevelsA<BreakStatement, GetBreakContinueInIf<BreakStatement>>];

	continue_statement =
		keyword_p("continue")[SetStatementA<ContinueStatement>] >>
		-integerNonZero_p[SetLevelsA<ContinueStatement, GetBreakContinue<ContinueStatement>>];

	contif_statement =
		keyword_p("contif")[SetStatementA<IfStatement>]
		>> statement[FinishBreakContinueIfA<ContinueStatement>] >>
		-integerNonZero_p[SetLevelsA<ContinueStatement, GetBreakContinueInIf<ContinueStatement>>];

	bare_code_block =
		alwaysmatch_p[StartStatementA]
		>> (alwaysmatch_p[SetStatementA<CodeBlock>]
		>> *statement[AddStatementA<CodeBlock>])[FinishStatementA];

	wrapped_code_block =
		oppar[StartStatementA]
		>> (alwaysmatch_p[SetStatementA<CodeBlock>]
		>> *statement[AddStatementA<CodeBlock>] >> clpar)[FinishStatementA];

	if_statement =
		keyword_p("if")[SetStatementA<IfStatement>]
		>> statement[SetStatementAsConditionA<IfStatement>] // Condition
		>> bare_code_block[{StatementBindTo1stA<IfStatement, errThen>, acElseAddin }]		  // Code
		>> -(keyword_p("else")	  // Optional else, followed by more code
		>> bare_code_block[StatementBindTo2ndA<IfStatement, errElse>]);

	while_loop =
		keyword_p("while")[SetStatementA<WhileLoop>]
		>> statement[SetStatementAsConditionA<WhileLoop>]
		>> *statement[AddStatementA<WhileLoop>];

	for_loop =
		keyword_p("for")[SetStatementA<ForLoop>]
		>> wrapped_code_block[AddCodeBlockA<ForLoop>]
		>> statement[SetStatementAsConditionA<ForLoop>]
		>> wrapped_code_block[AddLooperCodeBlockA]
		>> *statement[AddStatementA<ForLoop>];

#ifdef ENABLE_FOREACH
	foreach_loop =
		keyword_p("foreach")[SetStatementA<ForEachLoop>]
#ifdef ENABLE_LDMSTM
		>> -ampersand[SetIsReferenceA]
#endif
		>> general_token[SetIterationVariableA]
		>> statement[StatementBindTo1stA<ForEachLoop, errCollectionArg>]
		>> *statement[AddStatementA<ForEachLoop>];
#endif
#ifdef ENABLE_GETPOLY
	getpoly_statement =
		keyword_p("&getpoly")[SetStatementA<GetPolyStatement>]
		>> statement[StatementBindTo1stA<GetPolyStatement, nullptr>];
#endif

	case_statement =
		alwaysmatch_p[StartStatementA]
		>> (oppar[SetStatementA<CaseStatement>]
		>>
			(keyword_p("else")[SetDefaultCaseA] |
			statement[StatementBindTo1stA<CaseStatement, errCaseArg>])[NoCaseE]	// REVIEW: does this need to be constant?
		>> *statement[AddStatementA<CaseStatement>]
		>> clpar[GeneralE]
		)[FinishStatementA];

	switch_statement =
		keyword_p("switch")[SetStatementA<SwitchStatement>]
		>> statement[StatementBindTo1stA<SwitchStatement, errSwitchArg>]
		>> *case_statement[SetCaseA<SwitchStatement>];

	// Nearly identical parsing to a switch statement. We re-work it to ifs in post-processing.
	cond_statement =
		keyword_p("cond")[SetStatementA<CondStatement>]
		>> *case_statement[SetCaseA<CondStatement>];

	switchto_case_statement =
		alwaysmatch_p[StartStatementA]
		>> (oppar[SetStatementA<CaseStatement>]
		>> *statement[AddStatementA<CaseStatement>]
		>> clpar[GeneralE]
		)[FinishStatementA];

	switchto_statement =
		keyword_p("switchto")[SetStatementA<SwitchStatement>]
		>> statement[StatementBindTo1stA<SwitchStatement, errSwitchArg>]
		>> *switchto_case_statement[SetCaseA<SwitchStatement>];

	// (SomeProc param1 param2 param3)
	procedure_call = alphanumNK_p[SetStatementNameA<ProcedureCall>] >> (*statement[AddStatementA<ProcedureCall>])[{nullptr, acInSendOrProcCall}];

	// posn: x y z
	send_param_call = selector_send_p[SetStatementNameA<SendParam>] >> alwaysmatch_p[SendParamIsMethod] >> *statement[AddStatementA<SendParam>];

	// expression selectorA: one two three, selectorB: one two
	// expression selectorA?
	send_call = (alwaysmatch_p[SetStatementA<SendCall>] // Simple form, e.g. gEgo
		>> ((alphanumSendToken_p[SetNameA<SendCall>]) | statement[StatementBindTo1stA<SendCall, errSendObject>])) // Expression, e.g. [clients 4], or (GetTheGuy)
		>>
		(propget_p[AddSimpleSendParamA] |			 // Single prop get
		(syntaxnode_d[send_param_call[AddSendParamA]] % -comma[GeneralE])	  // Or a series regular ones separated by optional comma
		)[{nullptr, acInSendOrProcCall}]			  // AC stuff that's inside a send call
		;

	// Operators
	// These are binary-only operators
	binaryOps = GenerateOperatorString({ ">>", "<<", "-", "/", "mod" });
	binary_operator = operator_p(binaryOps.c_str());

	// n-ary operators that are associative
	naryAssocOps = GenerateOperatorString({ "*", "+", "&", "|", "^", "and", "or" });
	naryassoc_operator = operator_p(naryAssocOps.c_str());

	// n-ary comparison operators
	naryCompareOps = GenerateOperatorString({ "u>=", ">=", "u>", ">", "u<=", "<=", "u<", "!=", "<", "==" });
	narycompare_operator = operator_p(naryCompareOps.c_str());

	unaryOps = GenerateOperatorString({"~", "not", "-", "++", "--" });
	unary_operator = operator_p(unaryOps.c_str());

	assignmentOps = GenerateOperatorString({ "+=", "-=", "*=", "/=", "mod=", "&=", "|=", "^=", ">>=", "<<=", "=" });
	assignment_operator = operator_p(assignmentOps.c_str());

	// blarg	or   [blarg statement]
	lvalue = (opbracket >> general_token[{SetStatementNameA<LValue>, ParseAutoCompleteContext::LValue}] >> statement[LValueIndexerA] >> clbracket) |
		keyword_p("argc")[SetStatementNameToParamTotalA<LValue>] |
#ifdef ENABLE_LDMSTM
		(period >> general_token[{DerefLValueA, ParseAutoCompleteContext::LValue}]) |
#endif
		general_token[{SetStatementNameA<LValue>, ParseAutoCompleteContext::LValue}];

	rest_statement =
		keyword_p("&rest")[SetStatementA<RestStatement>]
		>> -alphanumNK_p[SetStatementNameA<RestStatement>];

	assignment =
		assignment_operator[SetOperatorA<Assignment, AssignmentOperator>]
		>> syntaxnode_d[lvalue[AssignmentVariableA]]
		>> statement[StatementBindTo1stA<Assignment, errArgument>];

	// + 5 view
	binary_operation =
		binary_operator[SetOperatorA<BinaryOp, BinaryOperator>]
		>> statement[StatementBindTo1stA<BinaryOp, errArgument>]
		>> statement[StatementBindTo2ndA<BinaryOp, errBinaryOp>];

	// (+ 1 2 3 4 5)
	// These are parsed temporarily into a NaryOp node, then restructured into next BinaryOp nodes for maximum re-usability.
	// At least two parameters are required.
	naryassoc_operation =
		(naryassoc_operator[SetOperatorA<NaryOp, BinaryOperator>]
		>> statement[AddStatementA<NaryOp>]
		>> ++statement[AddStatementA<NaryOp>])[RestructureNaryAssociativeOpA];

	// (< 3 5 9 10)
	narycompare_operation =
		(narycompare_operator[SetOperatorA<NaryOp, BinaryOperator>]
		>> statement[AddStatementA<NaryOp>]
		>> ++statement[AddStatementA<NaryOp>]);

	// ~ signal
	unary_operation =
		unary_operator[SetOperatorA<UnaryOp, UnaryOperator>]
		>> statement[StatementBindTo1stA<UnaryOp, errArgument>];

	asm_arg =
		alwaysmatch_p[StartStatementA]
		>> value[FinishStatementA]
		>> !(colon | question);	// If colon, then it's the next line's label, if question it's actaully an opcode (e.g. le? ne?)

	asm_label =
		alphanumAsmLabel_p
		>> colon[SetLabelA];

	asm_statement =
		alwaysmatch_p[StartStatementA]
		>> (alwaysmatch_p[SetStatementA<Asm>]
		>> -asm_label											   // Optional label
		>> asmInstruction_p[SetNameA<Asm>]								// Instruction name
		>> -(asm_arg[AddStatementA<Asm>] % comma[GeneralE]))[FinishStatementA];		 // command separated values

	asm_block = keyword_p("asm")[SetStatementA<AsmBlock>]
		>> alwaysmatch_p[SetOpcodesExtraKeywordsA]
		>> *asm_statement[AddStatementA<AsmBlock>]
		>> alwaysmatch_p[RemoveExtraKeywordsA];

	// All possible statements.
	statement = alwaysmatch_p[StartStatementA]
		>>
		(
		(oppar >>
		// Add more here
		(
		repeat_statement |
		assignment |
		binary_operation |
		unary_operation |	   // Due to -, unary must come after binary.
		naryassoc_operation |
		narycompare_operation |
		return_statement |
#ifdef ENABLE_EXISTS
		exists_statement |
#endif
		if_statement |
		while_loop |
		for_loop |
#ifdef ENABLE_FOREACH
		foreach_loop |
#endif
#ifdef ENABLE_GETPOLY
		getpoly_statement |
#endif
		cond_statement |
		switchto_statement |
		switch_statement |
		breakif_statement |
		break_statement |
		continue_statement |
		contif_statement |
		asm_block |
		send_call |			 // Send has to come before procedure. Because procedure will match (foo sel:)
		procedure_call
		)[{nullptr, acStartStatement}] >>
		clpar)
		| (rest_statement
		| value)[{ValueErrorE, acJustAValue }]
		)[FinishStatementA];

	// TODO: we could change this to allow for constant expressions here (that can be evaluated)
	// An array initializer
	array_init = opbracket >> *(statement[ScriptVarInitAutoExpandA]) >> clbracket;  // [0 0 $4c VIEW_EGO]

	// Variable declaration, with optional array size (array size must be numeric!)
	// A   or  [A 6]   or [A MY_ARRAY_SIZE]
	var_decl =
		(opbracket >> alphanumNK_p[CreateVarDeclA] >> (integer_p[VarDeclSizeA] | alphanumNK_p[VarDeclSizeConstantA])[VarDeclSizeErrorA] >> clbracket) |
		alphanumNK_p[CreateVarDeclA];

	script_var = keyword_p("local")
		>> *((var_decl[CreateScriptVarA] >> -(equalSign[GeneralE] >> (statement[ScriptVarInitA] | array_init)))[FinishScriptVarA]);

	// StartFunctionTempVarA is needed to reset "was initializer value set". There are no initializer values for function variables in this syntax.
	function_var_decl = keyword_p("&tmp")[{ StartFunctionTempVarA, ParseAutoCompleteContext::Temp }] >> ++(var_decl[FinishFunctionTempVarA]);

	property_decl = alphanumNK_p[{CreateClassPropertyA, ParseAutoCompleteContext::ClassSelector}]
		>> statement[FinishClassPropertyStatementA];

	// (procname param0 param1 &tmp tmp0 tmp1 tmp2)
	procedure_base = oppar
		>> alphanumNK_p[FunctionNameA]
		>> *alphanumNK_p[FunctionParameterA]
		>> *function_var_decl
		>> clpar[GeneralE]
		>> *statement[FunctionStatementA];

	procedure_decl = keyword_p("procedure")[CreateProcedureA] >> procedure_base[{FunctionCloseA, ParseAutoCompleteContext::Block}];

	// Unsupported (unneeded) methods forward declaration
	// (methods
	//	  m1
	//	  m2
	//	  m3
	// )
	methods_fwd = keyword_p("methods")[{nullptr, ParseAutoCompleteContext::ClassLevelKeyword}] >> *alphanumNK_p[AddMethodFwdA];

	// Very similar to procedure_base, but with some autocomplete differences:
	method_base = oppar
		>> alphanumNK_p[{FunctionNameA, ParseAutoCompleteContext::Selector}]
		>> *alphanumNK_p[{FunctionParameterA, ParseAutoCompleteContext::Block}]
		>> *function_var_decl
		>> clpar[GeneralE]
		>> *statement[FunctionStatementA];

	method_decl = keyword_p("method")[{CreateMethodA, ParseAutoCompleteContext::ClassLevelKeyword}] >> method_base[FunctionCloseA];

#ifdef ENABLE_VERBS
	verb_clause =
		alwaysmatch_p[StartStatementA]
		>> (oppar[SetStatementA<VerbClauseStatement>]
		>> (alphanumNK_p[{VerbClauseVerbA, ParseAutoCompleteContext::DefineValue}] % comma[GeneralE]) // comma separated verbs - must have at least one verb though.
		>> *statement[AddStatementA<VerbClauseStatement>] // then code
		>> clpar[GeneralE])[FinishStatementA];
	// here - first just get nearVerbs working, then do an OR for the thing
	verb_handler_decl = keyword_p("verbs")[{CreateVerbHandlerA, ParseAutoCompleteContext::ClassLevelKeyword}]
		// >> // Todo, allow for temp vars I guess. Not sure how though.
		>> *verb_clause[FunctionStatementA]; 
#endif

	// The properties thing in a class or instance
	properties_decl = oppar >> keyword_p("properties")[{nullptr, ParseAutoCompleteContext::ClassLevelKeyword}] >> *property_decl >> clpar;

	classbase_decl =
		alphanumNK_p[ClassNameA]
		// Though our official syntax is "of", we'll also support kindof for Sierra compatibility.
		>> -((keyword_p("of")[GeneralE] | keyword_p("kindof")[GeneralE]) >> alphanumNK_p[{ClassSuperA, ParseAutoCompleteContext::SuperClass}])
		>> -properties_decl
		>> *(oppar[GeneralE] >>
			(
			methods_fwd |
			method_decl[FinishClassMethodA] |
#ifdef ENABLE_VERBS
			verb_handler_decl[FinishVerbHandlerA] | 
#endif
			procedure_decl[FinishClassProcedureA]) >> clpar);

	instance_decl = keyword_p("instance")[CreateClassA<true>] >> classbase_decl[ClassCloseA];

	class_decl = keyword_p("class")[CreateClassA<false>] >> classbase_decl[ClassCloseA];

	// Non-code items:
	class_def =
		keyword_p("classdef")[SetStatementA<ClassDefDeclaration>] >> alphanumNK_p[SetNameA<ClassDefDeclaration>]; // TODO

	extern_entry =
		general_token[SaveScratchStringA] >> immediateValue >> integer_p[AddExternDeclA];

	extern_section =
		keyword_p("extern") >> *extern_entry;

	selector_entry =
		general_token >> integer_p[AddSelectorDeclA];

	selector_section =
		keyword_p("selectors") >> *selector_entry;

	class_def_attributes =
		(keyword_p("script#") >> integer_p[SetScriptNumberA]) |
		(keyword_p("class#") >> integer_p[SetClassNumberA]) |
		(keyword_p("super#") >> integer_p[SetSuperNumberA]) |
		(keyword_p("file#") >> quotedstring_p[SetFileA]);

	class_def_internal = alphanumNK_p[SetStatementNameA<ClassDefDeclaration>] >>
		*class_def_attributes >>
		oppar[GeneralE] >> keyword_p("properties") >> *(alphanumNK_p >> integer_p)[AddClassDefPropA] >> clpar[GeneralE] >>
		oppar[GeneralE] >> keyword_p("methods") >> *alphanumNK_p[AddClassDefMethodA] >> clpar[GeneralE];

	class_def =
		keyword_p("classdef") >> syntaxnode_d[class_def_internal[AddClassDefDeclA]];

	global_entry =
		general_token[SaveScratchStringA] >> integer_p[SaveGlobalVarIndexA] >> -(equalSign[GeneralE] >> immediateValue);

	global_section =
		keyword_p("global") >> *global_entry[AddGlobalEntryA];

	// Unsupported (unneeded) procedure fwd declaration. Same keyword as an actual procedure
	// definition, but no opening paren after (so that's how we distinguish). In-class procs are also declared here.
	// (procedure
	//	  GetStatus
	//	  SomeOtherProc
	//	  SaveFile
	// )
	procedures_fwd =
		keyword_p("procedure") >> *alphanumNK_p[AddProcedureFwdA];

	entire_script = *(oppar[GeneralE]
		>> (include
		| use
		| define[FinishDefineA]
		| enumStatement
		| instance_decl[FinishClassA]
		| class_decl[FinishClassA]
		| procedure_decl[FinishProcedureA]
		| synonyms
		| exports
		| scriptNum
		| genText

		// The following are not yet supported, but we'll include them to possibly
		// "reserve" for future use.
		| extern_section
		| global_section
		| selector_section
		| class_def
		| procedures_fwd

		| script_var)[{IdentifierE, ParseAutoCompleteContext::TopLevelKeyword}]
		>> clpar[GeneralE]);

	// And for headers, only defines, includes are allowed. And also #ifdef!
	// REVIEW: This is kind of a hack.
	entire_header = *
		(
		(keyword_p("#ifdef") >> alphanumNK_p[EvaluateIfDefA])
		| keyword_p("#endif")[EvaluateEndIfA]
		|
		(oppar[GeneralE] >>
		(include |
		define[FinishDefineA] |
		enumStatement

		// The following are not yet supported, but we'll include them to possibly
		// "reserve" for future use.
		| extern_section
		| global_section
		| selector_section
		| class_def
		| procedures_fwd

		)[IdentifierE] >>
		clpar[GeneralE])
		);

}

#ifdef ENABLE_VERBS
unique_ptr<CaseStatement> _MakeVerbHandlerElse()
{
	unique_ptr<CaseStatement> theCase = make_unique<CaseStatement>();
	theCase->SetDefault(true);

	// (super doVerb: verb item &rest)
	unique_ptr<SendCall> theSend = make_unique<SendCall>();
	theSend->SetName("super");
	// Create the send param to add to the send call
	unique_ptr<SendParam> param = std::make_unique<SendParam>();
	param->SetName("doVerb");
	param->SetIsMethod(true);
	param->AddStatement(make_unique<PropertyValue>("theVerb", ValueType::Token));
	param->AddStatement(make_unique<RestStatement>());
	theSend->AddSendParam(move(param));
	theCase->AddStatement(move(theSend));
	return theCase;
}

void _ProcessVerbHandler(ClassDefinition &theClass, VerbHandlerDefinition &verbHandler, SwitchStatement **pSwitchWeak)
{
	unordered_set<string> usedVerbs;

	// Make a doVerb method with params verb and item - but only do this once.
	unique_ptr<MethodDefinition> doVerbMethod;
	// Now create a switch
	unique_ptr<SwitchStatement> _switch;
	if (!*pSwitchWeak)
	{
		_switch = make_unique<SwitchStatement>();
		_switch->SetStatement1(_MakeTokenStatement("theVerb"));
		*pSwitchWeak = _switch.get();
		doVerbMethod = make_unique<MethodDefinition>();
		doVerbMethod->SetName("doVerb");
		doVerbMethod->SetOwnerClass(&theClass);
		doVerbMethod->AddSignature(_CreateVerbHandlerSignature());
	}
	for (auto &statement : verbHandler.GetStatements())
	{
		assert(statement->GetNodeType() == NodeTypeVerbClause);
		VerbClauseStatement &verbClause = static_cast<VerbClauseStatement&>(*statement);
		unique_ptr<CaseStatement> theCase = make_unique<CaseStatement>();
		theCase->SetCaseValue(make_unique<PropertyValue>(verbClause.Verbs[0]));
		// Give all the code to the case statement
		swap(theCase->GetStatements(), verbClause.GetStatements());
		(*pSwitchWeak)->AddCase(move(theCase));
	}
	if (_switch)
	{
		doVerbMethod->AddStatement(move(_switch));
		theClass.AddMethod(move(doVerbMethod));
	} // but not if we only had a weak ptr.
}

void _ProcessClassForVerbHandlers(Script &script, ClassDefinition &theClass)
{
	SwitchStatement *pSwitchWeak = nullptr;
	for (auto &verbHandler : theClass.GetVerbHandlers())
	{
		_ProcessVerbHandler(theClass, *verbHandler, &pSwitchWeak);
	}
	if (pSwitchWeak)
	{
		// It means we added one. Put in the default handler:
		pSwitchWeak->AddCase(_MakeVerbHandlerElse());
	}
}
#endif

#ifdef ENABLE_FOREACH
bool _IsItADeclaredVariable(const VariableDeclVector &varDecls, const string &name)
{
	return find_if(varDecls.begin(), varDecls.end(), [&name](const auto &varDecl) { return varDecl->GetName() == name; }) != varDecls.end();
}

void _ReplaceIterationVariable(ICompileLog &log, Script &script, ForEachLoop &theForEach, const std::string &iterationVariableOrig, const std::string &newIterationVariable)
{
	// Now replace the iteration variable with the newIterationVariable
	// Replace the iteration variable with [bufferName loopIndexVariable]
	EnumScriptElements<LValue>(theForEach,
		[&log, &script, &iterationVariableOrig, &newIterationVariable](LValue &lValue)
	{
		if (lValue.GetName() == iterationVariableOrig)
		{
			// This is us
			if (lValue.HasIndexer())
			{
				log.ReportResult(CompileResult("An iteration variable can not be indexed.", script.GetScriptId(), lValue.GetPosition().Line()));
			}
			lValue.SetName(newIterationVariable);
		}
	});
	EnumScriptElements<ComplexPropertyValue>(theForEach,
		[&log, &script, &iterationVariableOrig, &newIterationVariable](ComplexPropertyValue &propValue)
	{
		if ((propValue.GetType() == ValueType::Token) && (propValue.GetStringValue() == iterationVariableOrig))
		{
			// This is us
			if (propValue.GetIndexer())
			{
				log.ReportResult(CompileResult("An iteration variable can not be indexed.", script.GetScriptId(), propValue.GetPosition().Line()));
			}
			propValue.SetValue(newIterationVariable, ValueType::Token);
		}
	});
	EnumScriptElements<SendCall>(theForEach,
		[&log, &script, &iterationVariableOrig, &newIterationVariable](SendCall &theSend)
	{
		if (theSend.GetTargetName() == iterationVariableOrig)
		{
			// This is us - if we have a tempvar we can just set a name:
			theSend.SetName(newIterationVariable);
		}
	});
}

void _ProcessForEach(ICompileLog &log, Script &script, FunctionBase &func, ForEachLoop &theForEach, int variableGenHint)
{

	// For now, just support arrays
	// In the future, we could support params and lists.

	// This turns this:
	// (foreach boop buffer
	//	 (boop x: 0)
	// )
	//
	// Into:
	// (for ((= i 0)) (< i &sizeof buffer) ((++ i))
	//	 ([buffer i] x: 0)
	// )
	//
	// This requires making a new iteration variable.

	// Come up with a good loop index name.

	string iterationVariableOrig = theForEach.IterationVariable;

	string newIterationVariable;
#ifdef ENABLE_LDMSTM
	bool isReference = theForEach.IsReference;
	if (!isReference)
#endif
	{
		// If the foreach doesn't use a reference iteration variable, we're going to have an actual variable with this name. To reduce the possibility of conflicts,
		// let's muck up the name. This lets us have multiple foreachs using the same iteration variable name in a function.
		newIterationVariable = (variableGenHint < 26) ? fmt::format("{}__{}", theForEach.IterationVariable, (char)(variableGenHint + 'A')) : fmt::format("{}__{}", theForEach.IterationVariable, variableGenHint); ;
		func.GetVariablesNC().push_back(make_unique<VariableDecl>(newIterationVariable));
	}

	bool isBuffer = true;

	// Buffer name?
	string bufferName = "INVALID";
	if (theForEach.GetStatement1()->GetNodeType() == sci::NodeType::NodeTypeComplexValue)
	{
		ComplexPropertyValue &collection = static_cast<ComplexPropertyValue&>(*theForEach.GetStatement1());
		if (collection.GetType() == ValueType::Token)
		{
			bufferName = collection.GetStringValue();
			// Now, if this bufferName is a local or temp var, then we can use &sizeof.
			// Otherwise, we'll assume it's a FixedList... Or rather, an object with elements and size properties.
			//bool isBuffer = find_if(func.GetVariables().begin(), func.GetVariables().end(), [&bufferName](const std::unique_ptr<VariableDecl> &varDecl) { return varDecl->GetName() == bufferName; }) != func.GetVariables().end();
			isBuffer = _IsItADeclaredVariable(func.GetVariables(), bufferName) || _IsItADeclaredVariable(script.GetScriptVariables(), bufferName);
		}
		else
		{
			log.ReportResult(CompileResult("The collection must be a temp or local array.", script.GetScriptId(), collection.GetPosition().Line()));
		}
	}
	else
	{
		log.ReportResult(CompileResult("The collection must be a temp or local array!", script.GetScriptId(), theForEach.GetStatement1()->GetPosition().Line()));
	}
	// else it would be some statement...

	if (isBuffer)
	{
		string loopIndexName = (variableGenHint < 26) ? fmt::format("i_{}", (char)(variableGenHint + 'A')) : fmt::format("i_{}", variableGenHint);
		func.GetVariablesNC().push_back(make_unique<VariableDecl>(loopIndexName));


		unique_ptr<ForLoop> forLoop = std::make_unique<ForLoop>();

		// Condition
		unique_ptr<BinaryOp> lessThan = make_unique<BinaryOp>(BinaryOperator::LessThan);
		lessThan->SetStatement1(_MakeTokenStatement(loopIndexName));
		lessThan->SetStatement2(_MakeStringStatement(bufferName, ValueType::ArraySize));
		unique_ptr<ConditionalExpression> condition = make_unique<ConditionalExpression>();
		condition->AddStatement(move(lessThan));
		forLoop->SetCondition(move(condition));

		// Initializer
		unique_ptr<Assignment> theEquals = make_unique<Assignment>();
		theEquals->Operator = AssignmentOperator::Assign;
		theEquals->SetVariable(make_unique<LValue>(loopIndexName));
		theEquals->SetStatement1(_MakeNumberStatement(0));
		forLoop->SetCodeBlock(_WrapInCodeBlock(move(theEquals)));

		// Looper
		unique_ptr<UnaryOp> thePlusPlus = make_unique<UnaryOp>();
		thePlusPlus->Operator = UnaryOperator::Increment;
		thePlusPlus->SetStatement1(_MakeTokenStatement(loopIndexName));
		forLoop->SetLooper(_WrapInCodeBlock(move(thePlusPlus)));

#ifdef ENABLE_LDMSTM
		if (!isReference)
		{
#endif
			_ReplaceIterationVariable(log, script, theForEach, iterationVariableOrig, newIterationVariable);
#ifdef ENABLE_LDMSTM
		}
		else
		{
			// Replace the iteration variable with [bufferName loopIndexVariable]
			EnumScriptElements<LValue>(theForEach,
				[&log, &script, &iterationVariableOrig, &bufferName, &loopIndexName](LValue &lValue)
			{
				if (lValue.GetName() == iterationVariableOrig)
				{
					// This is us
					if (lValue.HasIndexer())
					{
						log.ReportResult(CompileResult("An iteration variable can not be indexed.", script.GetScriptId(), lValue.GetPosition().Line()));
					}
					lValue.SetName(bufferName);
					lValue.SetIndexer(_MakeTokenStatement(loopIndexName));
				}
			});
			EnumScriptElements<ComplexPropertyValue>(theForEach,
				[&log, &script, &iterationVariableOrig, &bufferName, &loopIndexName](ComplexPropertyValue &propValue)
			{
				if ((propValue.GetType() == ValueType::Token) && (propValue.GetStringValue() == iterationVariableOrig))
				{
					// This is us
					if (propValue.GetIndexer())
					{
						log.ReportResult(CompileResult("An iteration variable can not be indexed.", script.GetScriptId(), propValue.GetPosition().Line()));
					}
					propValue.SetValue(bufferName, ValueType::Token);
					propValue.SetIndexer(_MakeTokenStatement(loopIndexName));
				}
			});
			EnumScriptElements<SendCall>(theForEach,
				[&log, &script, &iterationVariableOrig, &bufferName, &loopIndexName](SendCall &theSend)
			{
				if (theSend.GetTargetName() == iterationVariableOrig)
				{
					// This is us - if we have a tempvar we can just set a name, but
					// theSend.SetName()
					// Otherwise, we need to mess with the sendcall to change the target to [bufferName loopIndexName]
					unique_ptr<LValue> lValue = make_unique<LValue>(bufferName);
					lValue->SetIndexer(_MakeTokenStatement(loopIndexName));
					theSend.SetLValue(move(lValue));
					theSend.SetName(""); // So we use the LValue
				}
			});
		}
#endif

		// Transfer code to forloop
		swap(forLoop->GetStatements(), theForEach.GetStatements());

#ifdef ENABLE_LDMSTM
		if (!isReference)
#endif
		{
			// Put in one of these at the beginning of the forloop body: (= newIterationVariable [buffer loopIndexName])
			unique_ptr<Assignment> loopAssignment = make_unique<Assignment>();
			loopAssignment->Operator = AssignmentOperator::Assign;
			loopAssignment->SetVariable(make_unique<LValue>(newIterationVariable));
			unique_ptr<ComplexPropertyValue> loopAssignmentValue = make_unique<ComplexPropertyValue>();
			loopAssignmentValue->SetValue(bufferName, ValueType::Token);
			loopAssignmentValue->SetIndexer(_MakeTokenStatement(loopIndexName));
			loopAssignment->SetStatement1(move(loopAssignmentValue));
			forLoop->GetStatements().insert(forLoop->GetStatements().begin(), move(loopAssignment));
		}

		// Our final thing is just one statement, but it might not be for more complex iterators
		theForEach.FinalCode.push_back(move(forLoop));
	}
	else
	{
		// A while loop makes more sense for these things.
		unique_ptr<WhileLoop> whileLoop = std::make_unique<WhileLoop>();

		//KAWA: instead of what Phil did with his intended semantics, I want
		//	(foreach blah myList ...)
		//to become
		//	(= curNode (FirstNode (myList elements?))
		//	(while curNode
		//		(= nextNode (NextNode curNode))
		//		(= blah (NodeValue curNode))
		//		(= curNode nextNode)
		//		...
		//	)

		string curPtrName = (variableGenHint < 26) ? fmt::format("curNode_{}", (char)(variableGenHint + 'A')) : fmt::format("curNode_{}", variableGenHint);
		func.GetVariablesNC().push_back(make_unique<VariableDecl>(curPtrName));

		string nextPtrName = (variableGenHint < 26) ? fmt::format("nextNode_{}", (char)(variableGenHint + 'A')) : fmt::format("nextNode_{}", variableGenHint);
		func.GetVariablesNC().push_back(make_unique<VariableDecl>(nextPtrName));

		// First off, (= curNode (FirstNode (myList elements?))
		unique_ptr<ProcedureCall> firstNodeCall = make_unique<ProcedureCall>("FirstNode");
		firstNodeCall->AddStatement(_MakeSimpleSend(bufferName, "elements"));

		unique_ptr<Assignment> firstAssignment = make_unique<Assignment>();
		firstAssignment->Operator = AssignmentOperator::Assign;
		firstAssignment->SetVariable(make_unique<LValue>(curPtrName));
		firstAssignment->SetStatement1(move(firstNodeCall));

		theForEach.FinalCode.push_back(move(firstAssignment));

		//The (while curNode) bit
		unique_ptr<ConditionalExpression> condition = make_unique<ConditionalExpression>();
		condition->AddStatement(_MakeTokenStatement(curPtrName));
		whileLoop->SetCondition(move(condition));

		// Now the meat of the loop
#ifdef ENABLE_LDMSTM
		if (!isReference)
		{
#endif
			// This is the same as in a buffer-based foreach
			_ReplaceIterationVariable(log, script, theForEach, iterationVariableOrig, newIterationVariable);
#ifdef ENABLE_LDMSTM
		}
		else
		{
			// Replace the iteration variable with *newIterationVariable
			EnumScriptElements<LValue>(theForEach,
				[&log, &script, &iterationVariableOrig, &curPtrName](LValue &lValue)
			{
				if (lValue.GetName() == iterationVariableOrig)
				{
					// This is us
					if (lValue.HasIndexer())
					{
						log.ReportResult(CompileResult("An iteration variable can not be indexed.", script.GetScriptId(), lValue.GetPosition().Line()));
					}
					lValue.SetName(curPtrName);
					lValue.IsDeref = true;
				}
			});
			EnumScriptElements<ComplexPropertyValue>(theForEach,
				[&log, &script, &iterationVariableOrig, &curPtrName](ComplexPropertyValue &propValue)
			{
				if ((propValue.GetType() == ValueType::Token) && (propValue.GetStringValue() == iterationVariableOrig))
				{
					// This is us
					if (propValue.GetIndexer())
					{
						log.ReportResult(CompileResult("An iteration variable can not be indexed.", script.GetScriptId(), propValue.GetPosition().Line()));
					}
					propValue.SetValue(curPtrName, ValueType::Deref);
				}
			});
			EnumScriptElements<SendCall>(theForEach,
				[&log, &script, &iterationVariableOrig, &curPtrName](SendCall &theSend)
			{
				if (theSend.GetTargetName() == iterationVariableOrig)
				{
					// This is us - if we have a tempvar we can just set a name, but
					// theSend.SetName()
					// Otherwise, we need to mess with the sendcall to change the target to *newIterationVariable
					// I don't even think this would compile without foreach! Can't have (*ptr poop:) -> we'd mistake it for multiplcation.
					unique_ptr<LValue> lValue = make_unique<LValue>(curPtrName);
					lValue->IsDeref = true;
					theSend.SetLValue(move(lValue));
					theSend.SetName(""); // So we use the LValue
				}
			});
		}
#endif

		// Transfer code to whileLoop
		swap(whileLoop->GetStatements(), theForEach.GetStatements());

#ifdef ENABLE_LDMSTM
		if (!isReference)
#endif
		{
			//KAWA: We don't want this. We want (= nextNode (NextNode curNode))
			unique_ptr<ProcedureCall> nextNodeCall = make_unique<ProcedureCall>("NextNode");
			nextNodeCall->AddStatement(_MakeStringStatement(curPtrName, ValueType::Token));

			unique_ptr<Assignment> nextAssignment = make_unique<Assignment>();
			nextAssignment->Operator = AssignmentOperator::Assign;
			nextAssignment->SetVariable(make_unique<LValue>(nextPtrName));
			nextAssignment->SetStatement1(move(nextNodeCall));

			//and then (= blah (NodeValue curNode))
			unique_ptr<ProcedureCall> nodeValueCall = make_unique<ProcedureCall>();
			nodeValueCall->SetName("NodeValue");
			nodeValueCall->AddStatement(_MakeStringStatement(curPtrName, ValueType::Token));

			unique_ptr<Assignment> valueAssignment = make_unique<Assignment>();
			valueAssignment->Operator = AssignmentOperator::Assign;
			valueAssignment->SetVariable(make_unique<LValue>(newIterationVariable));
			valueAssignment->SetStatement1(move(nodeValueCall));

			//and then finally (= curNode nextNode)
			unique_ptr<Assignment> passAssignment = make_unique<Assignment>();
			passAssignment->Operator = AssignmentOperator::Assign;
			passAssignment->SetVariable(make_unique<LValue>(curPtrName));
			passAssignment->SetStatement1(_MakeStringStatement(nextPtrName, ValueType::Token));

			//Add them in REVERSE ORDER you doof.
			whileLoop->GetStatements().insert(whileLoop->GetStatements().begin(), move(passAssignment));
			whileLoop->GetStatements().insert(whileLoop->GetStatements().begin(), move(valueAssignment));
			whileLoop->GetStatements().insert(whileLoop->GetStatements().begin(), move(nextAssignment));
		}

		theForEach.FinalCode.push_back(move(whileLoop));
	}
}

void _ProcessForEaches(ICompileLog &log, Script &script)
{
	EnumScriptElements<FunctionBase>(script,
		[&log, &script](FunctionBase &func)
	{
		int variableGenHint = 0;
		EnumScriptElements<ForEachLoop>(func,
			[&log, &script, &func, &variableGenHint](ForEachLoop &forEachLoop)
		{
			_ProcessForEach(log, script, func, forEachLoop, variableGenHint);
			variableGenHint++;
		});
	});
}
#endif

#ifdef ENABLE_GETPOLY
void _ProcessGetPoly(ICompileLog &log, Script &script, FunctionBase &func, GetPolyStatement &theGetPoly)
{
	if (theGetPoly.GetStatement1()->GetNodeType() == sci::NodeType::NodeTypeComplexValue)
	{
		auto cpv = static_cast<ComplexPropertyValue&>(*theGetPoly.GetStatement1());
		if (cpv.GetType() == ValueType::String || cpv.GetType() == ValueType::ResourceString)
		{
			auto ourScriptNum = script.GetScriptNumber();
			auto ourPolyName = "P_" + cpv.GetStringValue();
			if (ourPolyName == "P_")
				ourPolyName = "";
			
			//TODO: Load polygon data for current script, find ourPolyName, and USE THAT BITCH
			auto polyComponent = CreatePolygonComponent(appState->GetResourceMap().Helper().GetPolyFolder(), ourScriptNum);

			const SCIPolygon *ourPolygon = nullptr;
			for (const SCIPolygon &poly : polyComponent->Polygons())
			{
				//if (!poly.Name.empty())
				{
					auto thisPolyName = poly.Name;
					if (thisPolyName.compare(ourPolyName) == 0)
					{
						ourPolygon = &poly;
						break;
					}
				}
			}
			if (ourPolygon)
			{
				unique_ptr<SendCall> polySend = std::make_unique<SendCall>();
				polySend->SetName("Polygon");
				unique_ptr<SendParam> polyNew = std::make_unique<SendParam>();
				polyNew->SetName("new");
				polyNew->SetIsMethod(true);
				polySend->AddSendParam(move(polyNew));

				unique_ptr<SendParam> typeParam = std::make_unique<SendParam>();
				typeParam->SetName("type");
				typeParam->SetIsMethod(false);
				//typeParam->AddStatement(_MakeNumberStatement(2));
				typeParam->AddStatement(_MakeNumberStatement((int)ourPolygon->Type));

				unique_ptr<SendParam> initParam = std::make_unique<SendParam>();
				initParam->SetName("init");
				initParam->SetIsMethod(false);
				//Repeat as needed
				//initParam->AddStatement(_MakeNumberStatement(64));
				//initParam->AddStatement(_MakeNumberStatement(42));
				for (point16 point : ourPolygon->Points())
				{
					initParam->AddStatement(_MakeNumberStatement(point.x));
					initParam->AddStatement(_MakeNumberStatement(point.y));
				}
				//

				unique_ptr<SendParam> yourParam = std::make_unique<SendParam>();
				yourParam->SetName("yourself");
				yourParam->SetIsMethod(false);

				unique_ptr<SendCall> pSend = std::make_unique<SendCall>();
				pSend->SetStatement1(move(polySend));
				pSend->AddSendParam(move(typeParam));
				pSend->AddSendParam(move(initParam));
				pSend->AddSendParam(move(yourParam));
				theGetPoly.FinalCode.push_back(move(pSend));
			}
			else
			{
				log.ReportResult(CompileResult(fmt::format("Unknown polygon name {} in &getpoly.", cpv.GetStringValue()), script.GetScriptId(), cpv.GetPosition().Line()));
			}
		}
		else
		{
			log.ReportResult(CompileResult("&getpoly must have a polygon name as a string parameter, without the P_ in front, or an empty string for Default.", script.GetScriptId(), cpv.GetPosition().Line()));
		}
	}
	else
	{
		log.ReportResult(CompileResult("&getpoly must have a polygon name as a string parameter, without the P_ in front, or an empty string for Default.", script.GetScriptId(), theGetPoly.GetLineNumber()));
	}
}

void _ProcessGetPolys(ICompileLog &log, Script &script)
{
	EnumScriptElements<FunctionBase>(script,
		[&log, &script](FunctionBase &func)
	{
		EnumScriptElements<GetPolyStatement>(func,
			[&log, &script, &func](GetPolyStatement &getPolyStatement)
		{
			_ProcessGetPoly(log, script, func, getPolyStatement);
		});
	});
}
#endif

//
// Fix up scripts so that they conform to standards. Differences in the SCI syntax make
// it difficult to get the script OM in its final state just in the parsing phase.
//
// - Propagate "public" from exports section to the individual procedures/instances
// - Identify switchtos and auto-number their cases
// - Transform cond into if-elseif-else
//
void PostProcessScript(ICompileLog *pLog, Script &script)
{
	// Push public down to instances and procedures
	EnumScriptElements<ProcedureDefinition>(script,
		[&script](ProcedureDefinition &proc)
	{
		proc.SetPublic(script.IsExport(proc.GetName()));
	}
		);

	EnumScriptElements<ClassDefinition>(script,
		[&script](ClassDefinition &instance)
	{
		// This could be a class too (not just instance). The main game class is public.
		instance.SetPublic(script.IsExport(instance.GetName()));
#ifdef ENABLE_VERBS
		_ProcessClassForVerbHandlers(script, instance);
#endif
	}
		);

#ifdef ENABLE_FOREACH
	// Re-work foreach's into for loops (only for compiles where there is a log, e.g. actual compiles)
	if (pLog)
	{
		_ProcessForEaches(*pLog, script);
	}
#endif

#ifdef ENABLE_GETPOLY
	if (pLog)
	{
		_ProcessGetPolys(*pLog, script);
	}
#endif

	// Re-work conds into if-elses.
	EnumScriptElements<CondStatement>(script,
		[pLog, &script](CondStatement &cond)
	{
		// TODO: Enforce that else needs to be last.
		// TODO: What if only one else?
		assert(!cond.GetStatement1());
		if (!cond._clausesTemp.empty())
		{
			std::unique_ptr<CodeBlock> elseCodeBlock;
			auto it = cond._clausesTemp.rbegin();
			if ((*it)->IsDefault())
			{
				elseCodeBlock = make_unique<CodeBlock>(move((*it)->GetStatements()));
				++it;
			}
			std::unique_ptr<IfStatement> currentIf;
			while (it != cond._clausesTemp.rend())
			{
				auto &clause = *it;
				if (clause->IsDefault())
				{
					if (pLog)
					{
						pLog->ReportResult(CompileResult("The else clause must be the last clause in a cond.", script.GetScriptId(), clause->GetPosition().Line()));
					}
					break;
				}
				std::unique_ptr<IfStatement> newIf = make_unique<IfStatement>();
				if (currentIf)
				{
					// Put this in our new if's else block.
					newIf->SetStatement2(make_unique<CodeBlock>(move(currentIf)));
				}
				currentIf = move(newIf);
				// Grab the statement1 of the CaseStatement and put it in the if condition.
				currentIf->SetCondition(make_unique<ConditionalExpression>(move(clause->GetStatement1Internal())));
				// Then transfer all the statements into a CodeBlock for the if.
				currentIf->SetStatement1(make_unique<CodeBlock>(move(clause->GetStatements())));
				if (elseCodeBlock)
				{
					// This goes into the final if.
					currentIf->SetStatement2(move(elseCodeBlock));
				}
				++it;
			}

			// Case where there is just a single else, just copy statements into a codeblock.
			if (elseCodeBlock)
			{
				assert(!currentIf);
				cond.SetStatement1(move(elseCodeBlock));
			}
			else
			{
				assert(!elseCodeBlock);
				cond.SetStatement1(move(currentIf));
			}
		}

		cond._clausesTemp.clear();
	}
	);

	// Identify switchto statements and auto-enumerate the cases. If no case statements have values and none are default,
	// it's a switchto
	EnumScriptElements<SwitchStatement>(script,
		[](SwitchStatement &switchStatement)
	{
		if (all_of(switchStatement._cases.begin(), switchStatement._cases.end(),
			[](std::unique_ptr<CaseStatement> &caseStatement) { return !caseStatement->IsDefault() && (caseStatement->GetCaseValue() == nullptr); }))
		{
			uint16_t value = 0;
			for (auto &caseStatement : switchStatement._cases)
			{
				caseStatement->SetCaseValue(std::make_unique<PropertyValue>(value));
				value++;
			}
		}
	}
		);

	// Report warnings if any un-implemented constructs are used.
	std::vector<std::string> unimplementedWarnings;
	if (!script.Externs.empty())
	{
		unimplementedWarnings.push_back("extern statements(s)");
	}
	if (!script.Globals.empty())
	{
		unimplementedWarnings.push_back("global statements(s)");
	}
	if (!script.ClassDefs.empty())
	{
		unimplementedWarnings.push_back("classdef statements(s)");
	}
	if (!script.Selectors.empty())
	{
		unimplementedWarnings.push_back("selectors statements(s)");
	}
	if (!script.ProcedureForwards.empty())
	{
		unimplementedWarnings.push_back("procedure fwd declaration(s)");
	}
	bool hasMethodFwdDeclarations = false;
	for (auto &theClass : script.GetClasses())
	{
		hasMethodFwdDeclarations = hasMethodFwdDeclarations || !theClass->MethodForwards.empty();
	}
	if (hasMethodFwdDeclarations)
	{
		unimplementedWarnings.push_back("methods fwd declaration(s)");
	}
	if (pLog)
	{
		for (auto &warning : unimplementedWarnings)
		{
			std::string text = warning + "ignored - not implemented";
			pLog->ReportResult(CompileResult(text, script.GetScriptId(), 1, 0, CompileResult::CompileResultType::CRT_Warning));
		}
	}
}

// For error reporting:
template<typename _It>
void ExtractSomeToken(std::string &str, _It &stream)
{
	char ch = *stream;
	while (ch && !isspace(ch) && (ch != '(') && (ch != ')'))
	{
		str += ch;
		ch = *(++stream);
	}
}


//
// This does the parsing.
//
bool SCISyntaxParser::Parse(Script &script, streamIt &stream, std::unordered_set<std::string> preProcessorDefines, ICompileLog *pError, bool addCommentsToOM, bool collectComments)
{
	g_compileSyntaxParseTimer.Start();

	SyntaxContext context(stream, script, preProcessorDefines, addCommentsToOM, collectComments);
	bool fRet = false;

#ifdef PARSE_DEBUG
	context.ParseDebug = true;
#endif

	if (entire_script.Match(&context, stream).Result() && (*stream == 0)) // Needs a full match
	{
		PostProcessScript(pError, script);
		fRet = true;
	}
	else
	{
		// With regards to syntax errors - there can really only be one, because we can't
		// recover afterwards.
		std::string strError = "Error: (" + script.GetScriptId().GetFileNameOrig() + ") ";
		strError += context.GetErrorText();
		streamIt errorPos = context.GetErrorPosition();

		strError += fmt::format(" ({}, {})", errorPos.GetLineNumber(), errorPos.GetColumnNumber());

		// We can maybe improve the error by extracting a token here and seeing if it's a keyword.
		std::string maybeKeyword;
		streamIt errorPosCopy = errorPos;
		ExtractSomeToken(maybeKeyword, errorPosCopy);
		if (!maybeKeyword.empty())
		{
			if (std::find(SCIStatementKeywords.begin(), SCIStatementKeywords.end(), maybeKeyword) != SCIStatementKeywords.end())
			{
				strError += fmt::format(" (Statements must being with a parenthesis: \"({0}\").", maybeKeyword);
			}
			else if (IsOperator(maybeKeyword, sciNameToBinaryOp) || IsOperator(maybeKeyword, sciNameToAssignmentOp) || IsOperator(maybeKeyword, sciNameToUnaryOp))
			{
				strError += fmt::format(" (Operator expressions must begin with a parenthesis: \"({0}\").", maybeKeyword);
			}
			else if (maybeKeyword == "else")
			{
				strError += ": \"else\" cannot appear here.";
			}
			// Maybe more?
			else
			{
				strError += fmt::format(": \"{0}\"", maybeKeyword);
			}
		}

		ScriptId scriptId(script.GetPath().c_str());
		if (pError)
		{
			// Add one to line#, since editor lines are 1-based
			pError->ReportResult(CompileResult(strError, scriptId, errorPos.GetLineNumber() + 1, errorPos.GetColumnNumber(), CompileResult::CRT_Error));
		}
	}
	g_compileSyntaxParseTimer.Stop();
	return fRet;
}

bool SCISyntaxParser::Parse(Script &script, streamIt &stream, std::unordered_set<std::string> preProcessorDefines, SyntaxContext &context)
{
	g_compileSyntaxParseTimer.Start();
	bool fRet = false;
	if (entire_script.Match(&context, stream).Result() && (*stream == 0)) // Needs a full match
	{
		PostProcessScript(nullptr, script);
		fRet = true;
	}
	g_compileSyntaxParseTimer.Stop();
	return fRet;
}

bool SCISyntaxParser::ParseHeader(Script &script, streamIt &stream, std::unordered_set<std::string> preProcessorDefines, ICompileLog *pError, bool collectComments)
{
	g_compileSyntaxParseTimer.Start();
	SyntaxContext context(stream, script, preProcessorDefines, false, collectComments);
	bool fRet = entire_header.Match(&context, stream).Result() && (*stream == 0);
	if (!fRet)
	{
		std::string strError = context.GetErrorText();
		streamIt errorPos = context.GetErrorPosition();
		ScriptId scriptId(script.GetPath().c_str());
		if (pError)
		{
			pError->ReportResult(CompileResult(strError, scriptId, errorPos.GetLineNumber() + 1, errorPos.GetColumnNumber(), CompileResult::CRT_Error));
		}
	}
	else
	{
		PostProcessScript(pError, script);
	}
	g_compileSyntaxParseTimer.Stop();
	return fRet;
}
