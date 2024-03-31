#include "cast.h"

#include <library/unittest/registar.h>

class TGenericCastsTest: public TTestBase {
    UNIT_TEST_SUITE(TGenericCastsTest);
    UNIT_TEST(TestReinterpret)
    UNIT_TEST(TestVerifyDynamicCast)
    UNIT_TEST(TestIntegralCast)
    UNIT_TEST(TestEnumCast)
    UNIT_TEST(TestToUnderlying)
    UNIT_TEST_SUITE_END();

private:
    inline void TestReinterpret() {
        static char tmp[2];

        UNIT_ASSERT_EQUAL(reinterpret_cast<short*>(tmp), ReinterpretCast<short*>(tmp));
    }

    struct TAaa {
        virtual ~TAaa() = default;
    };
    struct TBbb: public TAaa {};

    inline void TestVerifyDynamicCast() {
        TBbb bbb;
        TAaa* aaa = &bbb;
        TAaa* aaa2 = VerifyDynamicCast<TBbb*>(aaa);
        UNIT_ASSERT(aaa == aaa2);
    }

    void TestIntegralCast() {
        UNIT_ASSERT_EXCEPTION(SafeIntegerCast<ui32>(-5), TBadCastException);
        UNIT_ASSERT_EXCEPTION(SafeIntegerCast<ui16>(static_cast<i32>(Max<ui16>() + 10)), TBadCastException);
        UNIT_ASSERT_EXCEPTION(SafeIntegerCast<ui16>(static_cast<ui32>(Max<ui16>() + 10)), TBadCastException);
    }

    inline void TestEnumCast() {
        enum A {
            AM1 = -1
        };

        enum B : int {
            BM1 = -1
        };

        enum class C : unsigned short {
            CM1 = 1
        };

        UNIT_ASSERT_EXCEPTION(SafeIntegerCast<unsigned int>(AM1), TBadCastException);
        UNIT_ASSERT_EXCEPTION(SafeIntegerCast<unsigned int>(BM1), TBadCastException);
        UNIT_ASSERT_EXCEPTION(SafeIntegerCast<C>(AM1), TBadCastException);
        UNIT_ASSERT_EXCEPTION(static_cast<int>(SafeIntegerCast<C>(BM1)), TBadCastException);
        UNIT_ASSERT(SafeIntegerCast<A>(BM1) == AM1);
        UNIT_ASSERT(SafeIntegerCast<B>(AM1) == BM1);
        UNIT_ASSERT(SafeIntegerCast<A>(C::CM1) == 1);
        UNIT_ASSERT(SafeIntegerCast<B>(C::CM1) == 1);
        UNIT_ASSERT(SafeIntegerCast<A>(-1) == AM1);
        UNIT_ASSERT(SafeIntegerCast<B>(-1) == BM1);
        UNIT_ASSERT(SafeIntegerCast<C>(1) == C::CM1);
    }

    void TestToUnderlying() {
        enum A {
            AM1 = -1
        };

        enum B : int {
            BM1 = -1
        };

        enum class C : unsigned short {
            CM1 = 1
        };

        static_assert(static_cast<std::underlying_type_t<A>>(AM1) == ToUnderlying(AM1), "");
        static_assert(static_cast<std::underlying_type_t<B>>(BM1) == ToUnderlying(BM1), "");
        static_assert(static_cast<std::underlying_type_t<C>>(C::CM1) == ToUnderlying(C::CM1), "");

        static_assert(std::is_same<std::underlying_type_t<A>, decltype(ToUnderlying(AM1))>::value, "");
        static_assert(std::is_same<std::underlying_type_t<B>, decltype(ToUnderlying(BM1))>::value, "");
        static_assert(std::is_same<std::underlying_type_t<C>, decltype(ToUnderlying(C::CM1))>::value, "");

        UNIT_ASSERT_VALUES_EQUAL(static_cast<std::underlying_type_t<A>>(AM1), ToUnderlying(AM1));
        UNIT_ASSERT_VALUES_EQUAL(static_cast<std::underlying_type_t<B>>(BM1), ToUnderlying(BM1));
        UNIT_ASSERT_VALUES_EQUAL(static_cast<std::underlying_type_t<C>>(C::CM1), ToUnderlying(C::CM1));
    }
};

UNIT_TEST_SUITE_REGISTRATION(TGenericCastsTest);
