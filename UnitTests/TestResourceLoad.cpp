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
    };
}
