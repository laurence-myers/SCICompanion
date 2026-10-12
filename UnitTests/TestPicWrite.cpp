#include "stdafx.h"
#include "CppUnitTest.h"
#include "TestSupport.h"
#include "format.h"
#include "GameFolderHelper.h"
#include "Pic.h"
#include "PicCommands.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceSourceFlags.h"
#include "ResourceUtil.h"
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    namespace
    {
        // The bytes of an EGA pic with the commands, as SCI Companion writes
        // it: the bytes of the commands, then the end byte.
        std::vector<uint8_t> WrittenPic(const std::vector<PicCommand> &commands)
        {
            std::unique_ptr<ResourceEntity> pic(CreatePicResource(sciVersion0));
            pic->GetComponent<PicComponent>().commands = commands;
            sci::ostream stream;
            std::map<BlobKey, uint32_t> propertyBag;
            pic->WriteTo(stream, false, 1, propertyBag);
            return std::vector<uint8_t>(stream.GetInternalPointer(), stream.GetInternalPointer() + stream.GetDataSize());
        }

        // The commands of the bytes of an EGA pic, as SCI Companion reads
        // them.
        std::vector<PicCommand> ReadCommands(const std::vector<uint8_t> &data)
        {
            GameFolderHelper helper;
            ResourceBlob blob(helper, "pic", ResourceType::Pic, data, sciVersion0.DefaultVolumeFile, 1, NoBase36, sciVersion0, ResourceSourceFlags::PatchFile);
            sci::Result<std::unique_ptr<ResourceEntity>> pic = TryCreateResourceFromResourceData(blob);
            AssertOk(pic, "reading the pic");
            return (*pic)->GetComponent<PicComponent>().commands;
        }

        // The type and the values of a line or of a pattern, as text.
        std::string NameOf(const PicCommand &command)
        {
            char name[100] = {};
            command.GetName(name, ARRAYSIZE(name));
            return name;
        }

        std::string Hex(const std::vector<uint8_t> &bytes)
        {
            std::string text;
            for (uint8_t byte : bytes)
            {
                text += fmt::format("{0:02x} ", (int)byte);
            }
            return text;
        }

        // The writer gives the bytes for the commands, and the bytes read
        // back as the same commands.
        void AssertWrittenAndKept(const std::vector<PicCommand> &commands, const std::vector<uint8_t> &expectedBytes)
        {
            std::vector<uint8_t> written = WrittenPic(commands);
            Assert::AreEqual(Hex(expectedBytes), Hex(written), L"the bytes of the pic");

            std::vector<PicCommand> readBack = ReadCommands(written);
            Assert::AreEqual(commands.size(), readBack.size(), L"the count of the commands");
            for (size_t i = 0; i < commands.size(); i++)
            {
                Assert::AreEqual(NameOf(commands[i]), NameOf(readBack[i]), Wide("command " + std::to_string(i)).c_str());
            }
        }

        // A solid round pattern of size 1.
        PicCommand Pattern(int16_t x, int16_t y)
        {
            return PicCommand::CreatePattern(x, y, 1, 0, false, false);
        }
    }

    // The pic writer (SerializeAllCommands_SCI0_SCI1) keeps the lines and
    // the patterns. A point with a negative x or y has no absolute
    // coordinate (12 bits for x, 12 bits for y, no sign): it is in the pic
    // data only as a relative step from the point before it.
    //
    // A medium step is the byte of y (a sign bit and 7 bits; the byte is
    // less than 0xf0), then the byte of x (a signed byte).
    TEST_CLASS(TestPicWrite)
    {
    public:
        // The line of pic 112 of Space Quest 5: 111 rows up, to row -1.
        TEST_METHOD(Line_MediumStepToANegativeRow_IsKept)
        {
            AssertWrittenAndKept(
                { PicCommand::CreateLine(112, 110, 112, -1) },
                { 0xf5, 0x00, 0x70, 0x6e, 0xef, 0x00, 0xff });
        }

        // A step that has room in one byte (a short line) is a medium step
        // when it goes to a negative point.
        TEST_METHOD(Line_ShortStepToANegativePoint_IsAMediumStep)
        {
            AssertWrittenAndKept(
                { PicCommand::CreateLine(3, 2, -2, -1) },
                { 0xf5, 0x00, 0x03, 0x02, 0x83, 0xfb, 0xff });
        }

        // A negative column with a row that is not negative: a line to the
        // point, then a line from it to a point with a negative column.
        TEST_METHOD(Line_ToAndFromANegativeColumn_IsKept)
        {
            AssertWrittenAndKept(
                { PicCommand::CreateLine(3, 2, -2, 2), PicCommand::CreateLine(-2, 2, -40, 30) },
                { 0xf5, 0x00, 0x03, 0x02, 0x00, 0xfb, 0x1c, 0xda, 0xff });
        }

        // The second line starts at a negative point, so it cannot start a
        // new command (a command starts with an absolute coordinate). It is
        // a longer step than the first line: the two lines are one command
        // of medium steps.
        TEST_METHOD(Line_FromANegativePoint_ContinuesTheCommand)
        {
            AssertWrittenAndKept(
                { PicCommand::CreateLine(5, 2, 5, -3), PicCommand::CreateLine(5, -3, 60, 40) },
                { 0xf5, 0x00, 0x05, 0x02, 0x85, 0x00, 0x2b, 0x37, 0xff });
        }

        // The first line is a command of absolute coordinates. The second
        // line starts where the first ends, but it does not continue that
        // command: it goes to a negative point.
        TEST_METHOD(Line_ToANegativePoint_DoesNotContinueAbsoluteLines)
        {
            AssertWrittenAndKept(
                { PicCommand::CreateLine(10, 10, 300, 10), PicCommand::CreateLine(300, 10, 300, -5) },
                { 0xf6, 0x00, 0x0a, 0x0a, 0x10, 0x2c, 0x0a, 0xf5, 0x10, 0x2c, 0x0a, 0x8f, 0x00, 0xff });
        }

        // The largest medium steps: 128 columns left and 111 rows up, then
        // 127 columns right and 127 rows down.
        TEST_METHOD(Line_LargestMediumSteps_AreKept)
        {
            AssertWrittenAndKept(
                { PicCommand::CreateLine(200, 100, 72, -11), PicCommand::CreateLine(72, -11, 199, 116) },
                { 0xf5, 0x00, 0xc8, 0x64, 0xef, 0x80, 0x7f, 0x7f, 0xff });
        }

        // One more than the largest medium step, in each direction: each
        // line gets absolute coordinates.
        TEST_METHOD(Line_StepsLargerThanAMediumStep_AreAbsolute)
        {
            AssertWrittenAndKept(
                {
                    PicCommand::CreateLine(200, 150, 71, 150),   // 129 columns left
                    PicCommand::CreateLine(71, 150, 199, 150),   // 128 columns right
                    PicCommand::CreateLine(199, 150, 199, 38),   // 112 rows up
                    PicCommand::CreateLine(199, 38, 199, 166),   // 128 rows down
                },
                { 0xf6, 0x00, 0xc8, 0x96, 0x00, 0x47, 0x96, 0x00, 0xc7, 0x96, 0x00, 0xc7, 0x26, 0x00, 0xc7, 0xa6, 0xff });
        }

        // The pattern before the first negative point starts a command of
        // medium steps; the patterns at the negative points, and the pattern
        // after them, continue it.
        TEST_METHOD(Pattern_StepsToNegativePoints_AreKept)
        {
            AssertWrittenAndKept(
                { Pattern(10, 10), Pattern(10, 5), Pattern(10, -1), Pattern(60, -3), Pattern(20, 30) },
                { 0xf9, 0x01, 0xf4, 0x00, 0x0a, 0x0a, 0xfd, 0x00, 0x0a, 0x05, 0x86, 0x00, 0x82, 0x32, 0x21, 0xd8, 0xff });
        }

        // Patterns at points with a negative column and a row that is not
        // negative.
        TEST_METHOD(Pattern_StepsToNegativeColumns_AreKept)
        {
            AssertWrittenAndKept(
                { Pattern(3, 2), Pattern(-2, 2), Pattern(-40, 30) },
                { 0xf9, 0x01, 0xfd, 0x00, 0x03, 0x02, 0x00, 0xfb, 0x1c, 0xda, 0xff });
        }

        // Patterns with a pattern number (1, 2 and 3; size 3): the byte of
        // the number (the number times 2) is before each coordinate, also
        // before the medium step to the negative point.
        TEST_METHOD(Pattern_WithAPatternNumber_StepToANegativePoint_IsKept)
        {
            AssertWrittenAndKept(
                {
                    PicCommand::CreatePattern(10, 3, 3, 1, true, false),
                    PicCommand::CreatePattern(10, -2, 3, 2, true, false),
                    PicCommand::CreatePattern(11, 0, 3, 3, true, false),
                },
                { 0xf9, 0x23, 0xfd, 0x02, 0x00, 0x0a, 0x03, 0x04, 0x85, 0x00, 0x06, 0x02, 0x01, 0xff });
        }

        // The second pattern does not continue the command of absolute
        // coordinates: the pattern after it is at a negative point.
        TEST_METHOD(Pattern_ToANegativePoint_DoesNotContinueAbsolutePatterns)
        {
            AssertWrittenAndKept(
                { Pattern(10, 10), Pattern(300, 20), Pattern(300, -2) },
                { 0xf9, 0x01, 0xfa, 0x00, 0x0a, 0x0a, 0xfd, 0x10, 0x2c, 0x14, 0x96, 0x00, 0xff });
        }
    };
}
