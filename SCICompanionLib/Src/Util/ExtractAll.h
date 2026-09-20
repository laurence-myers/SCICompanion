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
#pragma once

class IExtractProgress
{
public:
	// Return false to abort
	virtual bool SetProgress(const std::string &info, int amountDone, int totalAmount) = 0;
	// Called once at the end with a summary of the resources that failed to
	// extract (empty when every resource succeeded). Called on the worker thread.
	virtual void SetSummary(const std::string &summary) { (void)summary; }
};

struct PaletteComponent;

// globalPalette is the game's palette 999, precomputed on the UI thread and
// owned by the caller for the length of the call. The extraction runs on a
// worker thread, so it must not read the resource map's own cached palette
// itself (#133). Null when there is no global palette.
void ExtractAllResources(SCIVersion version, const std::string &destinationFolder, bool extractResources, bool extractPicImages, bool extractViewImages, bool disassembleScripts, bool extractMessages, bool generateWavs, const PaletteComponent *globalPalette, IExtractProgress *progress = nullptr);
