/***************************************************************************
	Copyright (c) 2020 Philip Fortier

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
#include "PhonemeMap.h"
#include "GameFolderHelper.h"
#include "TomlFile.h"
#include "format.h"

using namespace std;

std::unordered_map<std::string, std::string> CreatePhonemeToExampleMap();

std::string GetPhonemeMapFilespec(int view, int loop)
{
	return fmt::format("phoneme_{0}_{1}.ini", view, loop);
}

std::string GetPhonemeMapPath(const GameFolderHelper &helper, int view, int loop)
{
	std::string folder = helper.GetLipSyncFolder();
	folder += "\\";
	folder += GetPhonemeMapFilespec(view, loop);
	return folder;
}

std::unique_ptr<PhonemeMap> LoadPhonemeMapForViewLoop(const GameFolderHelper &helper, int view, int loop)
{
	std::string fullPath = GetPhonemeMapPath(helper, view, loop);
	return std::make_unique<PhonemeMap>(fullPath);
}

PhonemeMap::PhonemeMap(const std::string &filename) : _filespec(filename.substr(filename.find_last_of("\\/")))
{
	if (PathFileExists(filename.c_str()))
	{
		sci::Result<toml::table> table = ParseTomlFile(filename);
		if (table)
		{
			const toml::table *phonemes = (*table)["phoneme_to_cel"].as_table();
			if (phonemes)
			{
				for (const auto &entry : *phonemes)
				{
					string phoneme(entry.first.str());
					// A value that is not an integer (for example `ah = "3"`)
					// is not a parse error: it is reported, and the entry is
					// skipped.
					const toml::value<int64_t> *value = entry.second.as_integer();
					if (value)
					{
						_phonemeToCel[phoneme] = (int)value->get();
					}
					else
					{
						if (!_errors.empty())
						{
							_errors += "\n";
						}
						_errors += "phoneme_to_cel." + phoneme + " is not an integer; entry ignored.";
					}
				}
			}

			ifstream file;
			file.open(filename, std::ios_base::binary | std::ios_base::in);
			if (file.is_open())
			{
				_fileContents = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			}
		}
		else
		{
			_errors = table.error().ToString();
		}
	}
	else
	{
		_errors = std::string("Can't open ") + filename;
	}
}

void PhonemeMap::SetCel(const std::string &phoneme, uint16_t cel)
{
	_phonemeToCel[phoneme] = cel;
}

uint16_t PhonemeMap::PhonemeToCel(const std::string &phoneme) const
{
	uint16_t cel = 0xffff;
	auto it = _phonemeToCel.find(phoneme);
	if (it != _phonemeToCel.end())
	{
		cel = (uint16_t)it->second;
	}
	return cel;
}

// Detects an empty phonememap (all zeroes)
bool PhonemeMap::IsEmpty() const
{
	auto itNotZero = std::find_if(_phonemeToCel.begin(), _phonemeToCel.end(),
		[](const std::pair<std::string, int> &entry) { return entry.second != 0; }
		);
	return itNotZero == _phonemeToCel.end();
}

void SaveToStream(std::ofstream &file, const PhonemeMap &map)
{
	std::unordered_map<std::string, std::string> examples = CreatePhonemeToExampleMap();

	file << "[phoneme_to_cel]\r\n";
	for (const auto &entry : map.Entries())
	{
		file << fmt::format("{0} =\t{1}\t# {2}\r\n", entry.first, entry.second, examples[entry.first]);
	}
}

bool SaveForViewLoop(const PhonemeMap &map, const GameFolderHelper &helper, int view, int loop, std::string &errors)
{
	std::string fullPath = GetPhonemeMapPath(helper, view, loop);
	std::string fullPathBak = fullPath + ".bak";

	std::ofstream fileBak;
	fileBak.open(fullPathBak, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
	if (fileBak.is_open())
	{
		SaveToStream(fileBak, map);
		fileBak.close();
	}

	std::unique_ptr<PhonemeMap> mapTest = std::make_unique<PhonemeMap>(fullPathBak);
	if (!mapTest->HasErrors())
	{
		// Save the real one.
		std::ofstream file;
		file.open(fullPath, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
		if (file.is_open())
		{
			SaveToStream(file, map);
			file.close();
		}
	}

	deletefile(fullPathBak);

	errors = mapTest->GetErrors();
	return !mapTest->HasErrors();
}
