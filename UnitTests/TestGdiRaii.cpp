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
#include "CppUnitTest.h"
#include "GdiRaii.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #51: GDI handles leaked because a stack pen/bitmap was left selected in a DC
    // and then deleted. Deleting an object a DC still holds either fails (the
    // handle leaks) or frees it under the DC (a dangling selection) -- either way a
    // bug. GdiSelectGuard selects an object and restores the previously selected
    // one on destruction, so the object is always deselected before it is deleted.
    TEST_CLASS(TestGdiRaii)
    {
    public:
        TEST_METHOD(GdiSelectGuard_SelectsThenRestoresPrevious)
        {
            HDC hdc = ::CreateCompatibleDC(NULL);
            Assert::IsNotNull(hdc, L"setup: memory DC");
            HGDIOBJ hDefaultPen = ::GetCurrentObject(hdc, OBJ_PEN);
            HPEN hPen = ::CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
            Assert::IsTrue(hPen != NULL, L"setup: pen created");

            {
                GdiSelectGuard guard(hdc, hPen);
                Assert::IsTrue(::GetCurrentObject(hdc, OBJ_PEN) == hPen,
                    L"the pen is selected inside the guard");
                Assert::IsTrue(guard.Previous() == hDefaultPen,
                    L"the guard saved the previously selected pen");
            }

            // The key property: the previous object is restored on scope exit, so
            // the pen is no longer held by the DC. Without the guard, the pen would
            // still be selected here.
            Assert::IsTrue(::GetCurrentObject(hdc, OBJ_PEN) == hDefaultPen,
                L"the previous pen is restored when the guard leaves scope");

            ::DeleteObject(hPen); // safe now: the DC no longer holds it
            ::DeleteDC(hdc);
        }

        // WindowDcGuard must be null-safe: GetDC can fail, and a null window must
        // not crash the constructor or destructor.
        TEST_METHOD(WindowDcGuard_NullWindow_IsNullAndSafe)
        {
            WindowDcGuard guard(nullptr);
            Assert::IsNull(guard.Dc(), L"a null window yields a null DC");
            // Leaving scope must not crash (the destructor guards on the null window).
        }
    };
}
