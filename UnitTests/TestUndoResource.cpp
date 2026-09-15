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
#include "AppState.h"
#include "UndoResource.h"
#include <vector>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // An undo item whose storage is recycled last-in-first-out, so freeing one
    // frame and then allocating the next reuses the same heap address. This
    // makes the dangling-pointer collision that the old dirty-flag check
    // depended on deterministic instead of relying on the allocator's chance
    // reuse. Freed blocks are kept, not returned to the CRT (harmless in a test).
    struct PoolItem
    {
        int value;

        static std::vector<void *> &FreeList()
        {
            static std::vector<void *> freeList;
            return freeList;
        }

        void *operator new(size_t sz)
        {
            std::vector<void *> &fl = FreeList();
            if (!fl.empty())
            {
                void *p = fl.back();
                fl.pop_back();
                return p;
            }
            return ::operator new(sz);
        }

        void operator delete(void *p)
        {
            FreeList().push_back(p);
        }
    };

    class TestUndoDoc : public CUndoResource<CDocument, PoolItem>
    {
    public:
        void v_OnUndoRedo() override {}
        void PubOnUndo() { OnUndo(); }
        void PubOnRedo() { OnRedo(); }
    };

    TEST_CLASS(TestUndoResource)
    {
        bool _createdAppState = false;

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            if (appState == nullptr)
            {
                appState = new AppState(nullptr);
                _createdAppState = true;
            }
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            if (_createdAppState)
            {
                delete appState;
                appState = nullptr;
                _createdAppState = false;
            }
        }

        // The saved-frame dirty check compared a raw pointer. When the saved
        // frame is erased and its heap slot is reused by a later, unsaved frame,
        // the old check found the reused address equal to the saved pointer and
        // cleared the modified flag -> no save prompt (silent data loss). The
        // id-based check gives each frame a durable identity, so a reused address
        // no longer aliases the saved frame.
        TEST_METHOD(UndoResource_DirtyFlagSurvivesFrameReuse)
        {
            PoolItem::FreeList().clear();
            TestUndoDoc doc;

            doc.AddFirstResource(std::make_unique<PoolItem>());     // frame A
            doc.AddNewResourceToUndo(std::make_unique<PoolItem>()); // frame B (unique address)
            doc.SetLastSaved(doc.GetResource());                    // save at B

            doc.PubOnUndo();                                        // -> A
            Assert::IsTrue(doc.IsModified() != FALSE, L"frame A is not the saved frame B");

            // New edit at A erases the redo tail (B); B's slot goes onto the free
            // list. The next allocation (D, below) reuses that exact slot.
            doc.AddNewResourceToUndo(std::make_unique<PoolItem>()); // frame C (fresh slot)
            doc.AddNewResourceToUndo(std::make_unique<PoolItem>()); // frame D reuses B's old slot

            doc.PubOnUndo();                                        // -> C
            Assert::IsTrue(doc.IsModified() != FALSE, L"frame C is not the saved frame");
            doc.PubOnRedo();                                        // -> D (shares B's old address)
            Assert::IsTrue(doc.IsModified() != FALSE,
                L"D is an unsaved edit reusing B's heap address and must not read as saved");
        }
    };
}
