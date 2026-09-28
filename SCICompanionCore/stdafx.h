// The precompiled header of SCICompanionCore: Windows, the ATL point and
// size types, and the STL. It has no MFC, so a core file cannot use the GUI
// (docs/scic-cli/plan.md, section 9, phase E).

#pragma once

// A core file must not include an MFC header. afx.h, which every MFC header
// includes first, stops the compile with an #error when this is defined (its
// text is about <atldbgmem.h>; the core does not use that header).
#define __ATLDBGMEM_H__

// The Windows headers that MFC's VC_EXTRALEAN leaves in (afxv_w32.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// The same Windows version as the GUI library (SCICompanionLib\stdafx.h),
// so the Windows structs have one layout in both libraries.
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0410
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0501
#endif
#include <SDKDDKVer.h>

#include <windows.h>
#include <shellapi.h>        // SHFileOperation
#include <mmsystem.h>        // WAVE_FORMAT_PCM
#include <atltypes.h>       // CPoint, CSize, CRect

#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <string>
#include <sstream>
#include <algorithm>
#include <vector>
#include <map>
#include <list>
#include <set>
#include <iostream>
#include <functional>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <stack>
#include <cassert>
#include <typeinfo>
#include <typeindex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iterator>

// Deprecated warnings caused by shlwapi
#pragma warning(disable: 4995)

// decorated name length exceeded
#pragma warning(disable: 4503)

// for deleting values in a map:
struct delete_map_value
{
    template<typename TKEY, typename TVALUE>
    void operator()(const std::pair<TKEY, TVALUE> &ptr) const
    {
        delete ptr.second;
    }
};

// Commonly included or large header files:
#include "sci.h"
#include "Version.h"
#include "Stream.h"
#include "StlUtil.h"
#include "Result.h"
