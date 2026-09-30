#include "stdafx.h"

// The keywords of the SCI language, for the compiler and the editor
// (declared in sci.h).

using namespace std;

std::vector<std::string> topLevelKeywordsSCI =
{
	// Keep this alphabetically sorted.
	"class",
	"define",
	"enum",
	"extern",
	"include",
	"instance",
	"local",
	"procedure",
	"public",
	"script#",
	"string",
	"synonyms",
	"text#",
	"use",
};

bool IsTopLevelKeyword(const std::string &word)
{
	auto &list = GetTopLevelKeywords();
	return binary_search(list.begin(), list.end(), word);
}

const std::vector<std::string> &GetTopLevelKeywords()
{
	return topLevelKeywordsSCI;
}

std::vector<std::string> codeLevelKeywordsSCI =
{
	// Sorted
	"&exists",
	"&getpoly",
	"&rest",
	"&sizeof",
	// "&tmp",   // This is special
	"and",
	"argc",
	"asm",
	"break",
	"breakif",
	"cond",
	"contif",
	"continue",
	"else",
	"enum",
	"false",
	"for",
	"foreach",
	"if",
	"mod",
	"not",
	"null",
	"of",
	"or",
	"repeat",
	"return",
	"scriptNumber",
	"self",
	"super",
	"switch",
	"switchto",
	"true",
	"while",
};


bool IsCodeLevelKeyword(const std::string &word)
{
	auto &list = GetCodeLevelKeywords();
	return binary_search(list.begin(), list.end(), word);
}

// Keep in alphabetical order
std::vector<std::string> valueKeywordsSCI =
{
	"argc",
	"false",
	"null",
	"objectFunctionArea",
	"objectInfo",
	"objectLocal",
	"objectName",
	"objectSize",
	"objectSpecies",
	"objectSuperclass",
	"objectTotalProperties",
	"objectType",
	"scriptNumber",
	"self",
	"true",
};

bool IsValueKeyword(const std::string &word)
{
	auto &list = GetValueKeywords();
	return binary_search(list.begin(), list.end(), word);
}

// Kept in alphabetical order: IsClassLevelKeyword searches it with binary_search.
std::vector<std::string> classLevelKeywordsSCI = { "method", "procedure", "properties", "verbs" };
bool IsClassLevelKeyword(const std::string &word)
{
	auto &list = GetClassLevelKeywords();
	return binary_search(list.begin(), list.end(), word);
}

// Sorted:
std::vector<std::string> unimplementedKeywordsSCI =
{
	"class#",
	"classdef",
	"extern",
	"file#",
	"global",
	"methods",
	"selectors",
	"super#",
};

bool IsUnimplementedKeyword(const std::string &word)
{
	return binary_search(unimplementedKeywordsSCI.begin(), unimplementedKeywordsSCI.end(), word);
}

bool IsSCIKeyword(const std::string &word)
{
	return (IsValueKeyword(word) || IsCodeLevelKeyword(word) || IsTopLevelKeyword(word) || IsClassLevelKeyword(word) ||
		IsUnimplementedKeyword(word) || (word == "&tmp"));
}

const std::vector<std::string> &GetValueKeywords()
{
	return valueKeywordsSCI;
}

const std::vector<std::string> &GetCodeLevelKeywords()
{
	return codeLevelKeywordsSCI;
}

const std::vector<std::string> &GetClassLevelKeywords()
{
	return classLevelKeywordsSCI;
}
