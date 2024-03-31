#include "strbuf.h"

#include <library/unittest/registar.h>

SIMPLE_UNIT_TEST_SUITE(TStrBufTest) {
    SIMPLE_UNIT_TEST(TestConstructors) {
        TStringBuf str("qwerty");

        UNIT_ASSERT_EQUAL(*~str, 'q');
        UNIT_ASSERT_EQUAL(+str, 6);

        TStringBuf str1(STRINGBUF("qwe\0rty"));
        TStringBuf str2(str1.data());
        UNIT_ASSERT_VALUES_UNEQUAL(str1, str2);
        UNIT_ASSERT_VALUES_EQUAL(+str1, 7);
        UNIT_ASSERT_VALUES_EQUAL(+str2, 3);
    }

    SIMPLE_UNIT_TEST(TestConstExpr) {
        static constexpr TStringBuf str1("qwe\0rty", 7);
        static constexpr TStringBuf str2(str1.Data(), str1.size());
        static constexpr TStringBuf str3 = STRINGBUF("qwe\0rty");

        UNIT_ASSERT_VALUES_EQUAL(str1.Size(), 7);

        UNIT_ASSERT_VALUES_EQUAL(str1, str2);
        UNIT_ASSERT_VALUES_EQUAL(str2, str3);
        UNIT_ASSERT_VALUES_EQUAL(str1, str3);
    }

    SIMPLE_UNIT_TEST(TestAfter) {
        TStringBuf str("qwerty");

        UNIT_ASSERT_EQUAL(str.After('w'), STRINGBUF("erty"));
        UNIT_ASSERT_EQUAL(str.After('x'), STRINGBUF("qwerty"));
        UNIT_ASSERT_EQUAL(str.After('y'), TStringBuf());
        UNIT_ASSERT_STRINGS_EQUAL(str.After('='), str);
    }

    SIMPLE_UNIT_TEST(TestBefore) {
        TStringBuf str("qwerty");

        UNIT_ASSERT_EQUAL(str.Before('w'), STRINGBUF("q"));
        UNIT_ASSERT_EQUAL(str.Before('x'), STRINGBUF("qwerty"));
        UNIT_ASSERT_EQUAL(str.Before('y'), STRINGBUF("qwert"));
        UNIT_ASSERT_EQUAL(str.Before('q'), TStringBuf());
    }

    SIMPLE_UNIT_TEST(TestRAfterBefore) {
        TStringBuf str("a/b/c");
        UNIT_ASSERT_STRINGS_EQUAL(str.RAfter('/'), "c");
        UNIT_ASSERT_STRINGS_EQUAL(str.RAfter('_'), str);
        UNIT_ASSERT_STRINGS_EQUAL(str.RAfter('a'), "/b/c");
        UNIT_ASSERT_STRINGS_EQUAL(str.RBefore('/'), "a/b");
        UNIT_ASSERT_STRINGS_EQUAL(str.RBefore('_'), str);
        UNIT_ASSERT_STRINGS_EQUAL(str.RBefore('a'), "");
    }

    SIMPLE_UNIT_TEST(TestAfterPrefix) {
        TStringBuf str("cat_dog");

        TStringBuf r = "the_same";
        UNIT_ASSERT(!str.AfterPrefix("dog", r));
        UNIT_ASSERT_EQUAL(r, "the_same");
        UNIT_ASSERT(str.AfterPrefix("cat_", r));
        UNIT_ASSERT_EQUAL(r, "dog");

        //example:
        str = "http://ya.ru";
        if (str.AfterPrefix("http://", r)) {
            UNIT_ASSERT_EQUAL(r, "ya.ru");
        }

        // SkipPrefix()
        TStringBuf a = "abcdef";
        UNIT_ASSERT(a.SkipPrefix("a") && a == "bcdef");
        UNIT_ASSERT(a.SkipPrefix("bc") && a == "def");
        UNIT_ASSERT(a.SkipPrefix("") && a == "def");
        UNIT_ASSERT(!a.SkipPrefix("xyz") && a == "def");
        UNIT_ASSERT(!a.SkipPrefix("defg") && a == "def");
        UNIT_ASSERT(a.SkipPrefix("def") && a == "");
        UNIT_ASSERT(a.SkipPrefix("") && a == "");
        UNIT_ASSERT(!a.SkipPrefix("def") && a == "");
    }

    SIMPLE_UNIT_TEST(TestBeforeSuffix) {
        TStringBuf str("cat_dog");

        TStringBuf r = "the_same";
        UNIT_ASSERT(!str.BeforeSuffix("cat", r));
        UNIT_ASSERT_EQUAL(r, "the_same");
        UNIT_ASSERT(str.BeforeSuffix("_dog", r));
        UNIT_ASSERT_EQUAL(r, "cat");

        //example:
        str = "maps.yandex.com.ua";
        if (str.BeforeSuffix(".ru", r)) {
            UNIT_ASSERT_EQUAL(r, "maps.yandex");
        }

        // ChopSuffix()
        TStringBuf a = "abcdef";
        UNIT_ASSERT(a.ChopSuffix("f") && a == "abcde");
        UNIT_ASSERT(a.ChopSuffix("de") && a == "abc");
        UNIT_ASSERT(a.ChopSuffix("") && a == "abc");
        UNIT_ASSERT(!a.ChopSuffix("xyz") && a == "abc");
        UNIT_ASSERT(!a.ChopSuffix("abcd") && a == "abc");
        UNIT_ASSERT(a.ChopSuffix("abc") && a == "");
        UNIT_ASSERT(a.ChopSuffix("") && a == "");
        UNIT_ASSERT(!a.ChopSuffix("abc") && a == "");
    }

    SIMPLE_UNIT_TEST(TestEmpty) {
        UNIT_ASSERT(TStringBuf().Empty());
        UNIT_ASSERT(!STRINGBUF("q").Empty());
    }

    SIMPLE_UNIT_TEST(TestShift) {
        TStringBuf qw("qwerty");
        TStringBuf str;

        str = qw;
        str.Chop(10);
        UNIT_ASSERT(str.Empty());

        str = qw;
        UNIT_ASSERT_EQUAL(str.SubStr(1), str + 1);
        UNIT_ASSERT_EQUAL(str + 2, STRINGBUF("erty"));
        UNIT_ASSERT_EQUAL(str += 3, qw.SubStr(3));
        str.Chop(1);
        UNIT_ASSERT_EQUAL(str, STRINGBUF("rt"));
    }

    SIMPLE_UNIT_TEST(TestSplit) {
        TStringBuf qw("qwerty");
        TStringBuf lt, rt;

        rt = qw;
        lt = rt.NextTok('r');
        UNIT_ASSERT_EQUAL(lt, STRINGBUF("qwe"));
        UNIT_ASSERT_EQUAL(rt, STRINGBUF("ty"));

        lt = qw;
        rt = lt.SplitOff('r');
        UNIT_ASSERT_EQUAL(lt, STRINGBUF("qwe"));
        UNIT_ASSERT_EQUAL(rt, STRINGBUF("ty"));

        rt = qw;
        lt = rt.NextTok('r');
        TStringBuf ty = rt.NextTok('r'); // no 'r' in "ty"
        UNIT_ASSERT_EQUAL(rt.Size(), 0);
        UNIT_ASSERT_EQUAL(ty, STRINGBUF("ty"));
    }

    SIMPLE_UNIT_TEST(TestNextTok) {
        TStringBuf buf("12q45q");
        TStringBuf tok;

        UNIT_ASSERT(buf.NextTok('q', tok) && tok == "12");
        UNIT_ASSERT(buf.NextTok('q', tok) && tok == "45");
        UNIT_ASSERT(!buf.NextTok('q', tok));
    }

    SIMPLE_UNIT_TEST(TestNextStringTok) {
        TStringBuf buf1("a@@b@@c");
        UNIT_ASSERT_EQUAL(buf1.NextTok("@@"), STRINGBUF("a"));
        UNIT_ASSERT_EQUAL(buf1.NextTok("@@"), STRINGBUF("b"));
        UNIT_ASSERT_EQUAL(buf1.NextTok("@@"), STRINGBUF("c"));
        UNIT_ASSERT_EQUAL(buf1, TStringBuf());

        TStringBuf buf2("a@@b@@c");
        UNIT_ASSERT_EQUAL(buf2.RNextTok("@@"), STRINGBUF("c"));
        UNIT_ASSERT_EQUAL(buf2.RNextTok("@@"), STRINGBUF("b"));
        UNIT_ASSERT_EQUAL(buf2.RNextTok("@@"), STRINGBUF("a"));
        UNIT_ASSERT_EQUAL(buf2, TStringBuf());
    }

    SIMPLE_UNIT_TEST(TestReadLine) {
        TStringBuf buf("12\n45\r\n\r\n23");
        TStringBuf tok;

        UNIT_ASSERT(buf.ReadLine(tok) && tok == "12");
        UNIT_ASSERT(buf.ReadLine(tok) && tok == "45");
        UNIT_ASSERT(buf.ReadLine(tok) && tok == "");
        UNIT_ASSERT(buf.ReadLine(tok) && tok == "23");
        UNIT_ASSERT(!buf.ReadLine(tok));
    }

    SIMPLE_UNIT_TEST(TestRFind) {
        TStringBuf buf1 = STRINGBUF("123123456");
        UNIT_ASSERT_EQUAL(buf1.rfind('3'), 5);
        UNIT_ASSERT_EQUAL(buf1.rfind('4'), 6);
        UNIT_ASSERT_EQUAL(buf1.rfind('7'), TStringBuf::npos);

        TStringBuf buf2;
        UNIT_ASSERT_EQUAL(buf2.rfind('3'), TStringBuf::npos);

        TStringBuf buf3 = TStringBuf("123123456", 6);
        UNIT_ASSERT_EQUAL(buf3.rfind('3'), 5);
        UNIT_ASSERT_EQUAL(buf3.rfind('4'), TStringBuf::npos);
        UNIT_ASSERT_EQUAL(buf3.rfind('7'), TStringBuf::npos);

        TStringBuf buf4 = TStringBuf("123123456", 5);
        UNIT_ASSERT_EQUAL(buf4.rfind('3'), 2);
    }

    SIMPLE_UNIT_TEST(TestRNextTok) {
        TStringBuf buf1("a.b.c");
        UNIT_ASSERT_EQUAL(buf1.RNextTok('.'), STRINGBUF("c"));
        UNIT_ASSERT_EQUAL(buf1, STRINGBUF("a.b"));

        TStringBuf buf2("a");
        UNIT_ASSERT_EQUAL(buf2.RNextTok('.'), STRINGBUF("a"));
        UNIT_ASSERT_EQUAL(buf2, TStringBuf());

        TStringBuf buf3("ab cd ef"), tok;
        UNIT_ASSERT(buf3.RNextTok(' ', tok) && tok == "ef" && buf3 == "ab cd");
        UNIT_ASSERT(buf3.RNextTok(' ', tok) && tok == "cd" && buf3 == "ab");
        UNIT_ASSERT(buf3.RNextTok(' ', tok) && tok == "ab" && buf3 == "");
        UNIT_ASSERT(!buf3.RNextTok(' ', tok) && tok == "ab" && buf3 == ""); // not modified
    }

    SIMPLE_UNIT_TEST(TestRSplitOff) {
        TStringBuf buf1("a.b.c");
        UNIT_ASSERT_EQUAL(buf1.RSplitOff('.'), STRINGBUF("a.b"));
        UNIT_ASSERT_EQUAL(buf1, STRINGBUF("c"));

        TStringBuf buf2("a");
        UNIT_ASSERT_EQUAL(buf2.RSplitOff('.'), TStringBuf());
        UNIT_ASSERT_EQUAL(buf2, STRINGBUF("a"));
    }

    SIMPLE_UNIT_TEST(TestCBeginCEnd) {
        const char helloThere[] = "Hello there";
        TStringBuf s{helloThere};

        size_t index = 0;
        for (auto it = s.cbegin(); s.cend() != it; ++it, ++index) {
            UNIT_ASSERT_VALUES_EQUAL(helloThere[index], *it);
        }
    }

    SIMPLE_UNIT_TEST(TestSplitOnAt) {
        TStringBuf s = "abcabc";
        TStringBuf l, r;

        size_t pos = s.find('a');
        UNIT_ASSERT(s.TrySplitOn(pos, l, r));
        UNIT_ASSERT(l == "" && r == "bcabc");
        UNIT_ASSERT(s.TrySplitAt(pos, l, r));
        UNIT_ASSERT(l == "" && r == "abcabc");

        pos = s.find("ca");
        UNIT_ASSERT(s.TrySplitOn(pos, l, r));
        UNIT_ASSERT(l == "ab" && r == "abc");
        UNIT_ASSERT(s.TrySplitOn(pos, l, r, 2));
        UNIT_ASSERT(l == "ab" && r == "bc");
        UNIT_ASSERT(s.TrySplitAt(pos, l, r));
        UNIT_ASSERT(l == "ab" && r == "cabc");

        // out of range
        pos = 100500;
        UNIT_ASSERT(s.TrySplitOn(pos, l, r)); // still true
        UNIT_ASSERT(l == "abcabc" && r == "");
        l = "111";
        r = "222";
        UNIT_ASSERT(s.TrySplitAt(pos, l, r)); // still true
        UNIT_ASSERT(l == "abcabc" && r == "");

        // npos
        pos = s.find("missing");
        l = "111";
        r = "222";
        UNIT_ASSERT(!s.TrySplitOn(pos, l, r));
        UNIT_ASSERT(l == "111" && r == "222"); // not modified
        s.SplitOn(pos, l, r);
        UNIT_ASSERT(l == "abcabc" && r == ""); // modified

        l = "111";
        r = "222";
        UNIT_ASSERT(!s.TrySplitAt(pos, l, r));
        UNIT_ASSERT(l == "111" && r == "222"); // not modified
        s.SplitAt(pos, l, r);
        UNIT_ASSERT(l == "abcabc" && r == ""); // modified
    }

    template <class T>
    void PassByConstReference(const T& val) {
        // In https://st.yandex-team.ru/IGNIETFERRO-294 was assumed that `const char[]` types are compile time strings
        // and that CharTraits::Length mmay not be called for them. Unfortunately that is not true, `char[]` types
        // are easily converted to `const char[]` if they are passed to a function accepting `const T&`.
        UNIT_ASSERT(TStringBuf(val).size() == 5);
    }

    SIMPLE_UNIT_TEST(TestPassingArraysByConstReference) {
        char data[] = "Hello\0word";
        PassByConstReference(data);
    }
}
