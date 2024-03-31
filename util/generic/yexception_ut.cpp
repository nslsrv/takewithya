#include "yexception.h"

static inline void Throw1DontMove() {
    ythrow yexception() << "blabla"; // don't move this line
}

static inline void Throw2DontMove() {
    ythrow yexception() << 1 << " qw " << 12.1; // don't move this line
}

#include <library/unittest/registar.h>

#include <util/stream/output.h>
#include <util/string/subst.h>

#include "yexception_ut.h"
#include "bt_exception.h"

#if defined(_MSC_VER)
#pragma warning(disable : 4702) /*unreachable code*/
#endif

static void CallbackFun(int i) {
    throw i;
}

static TOutputStream* OUTS = nullptr;

class TExceptionTest: public TTestBase {
    UNIT_TEST_SUITE(TExceptionTest);
    UNIT_TEST_EXCEPTION(TestException, yexception)
    UNIT_TEST_EXCEPTION(TestLineInfo, yexception)
    UNIT_TEST(TestCurrentExceptionMessageWhenThereisNoException)
    UNIT_TEST(TestFormat1)
    UNIT_TEST(TestRaise1)
    UNIT_TEST(TestVirtuality)
    UNIT_TEST(TestVirtualInheritance)
    UNIT_TEST(TestMixedCode)
    UNIT_TEST(TestBackTrace)
    UNIT_TEST(TestRethrowAppend)
    UNIT_TEST(TestMacroOverload)
    UNIT_TEST_SUITE_END();

private:
    inline void TestRethrowAppend() {
        try {
            try {
                ythrow yexception() << "shit";
            } catch (yexception& e) {
                e << "happens";

                throw;
            }
        } catch (...) {
            UNIT_ASSERT(CurrentExceptionMessage().Contains("shithappens"))
        }
    }

    inline void TestCurrentExceptionMessageWhenThereisNoException() {
        UNIT_ASSERT(CurrentExceptionMessage() == "(NO EXCEPTION)");
    }

    inline void TestBackTrace() {
        try {
            ythrow TWithBackTrace<TIoSystemError>() << "test";
        } catch (...) {
            UNIT_ASSERT(CurrentExceptionMessage().find('\n') != TString::npos);

            return;
        }

        UNIT_ASSERT(false);
    }

    inline void TestVirtualInheritance() {
        TStringStream ss;

        OUTS = &ss;

        class TA {
        public:
            inline TA() {
                *OUTS << "A";
            }
        };

        class TB {
        public:
            inline TB() {
                *OUTS << "B";
            }
        };

        class TC: public virtual TB, public virtual TA {
        public:
            inline TC() {
                *OUTS << "C";
            }
        };

        class TD: public virtual TA {
        public:
            inline TD() {
                *OUTS << "D";
            }
        };

        class TE: public TC, public TD {
        public:
            inline TE() {
                *OUTS << "E";
            }
        };

        TE e;

        UNIT_ASSERT_EQUAL(ss.Str(), "BACDE");
    }

    inline void TestVirtuality() {
        try {
            ythrow TFileError() << "1";
            UNIT_ASSERT(false);
        } catch (const TIoException&) {
        } catch (...) {
            UNIT_ASSERT(false);
        }

        try {
            ythrow TFileError() << 1;
            UNIT_ASSERT(false);
        } catch (const TSystemError&) {
        } catch (...) {
            UNIT_ASSERT(false);
        }

        try {
            ythrow TFileError() << '1';
            UNIT_ASSERT(false);
        } catch (const yexception&) {
        } catch (...) {
            UNIT_ASSERT(false);
        }

        try {
            ythrow TFileError() << 1.0;
            UNIT_ASSERT(false);
        } catch (const TFileError&) {
        } catch (...) {
            UNIT_ASSERT(false);
        }
    }

    inline void TestFormat1() {
        try {
            throw yexception() << 1 << " qw " << 12.1;
            UNIT_ASSERT(false);
        } catch (...) {
            const TString err = CurrentExceptionMessage();

            UNIT_ASSERT(err.Contains("1 qw 12.1"));
        }
    }

    static inline void CheckCurrentExceptionContains(const char* message) {
        TString err = CurrentExceptionMessage();
        SubstGlobal(err, '\\', '/'); // remove backslashes from path in message
        UNIT_ASSERT(err.Contains(message));
    }

    inline void TestRaise1() {
        try {
            Throw2DontMove();
            UNIT_ASSERT(false);
        } catch (...) {
            CheckCurrentExceptionContains("util/generic/yexception_ut.cpp:8: 1 qw 12.1");
        }
    }

    inline void TestException() {
        ythrow yexception() << "blablabla";
    }

    inline void TestLineInfo() {
        try {
            Throw1DontMove();
            UNIT_ASSERT(false);
        } catch (...) {
            CheckCurrentExceptionContains("util/generic/yexception_ut.cpp:4: blabla");

            throw;
        }
    }

    //! tests propagation of an exception through C code
    //! @note on some platforms, for example GCC on 32-bit Linux without -fexceptions option,
    //!       throwing an exception from a C++ callback through C code aborts program
    inline void TestMixedCode() {
        const int N = 26082009;
        try {
            TestCallback(&CallbackFun, N);
            UNIT_ASSERT(false);
        } catch (int i) {
            UNIT_ASSERT_VALUES_EQUAL(i, N);
        }
    }

    void TestMacroOverload() {
        try {
            Y_ENSURE(10 > 20);
        } catch (const yexception& e) {
            UNIT_ASSERT(e.AsStrBuf().Contains("10 > 20"));
        }

        try {
            Y_ENSURE(10 > 20, "exception message to search for");
        } catch (const yexception& e) {
            UNIT_ASSERT(e.AsStrBuf().Contains("exception message to search for"));
        }
    }
};

UNIT_TEST_SUITE_REGISTRATION(TExceptionTest);
