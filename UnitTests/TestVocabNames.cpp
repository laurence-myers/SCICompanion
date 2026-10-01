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
        }

        // A selector with the name of a keyword of the syntax is sel_<number>:
        // the SCI1.1 template names 509 cond, which the text would write as case
        // (the name of 732). A selector past the names is sel_<number> too.
        TEST_METHOD(KeywordSelectorNames_AreNumbered)
        {
            GameSession &session = _game.OpenCopy(TemplateSci11, false, SessionOptions());
            const GameFolderHelper &helper = session.Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper));
            std::string first = lookups.LookupSelectorName(509);
            std::string second = lookups.LookupSelectorName(732);
            Assert::AreEqual(std::string("sel_509"), first);
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
