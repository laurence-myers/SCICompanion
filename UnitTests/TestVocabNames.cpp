#include "stdafx.h"
#include "CppUnitTest.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "CompiledScript.h"
#include "Vocab99x.h"
#include "TestSupport.h"
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // The names that the decompiler gives a selector or a kernel with no name
    // of its own, or with the name of another one: sel_<number> and
    // kernel_<number>, which the compiler reads back as the number.
    TEST_CLASS(TestVocabNames)
    {
        NoAppState _noAppState;
        GameCopy _game;

    public:
        TEST_METHOD(NumberedNames_ReadBackAsTheNumber)
        {
            GameSession &session = _game.OpenCopy(TemplateSci11, false, SessionOptions());
            const GameFolderHelper &helper = session.Helper();
            SelectorTable selectors;
            Assert::IsTrue(selectors.Load(helper));
            uint16_t number = 0;
            Assert::IsTrue(selectors.ReverseLookup("sel_900", number));
            Assert::AreEqual(900, (int)number);
            Assert::IsTrue(selectors.IsSelectorName("sel_900"));
            Assert::IsFalse(selectors.ReverseLookup("sel_9x", number));
            Assert::IsFalse(selectors.ReverseLookup("sel_", number));
            Assert::IsFalse(selectors.ReverseLookup("sel_123456", number));

            KernelTable kernels;
            Assert::IsTrue(kernels.Load(helper));
            Assert::IsTrue(kernels.ReverseLookup("kernel_200", number));
            Assert::AreEqual(200, (int)number);
            // A kernel with a name of its own: kernel_<number> is not it (a procedure
            // of the source can have that name).
            Assert::IsFalse(kernels.ReverseLookup("kernel_5", number));

            // sel_<number> of a selector with no name reserves it: a new selector
            // gets another number.
            uint16_t free = (uint16_t)(selectors.GetNames().size() + 20);
            Assert::IsTrue(selectors.ReverseLookup("sel_" + std::to_string(free), number));
            selectors.ReserveNumberedName("sel_" + std::to_string(free), number);
            Assert::AreEqual(std::string("sel_") + std::to_string(free), selectors.Lookup(free));
            for (int i = 0; i < 40; i++)
            {
                Assert::AreNotEqual((int)free, (int)selectors.Add("vocabNamesNew" + std::to_string(i)));
            }
        }

        // The names of the lookups: the SCI1.1 template names 509 cond and 732 case
        // (the decompiled text writes cond as sel_509: DecompileLookups). A selector
        // past the names is sel_<number>.
        TEST_METHOD(SelectorNames_ReadBackAsTheirNumbers)
        {
            GameSession &session = _game.OpenCopy(TemplateSci11, false, SessionOptions());
            const GameFolderHelper &helper = session.Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper));
            std::string first = lookups.LookupSelectorName(509);
            std::string second = lookups.LookupSelectorName(732);
            Assert::AreEqual(std::string("cond"), first);
            Assert::AreEqual(std::string("case"), second);
            uint16_t number = 0;
            for (uint16_t selector : { (uint16_t)509, (uint16_t)732, (uint16_t)4000 })
            {
                Assert::IsTrue(lookups.GetSelectorTable().ReverseLookup(lookups.LookupSelectorName(selector), number));
                Assert::AreEqual((int)selector, (int)number, L"the name reads back as the selector");
            }
            Assert::AreEqual(std::string("sel_4000"), lookups.LookupSelectorName(4000));
            // A kernel past the names reads back too.
            KernelTable kernels;
            Assert::IsTrue(kernels.Load(helper));
            Assert::IsTrue(kernels.ReverseLookup(lookups.LookupKernelName(250), number));
            Assert::AreEqual(250, (int)number);
        }
    };
}
