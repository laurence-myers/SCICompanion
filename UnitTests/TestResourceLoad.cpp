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
#include "stdafx.h"
#include "CppUnitTest.h"
#include "View.h"
#include "ResourceEntity.h"
#include "ResourceMap.h"
#include "AppState.h"
#include "ResourceContainer.h"
#include "RasterOperations.h"
#include "Helper.h"
#include "Stream.h"
#include "format.h"
#include <cstring>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// External-linkage cel readers, defined in View.cpp (no public header). Used by
// the size-validation tests below to feed a crafted cel header directly.
void ReadCelFromVGA11(sci::istream &byteStream, Cel &cel, bool isPic);
void ReadCelFrom(ResourceEntity &resource, sci::istream byteStream, Cel &cel, bool isVGA);

namespace UnitTests
{
    TEST_CLASS(TestResourceLoad)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
        }

        TEST_METHOD(TestLoadResourcesSCI0)
        {
            _gameFolder = SetUpGameSCI0();
            _DoIt();
        }

        TEST_METHOD(TestLoadResourcesSCI11)
        {
            _gameFolder = SetUpGameSCI11();
            _DoIt();
        }

        // A .v16 file is an EGA SCI1 view loose patch. SCICompanion used to ignore
        // it (its patch mask was only view.*/*.v56), so such a view never loaded.
        // Save an EGA view from the SCI0 template as a NNN.v16 loose patch, then
        // re-enumerate and confirm it is picked up as a view and decodes. (#188)
        TEST_METHOD(LoosePatchV16_EgaViewIsLoaded)
        {
            _gameFolder = SetUpGameSCI0(); // an EGA game
            CResourceMap &map = appState->GetResourceMap();

            // Take an existing view from the template to re-save under a new number.
            std::unique_ptr<ResourceBlob> source;
            {
                auto container = map.Resources(ResourceTypeFlags::View, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags);
                for (auto it = container->begin(); it != container->end(); ++it)
                {
                    source = *it;
                    break;
                }
            }
            Assert::IsTrue(source != nullptr, L"the SCI0 template must contain at least one view");

            const int patchNumber = 909; // a number the template does not use
            std::string patchPath = _gameFolder + "\\" + std::to_string(patchNumber) + ".v16";
            Assert::IsTrue(SUCCEEDED(source->SaveToFile(patchPath)), L"failed to write the .v16 loose patch");

            // Re-enumerate: the .v16 must now be recognised and load as a view.
            bool loaded = false;
            auto container = map.Resources(ResourceTypeFlags::View, ResourceEnumFlags::AddInDefaultEnumFlags);
            for (auto it = container->begin(); it != container->end(); ++it)
            {
                if (it.GetResourceNumber() == patchNumber)
                {
                    std::unique_ptr<ResourceBlob> blob = *it;
                    Assert::IsTrue(blob->GetType() == ResourceType::View, L"a .v16 patch must be recognised as a view");
                    std::unique_ptr<ResourceEntity> view = CreateResourceFromResourceData(*blob, false);
                    Assert::IsTrue(view->GetComponent<RasterComponent>().Loops.size() > 0, L"the .v16 view must decode with at least one loop");
                    loaded = true;
                    break;
                }
            }
            Assert::IsTrue(loaded, L"a .v16 loose patch must be enumerated and load as a view");
        }

        TEST_METHOD_CLEANUP(TestLoadResources_Clean)
        {
            CleanUpGame(_gameFolder);
        }

        void _DoIt()
        {
            auto container = appState->GetResourceMap().Resources(ResourceTypeFlags::View, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags);
            for (auto &blob : *container)
            {
                try
                {
                    ResourceEntity *pTest = CreateViewResource(sciVersion0);
                }
                catch (std::exception)
                {
                    std::wstring message = fmt::format(L"Failed to load resource %d of type %d.", blob->GetNumber(), (int)blob->GetType());
                    Assert::IsTrue(false, message.c_str());
                }
            }
        }

    private:
        static std::string _gameFolder;

    };

    std::string TestResourceLoad::_gameFolder;

    // Size-validation tests. A separate class with no game setup, so the loader
    // paths run on crafted in-memory data. (Clean on a Release build; a Debug
    // build without the fix trips CRT asserts on the corrupt data first.)
    TEST_CLASS(TestResourceSizeValidation)
    {
    public:
        // A VGA1.1 raw (magnifier) cel whose 16-bit dimensions multiply to more
        // than fits in int. Before the fix the size overflowed to a 1-byte
        // allocation that the row loop then overran; the loader must reject it.
        TEST_METHOD(VGA11RawCel_OversizeRejected)
        {
            CelHeader_VGA11 hdr = {};
            hdr.size = size16(40000, 60000);
            hdr.always_0xa = 0;            // not 0xa/0x8a -> the raw/magnifier path
            hdr.offsetRLE = sizeof(hdr);   // nonzero, points at the buffer end (seekg valid)
            hdr.offsetLiteral = 0;         // selects the raw branch
            std::vector<uint8_t> buf(sizeof(hdr));
            memcpy(buf.data(), &hdr, sizeof(hdr));

            sci::istream stream(buf.data(), (uint32_t)buf.size());
            Cel cel;
            std::string message;
            bool threw = false;
            try { ReadCelFromVGA11(stream, cel, false); }
            catch (std::exception &e) { threw = true; message = e.what(); }
            Assert::IsTrue(threw, L"an oversize raw cel must be rejected");
            Assert::AreEqual(std::string("Corrupt raster resource."), message);
        }

        // Saving a view whose serialized loop offsets would exceed the 16-bit
        // offset field must be rejected, not silently truncated. The first loop's
        // RLE data (alternating pixels defeat the run encoding) exceeds 64 KB, so
        // the second loop's start offset overflows 16 bits.
        TEST_METHOD(OversizeViewSave_Rejected)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion0)); // EGA, no palette
            RasterComponent &raster = view->GetComponent<RasterComponent>();
            raster.Loops.clear();

            Loop bigLoop;
            for (int c = 0; c < 2; c++) // two 320x240 cels -> well over 64 KB after RLE
            {
                Cel cel;
                cel.size = size16(320, 240);
                cel.TransparentColor = 0;
                cel.Data.allocate(cel.GetDataSize());
                for (size_t i = 0; i < cel.GetDataSize(); i++)
                {
                    cel.Data[i] = (uint8_t)(i & 1); // alternating -> ~1 byte/pixel
                }
                bigLoop.Cels.push_back(cel);
            }
            raster.Loops.push_back(bigLoop);

            Loop tinyLoop;
            {
                Cel cel;
                cel.size = size16(1, 1);
                cel.TransparentColor = 0;
                cel.Data.allocate(cel.GetDataSize());
                cel.Data[0] = 0;
                tinyLoop.Cels.push_back(cel);
            }
            raster.Loops.push_back(tinyLoop);

            sci::ostream out;
            std::string message;
            bool threw = false;
            try { view->WriteToTest(out, true, 0); }
            catch (std::exception &e) { threw = true; message = e.what(); }
            Assert::IsTrue(threw, L"an oversize view save must be rejected, not truncated");
            Assert::AreEqual(std::string("View resource is too large"), message);
        }

        // An EGA view cel whose declared size is far larger than any real cel --
        // the Police Quest 3 EGA view 390 corruption, loop 0 cel 2, size 23828x5633.
        // The loader must not reject the whole view; it collapses the corrupt cel to
        // a 1x1 placeholder and keeps going, so the rest of the view still loads.
        // (#182)
        TEST_METHOD(CorruptEgaCelSize_CollapsesToPlaceholder)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion0)); // EGA, 320x200 max

            std::vector<uint8_t> buf;
            auto push16 = [&](uint16_t v) { buf.push_back((uint8_t)(v & 0xff)); buf.push_back((uint8_t)(v >> 8)); };
            push16(23828); // width  -- out of range
            push16(5633);  // height -- out of range
            push16(0);     // placement
            buf.push_back(0); // transparent colour
            // No image data on purpose: a corrupt size must be caught before any decode.

            sci::istream stream(buf.data(), (uint32_t)buf.size());
            Cel cel;
            bool threw = false;
            try { ReadCelFrom(*view, stream, cel, false); }
            catch (std::exception &) { threw = true; }
            Assert::IsFalse(threw, L"a corrupt cel size must not fail the whole view");
            Assert::AreEqual(1, (int)cel.size.cx, L"corrupt cel width collapses to 1");
            Assert::AreEqual(1, (int)cel.size.cy, L"corrupt cel height collapses to 1");
        }
    };
}
