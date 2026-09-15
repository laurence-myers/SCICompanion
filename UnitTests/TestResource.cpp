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
#include "format.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

std::wstring ToString(const RasterChangeHint& q)
{
    return fmt::format(L"RasterChangeHint:{0:08x}", (int)q);
}

namespace UnitTests
{		
    TEST_CLASS(TestResource)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
        }

        TEST_METHOD(TestCreateDefaultResources)
        {
	        // Create the default resources.
            ResourceEntity *pTest = CreateDefaultViewResource(sciVersion0);
        }

        // Saving a VGA2 view transfers the per-row-offset region using the
        // row-offset stream's size, not the (larger) literal-image size. Before
        // the fix the transfer used celRawData.GetDataSize(), which over-read the
        // row-offset stream; sci::transfer now detects that short read and throws,
        // so WriteTo failed. With the correct size, WriteTo serializes cleanly.
        // (Cels are sized so the literal image is larger than the row offsets:
        // per cel, 32*20 literal bytes vs 20*8 row-offset bytes.)
        TEST_METHOD(ViewVGA2Save_RowOffsetTransferInBounds)
        {
            SCIVersion version = sciVersion2; // ViewFormat::VGA2
            std::unique_ptr<ResourceEntity> resource(CreateViewResource(version));
            RasterComponent &raster = resource->GetComponent<RasterComponent>();
            raster.Resolution = version.DefaultResolution;

            Loop loop;
            for (int c = 0; c < 2; c++)
            {
                Cel cel;
                cel.size = size16(32, 20);
                cel.TransparentColor = 0;
                cel.Data.allocate(cel.GetDataSize());
                for (size_t i = 0; i < cel.GetDataSize(); i++)
                {
                    cel.Data[i] = (uint8_t)((i * 37 + c * 101 + 7) & 0xff);
                }
                loop.Cels.push_back(cel);
            }
            raster.Loops.push_back(loop);

            sci::ostream blob;
            std::map<BlobKey, uint32_t> propertyBag;
            resource->WriteTo(blob, true, 0, propertyBag);
            Assert::IsTrue(blob.GetDataSize() > 0, L"VGA2 view failed to serialize");
        }

        TEST_METHOD(TestViewMirror)
        {
            RasterChange change;
            ResourceEntity *pTest = CreateDefaultViewResource(sciVersion0);
            Assert::IsNotNull(pTest);
            RasterComponent *raster = pTest->TryGetComponent<RasterComponent>();
            Assert::IsNotNull(raster);
            // Insert second loop:
            change = InsertLoop(*raster, 0, false);
            Assert::AreEqual(change.hint, RasterChangeHint::NewView);
            // Make it a mirror of the first:
            change = MakeMirrorOf(*raster, 1, 0);
            Assert::AreEqual(change.hint, RasterChangeHint::NewView);
            Loop &loopOrig = raster->Loops[0];
            Assert::IsFalse(loopOrig.IsMirror);
            Assert::AreEqual(loopOrig.MirrorOf, (uint8_t)0xff);
            Loop &loopMirror = raster->Loops[1];
            Assert::IsTrue(loopMirror.IsMirror);
            Assert::AreEqual(loopMirror.MirrorOf, (uint8_t)0);
            // Un-mirror it
            change = MakeMirrorOf(*raster, 1, -1);
            Assert::AreEqual(change.hint, RasterChangeHint::NewView);
            Assert::IsTrue(!loopMirror.IsMirror);
            Assert::AreEqual(loopMirror.MirrorOf, (uint8_t)0xff);
        }

	};
}