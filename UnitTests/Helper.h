/***************************************************************************
    Copyright (c) 2015 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
#pragma once

std::string SetUpGame(const std::string &name);
std::string GetTestFileDirectory(const std::string &subDirectory);
// The directory of the test module (the build output folder). Snapshot actuals
// are written under here so RunTests.ps1 -UpdateSnapshots can copy them back.
std::string GetTestModuleDirectory();
void CleanUpGame(const std::string &gameFolder);
std::string SetUpGameSCI0();
std::string SetUpGameSCI11();

// Points the app state at a game folder that already exists, with no copy.
// For read-only whole-game dumps (the golden diff). Nothing is written to the
// game folder. Pair with CleanUpExistingGame, which does not delete anything.
void SetUpExistingGame(const std::string &gameFolder);
void CleanUpExistingGame();

// Copies an existing game from an absolute source folder (e.g. a real Sierra
// game under F:\Games\Sierra\...) to a temp folder and points AppState there,
// so recompiles and commits never touch the original. Point it at the concrete
// folder that holds resource.map, not a variant parent. Returns the temp folder
// to pass to CleanUpGame. The copy includes any audio/video volumes, which is
// slow for CD titles, but this is only used by the opt-in bytecode oracle test.
std::string SetUpExistingGameCopy(const std::string &absoluteGameFolder);