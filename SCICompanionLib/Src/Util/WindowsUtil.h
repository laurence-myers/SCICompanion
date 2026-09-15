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

BOOL HandleEditBoxCommands(MSG* pMsg, CEdit &wndEdit);

// True if the compile-dialog pump should dispatch this message: paint always,
// other messages only when they belong to hDialog (or a child). See #55.
bool ShouldDispatchCompilePumpMessage(const MSG &msg, HWND hDialog);

// Pump paint and input during a compile, dispatching only hDialog's own messages
// (so its Cancel button stays live while a foreign command cannot re-enter the
// resource map). Returns true if a WM_QUIT was seen -- it is reposted via
// PostQuitMessage and the caller must stop its loop. See #55.
bool PumpCompileDialogMessagesQuitPending(HWND hDialog);
