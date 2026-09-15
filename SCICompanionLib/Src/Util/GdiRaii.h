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
#pragma once

//
// RAII guards for GDI handles (#51). They hold no MFC view state, so a unit test
// can cover them. Include after the Windows/MFC headers (via stdafx.h).
//

// Selects a GDI object into a device context and restores the previously selected
// object on destruction. GDI will not delete an object a device context still
// holds, so a stack pen/bitmap left selected when it goes out of scope leaks its
// handle. This guard makes sure the object is deselected first. Declare the guard
// AFTER the object it selects, so the guard (restore) destructs before the object
// (delete): the object is deselected before it is deleted.
class GdiSelectGuard
{
public:
	GdiSelectGuard(HDC hdc, HGDIOBJ hObject) : _hdc(hdc), _hOld(::SelectObject(hdc, hObject)) {}
	~GdiSelectGuard() { ::SelectObject(_hdc, _hOld); }
	GdiSelectGuard(const GdiSelectGuard &) = delete;
	GdiSelectGuard &operator=(const GdiSelectGuard &) = delete;

	// The object that was selected before this guard (restored on destruction).
	HGDIOBJ Previous() const { return _hOld; }

private:
	HDC _hdc;
	HGDIOBJ _hOld;
};

// Gets a device context for a window and releases it on destruction. GetDC hands
// out a device context from a small system cache that the system frees only on
// ReleaseDC; a path that forgets ReleaseDC exhausts the cache over time. Dc() may
// be null (GetDC can fail); check it before use.
class WindowDcGuard
{
public:
	explicit WindowDcGuard(CWnd *pWnd) : _pWnd(pWnd), _pDC(pWnd ? pWnd->GetDC() : nullptr) {}
	~WindowDcGuard() { if (_pWnd && _pDC) { _pWnd->ReleaseDC(_pDC); } }
	WindowDcGuard(const WindowDcGuard &) = delete;
	WindowDcGuard &operator=(const WindowDcGuard &) = delete;

	CDC *Dc() const { return _pDC; }

private:
	CWnd *_pWnd;
	CDC *_pDC;
};
