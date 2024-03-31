#include "split.h"

#include <library/unittest/registar.h>

#include <util/stream/output.h>
#include <util/charset/wide.h>
#include <util/datetime/cputimer.h>
#include <util/generic/maybe.h>

template <typename T>
static inline void OldSplit(char* pszBuf, T* pRes) {
    pRes->resize(0);
    pRes->push_back(pszBuf);
    for (char* pszData = pszBuf; *pszData; ++pszData) {
        if (*pszData == '\t') {
            *pszData = 0;
            pRes->push_back(pszData + 1);
        }
    }
}

template <class T1, class T2>
inline void Cmp(const T1& t1, const T2& t2) {
    try {
        UNIT_ASSERT_EQUAL(t1.size(), t2.size());
    } catch (...) {
        Print(t1);
        Cerr << "---------------" << Endl;
        Print(t2);

        throw;
    }

    auto i = t1.begin();
    auto j = t2.begin();

    for (; i != t1.end() && j != t2.end(); ++i, ++j) {
        try {
            UNIT_ASSERT_EQUAL(*i, *j);
        } catch (...) {
            Cerr << "(" << *i << ")->(" << *j << ")" << Endl;

            throw;
        }
    }
}

template <class T>
inline void Print(const T& t) {
    for (typename T::const_iterator i = t.begin(); i != t.end(); ++i) {
        Cerr << *i << Endl;
    }
}

template <template <typename> class TConsumer, typename TResult, typename I, typename TDelimiter>
void TestDelimiterOnString(TResult& good, I* str, const TDelimiter& delim) {
    TResult test;
    TConsumer<TResult> consumer(&test);
    SplitString(str, delim, consumer);
    Cmp(good, test);
    UNIT_ASSERT_EQUAL(good, test);
}

template <template <typename> class TConsumer, typename TResult, typename I, typename TDelimiter>
void TestDelimiterOnRange(TResult& good, I* b, I* e, const TDelimiter& delim) {
    TResult test;
    TConsumer<TResult> consumer(&test);
    SplitString(b, e, delim, consumer);
    Cmp(good, test);
    UNIT_ASSERT_EQUAL(good, test);
}

template <typename TConsumer, typename TResult, typename I>
void TestConsumerOnString(TResult& good, I* str, I* d) {
    TResult test;
    TContainerConsumer<TResult> consumer(&test);
    TConsumer tested(&consumer);
    TCharDelimiter<const I> delim(*d);
    SplitString(str, delim, tested);
    Cmp(good, test);
    UNIT_ASSERT_EQUAL(good, test);
}

template <typename TConsumer, typename TResult, typename I>
void TestConsumerOnRange(TResult& good, I* b, I* e, I* d) {
    TResult test;
    TContainerConsumer<TResult> consumer(&test);
    TConsumer tested(&consumer);
    TCharDelimiter<const I> delim(*d);
    SplitString(b, e, delim, tested);
    Cmp(good, test);
    UNIT_ASSERT_EQUAL(good, test);
}

using TStrokaConsumer = TContainerConsumer<yvector<TString>>;

void TestLimitingConsumerOnString(yvector<TString>& good, const char* str, const char* d, size_t n, const char* last) {
    yvector<TString> test;
    TStrokaConsumer consumer(&test);
    TLimitingConsumer<TStrokaConsumer, const char> limits(n, &consumer);
    TCharDelimiter<const char> delim(*d);
    SplitString(str, delim, limits);
    Cmp(good, test);
    UNIT_ASSERT_EQUAL(good, test);
    UNIT_ASSERT_EQUAL(TString(limits.Last), TString(last)); // Quite unobvious behaviour. Why the last token is not added to slave consumer?
}

void TestLimitingConsumerOnRange(yvector<TString>& good, const char* b, const char* e, const char* d, size_t n, const char* last) {
    yvector<TString> test;
    TStrokaConsumer consumer(&test);
    TLimitingConsumer<TStrokaConsumer, const char> limits(n, &consumer);
    TCharDelimiter<const char> delim(*d);
    SplitString(b, e, delim, limits);
    Cmp(good, test);
    UNIT_ASSERT_EQUAL(good, test);
    UNIT_ASSERT_EQUAL(TString(limits.Last), TString(last));
}

SIMPLE_UNIT_TEST_SUITE(SplitStringTest) {
    SIMPLE_UNIT_TEST(TestCharSingleDelimiter) {
        TString data("qw ab  qwabcab");
        TString canonic[] = {"qw", "ab", "", "qwabcab"};
        yvector<TString> good(canonic, canonic + 4);
        TCharDelimiter<const char> delim(' ');

        TestDelimiterOnString<TContainerConsumer>(good, ~data, delim);
        TestDelimiterOnRange<TContainerConsumer>(good, ~data, data.end(), delim);
    }

    SIMPLE_UNIT_TEST(TestWideSingleDelimiter) {
        TUtf16String data(ASCIIToWide("qw ab  qwabcab"));
        TUtf16String canonic[] = {ASCIIToWide("qw"), ASCIIToWide("ab"), TUtf16String(), ASCIIToWide("qwabcab")};
        yvector<TUtf16String> good(canonic, canonic + 4);
        TCharDelimiter<const TChar> delim(' ');

        TestDelimiterOnString<TContainerConsumer>(good, ~data, delim);
        TestDelimiterOnRange<TContainerConsumer>(good, ~data, data.end(), delim);
    }

    SIMPLE_UNIT_TEST(TestConvertToIntCharSingleDelimiter) {
        TString data("42 4242 -12345 0");
        i32 canonic[] = {42, 4242, -12345, 0};
        yvector<i32> good(canonic, canonic + 4);
        TCharDelimiter<const char> delim(' ');

        TestDelimiterOnString<TContainerConvertingConsumer>(good, ~data, delim);
        TestDelimiterOnRange<TContainerConvertingConsumer>(good, ~data, data.end(), delim);
    }

    SIMPLE_UNIT_TEST(TestConvertAndSplit) {
        TString data("42 4242 -12345 0");
        i32 canonic[] = {42, 4242, -12345, 0};
        yvector<i32> good(canonic, canonic + 4);
        TCharDelimiter<const char> delim(' ');

        yvector<i32> test;
        SplitConvertStringTo(TStringBuf(~data), STRINGBUF(" "), &test);
        Cmp(good, test);

        test.clear();
        good.pop_back();
        SplitConvertRangeTo(~data, ~data + data.size() - 2, " ", &test);
        Cmp(good, test);
    }

    SIMPLE_UNIT_TEST(TestConvertAndSplitSet) {
        TString data("42 4242 -12345 0 0 42");
        i32 canonic[] = {42, 4242, -12345, 0};
        yset<i32> good(canonic, canonic + 4);
        TCharDelimiter<const char> delim(' ');

        yset<i32> test;
        SplitConvertStringTo(TStringBuf(~data), STRINGBUF(" "), &test);
        Cmp(good, test);
    }

    SIMPLE_UNIT_TEST(TestSplitSet) {
        TString data("ab,bc,cd,cd,defgh");
        TString canonic[] = {"ab", "bc", "cd", "defgh"};
        yset<TString> good(canonic, canonic + 4);
        yset<TString> test;
        SplitStringTo(data, ',', &test);
        Cmp(good, test);
    }

    SIMPLE_UNIT_TEST(TestCharSkipEmty) {
        TString data("qw ab  qwabcab ");
        TString canonic[] = {"qw", "ab", "qwabcab"};
        yvector<TString> good(canonic, canonic + 3);

        TestConsumerOnString<TSkipEmptyTokens<TStrokaConsumer>>(good, ~data, " ");
        TestConsumerOnRange<TSkipEmptyTokens<TStrokaConsumer>>(good, ~data, data.end(), " ");
    }

    SIMPLE_UNIT_TEST(TestCharKeepDelimiters) {
        TString data("qw ab  qwabcab ");
        TString canonic[] = {"qw", " ", "ab", " ", "", " ", "qwabcab", " ", ""};
        yvector<TString> good(canonic, canonic + 9);

        TestConsumerOnString<TKeepDelimiters<TStrokaConsumer>>(good, ~data, " ");
        TestConsumerOnRange<TKeepDelimiters<TStrokaConsumer>>(good, ~data, data.end(), " ");
    }

    SIMPLE_UNIT_TEST(TestCharLimit) {
        TString data("qw ab  qwabcab ");
        TString canonic[] = {"qw", "ab"};
        yvector<TString> good(canonic, canonic + 2);

        TestLimitingConsumerOnString(good, ~data, " ", 3, " qwabcab ");
        TestLimitingConsumerOnRange(good, ~data, data.end(), " ", 3, " qwabcab ");
    }

    SIMPLE_UNIT_TEST(TestCharStringDelimiter) {
        TString data("qw ab qwababcab");
        TString canonic[] = {"qw ", " qw", "", "c", ""};
        yvector<TString> good(canonic, canonic + 5);
        TStringDelimiter<const char> delim("ab");

        TestDelimiterOnString<TContainerConsumer>(good, ~data, delim);
        TestDelimiterOnRange<TContainerConsumer>(good, ~data, data.end(), delim);
    }

    SIMPLE_UNIT_TEST(TestWideStringDelimiter) {
        TUtf16String data(ASCIIToWide("qw ab qwababcab"));
        TUtf16String canonic[] = {ASCIIToWide("qw "), ASCIIToWide(" qw"), TUtf16String(), ASCIIToWide("c"), TUtf16String()};
        yvector<TUtf16String> good(canonic, canonic + 5);
        TUtf16String wideDelim(ASCIIToWide("ab"));
        TStringDelimiter<const TChar> delim(~wideDelim);

        TestDelimiterOnString<TContainerConsumer>(good, ~data, delim);
        TestDelimiterOnRange<TContainerConsumer>(good, ~data, data.end(), delim);
    }

    SIMPLE_UNIT_TEST(TestCharSetDelimiter) {
        TString data("qw ab qwababccab");
        TString canonic[] = {"q", " ab q", "abab", "", "ab"};
        yvector<TString> good(canonic, canonic + 5);
        TSetDelimiter<const char> delim("wc");

        TestDelimiterOnString<TContainerConsumer>(good, ~data, delim);
        TestDelimiterOnRange<TContainerConsumer>(good, ~data, data.end(), delim);
    }

    SIMPLE_UNIT_TEST(TestWideSetDelimiter) {
        TUtf16String data(ASCIIToWide("qw ab qwababccab"));
        TUtf16String canonic[] = {ASCIIToWide("q"), ASCIIToWide(" ab q"), ASCIIToWide("abab"), TUtf16String(), ASCIIToWide("ab")};
        yvector<TUtf16String> good(canonic, canonic + 5);
        TUtf16String wideDelim(ASCIIToWide("wc"));
        TSetDelimiter<const TChar> delim(~wideDelim);

        TestDelimiterOnString<TContainerConsumer>(good, ~data, delim);
    }

    SIMPLE_UNIT_TEST(TestWideSetDelimiterRange) {
        TUtf16String data(ASCIIToWide("qw ab qwababccab"));
        TUtf16String canonic[] = {ASCIIToWide("q"), ASCIIToWide(" ab q"), ASCIIToWide("abab"), TUtf16String(), ASCIIToWide("ab")};
        yvector<TUtf16String> good(1);
        TUtf16String wideDelim(ASCIIToWide("wc"));
        TSetDelimiter<const TChar> delim(~wideDelim);

        yvector<TUtf16String> test;
        TContainerConsumer<yvector<TUtf16String>> consumer(&test);
        SplitString(~data, ~data, delim, consumer); // Empty string is still inserted into consumer
        Cmp(good, test);

        good.assign(canonic, canonic + 4);
        good.push_back(TUtf16String());
        test.clear();
        SplitString(~data, data.end() - 2, delim, consumer);
        Cmp(good, test);
    }

    SIMPLE_UNIT_TEST(TestSplit) {
        TString data("qw ab qwababcab");
        TString canonic[] = {"qw ", " qw", "c"};
        yvector<TString> good(canonic, canonic + 3);
        TString delim = "ab";
        yvector<TString> test;
        Split(data, delim, test);
        Cmp(good, test);

        yvector<TStringBuf> test1;
        Split(data, ~delim, test1);
        Cmp(good, test1);
    }

    SIMPLE_UNIT_TEST(ConvenientSplitTest) {
        TString data("abc 22 33.5 xyz");
        TString str;
        int num1 = 0;
        double num2 = 0;
        TStringBuf strBuf;
        Split(data, ' ', str, num1, num2, strBuf);
        UNIT_ASSERT_VALUES_EQUAL(str, "abc");
        UNIT_ASSERT_VALUES_EQUAL(num1, 22);
        UNIT_ASSERT_VALUES_EQUAL(num2, 33.5);
        UNIT_ASSERT_VALUES_EQUAL(strBuf, "xyz");
    }

    SIMPLE_UNIT_TEST(ConvenientSplitTestWithMaybe) {
        TString data("abc 42");
        TString str;
        TMaybe<double> num2 = 1;
        TMaybe<double> maybe = 1;

        Split(data, ' ', str, num2, maybe);

        UNIT_ASSERT_VALUES_EQUAL(str, "abc");
        UNIT_ASSERT_VALUES_EQUAL(*num2, 42);
        UNIT_ASSERT(!maybe);
    }

    SIMPLE_UNIT_TEST(ConvenientSplitTestExceptions) {
        TString data("abc 22 33");
        TString s1, s2, s3, s4;

        UNIT_ASSERT_EXCEPTION(Split(data, ' ', s1, s2), yexception);
        UNIT_ASSERT_NO_EXCEPTION(Split(data, ' ', s1, s2, s3));
        UNIT_ASSERT_EXCEPTION(Split(data, ' ', s1, s2, s3, s4), yexception);
    }

    SIMPLE_UNIT_TEST(ConvenientSplitTestMaybeExceptions) {
        TString data("abc 22 33");
        TString s1, s2;
        TMaybe<TString> m1, m2;

        UNIT_ASSERT_EXCEPTION(Split(data, ' ', s1, m1), yexception);
        UNIT_ASSERT_EXCEPTION(Split(data, ' ', m1, m2), yexception);
        UNIT_ASSERT_NO_EXCEPTION(Split(data, ' ', s1, s2, m1));

        UNIT_ASSERT_NO_EXCEPTION(Split(data, ' ', s1, s2, m1, m2));
        UNIT_ASSERT_EXCEPTION(Split(data, ' ', m1, m2, s1, s2), yexception);

        UNIT_ASSERT_NO_EXCEPTION(Split(data, ' ', s1, s2, m1, m2, m1, m1, m1, m1));
        UNIT_ASSERT_EXCEPTION(Split(data, ' ', s1, s2, m1, m2, m1, m1, m1, m1, s1), yexception);
    }
}
