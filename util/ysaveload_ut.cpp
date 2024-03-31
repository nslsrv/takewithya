#include "ysaveload.h"

#include <library/unittest/registar.h>

#include <util/memory/pool.h>
#include <util/stream/buffer.h>
#include <util/memory/blob.h>
#include <util/generic/list.h>
#include <util/generic/map.h>
#include <util/generic/set.h>
#include <util/generic/hash.h>
#include <util/generic/deque.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/buffer.h>
#include <util/generic/hash_set.h>
#include <util/generic/maybe.h>

static inline char* AllocateFromPool(TMemoryPool& pool, size_t len) {
    return (char*)pool.Allocate(len);
}

class TSaveLoadTest: public TTestBase {
    UNIT_TEST_SUITE(TSaveLoadTest);
    UNIT_TEST(TestSaveLoad)
    UNIT_TEST(TestNewStyle)
    UNIT_TEST(TestNewNewStyle)
    UNIT_TEST(TestList)
    UNIT_TEST_SUITE_END();

    struct TSaveHelper {
        inline void Save(TOutputStream* o) const {
            o->Write("qwerty", 7);
        }

        inline void Load(TInputStream* i) {
            char buf[7];

            UNIT_ASSERT_EQUAL(i->Load(buf, 7), 7);
            UNIT_ASSERT_EQUAL(strcmp(buf, "qwerty"), 0);
        }
    };

    struct TNewStyleSaveHelper {
        template <class S>
        inline void SaveLoad(S* s) {
            ::SaveLoad(s, Str);
        }

        TString Str;
    };

    struct TNewNewStyleHelper {
        TString Str;
        ui32 Int;

        Y_SAVELOAD_DEFINE(Str, Int)
    };

private:
    inline void TestNewNewStyle() {
        TString ss;

        {
            TNewNewStyleHelper h;

            h.Str = "qw";
            h.Int = 42;

            TStringOutput so(ss);

            ::Save(&so, h);
        }

        {
            TNewNewStyleHelper h;

            TStringInput si(ss);
            ::Load(&si, h);

            UNIT_ASSERT_EQUAL(h.Str, "qw");
            UNIT_ASSERT_EQUAL(h.Int, 42);
        }
    }

    inline void TestNewStyle() {
        TString ss;

        {
            TNewStyleSaveHelper sh;
            sh.Str = "qwerty";
            TStringOutput so(ss);
            SaveLoad(&so, sh);
        }

        {
            TNewStyleSaveHelper sh;
            TStringInput si(ss);
            SaveLoad(&si, sh);

            UNIT_ASSERT_EQUAL(sh.Str, "qwerty");
        }
    }

    inline void TestSaveLoad() {
        TBufferStream S_;

        //save part
        {
            Save(&S_, (ui8)1);
            Save(&S_, (ui16)2);
            Save(&S_, (ui32)3);
            Save(&S_, (ui64)4);
        }

        {
            yvector<ui16> vec;

            vec.push_back((ui16)1);
            vec.push_back((ui16)2);
            vec.push_back((ui16)4);

            Save(&S_, vec);
        }

        {
            ymap<ui16, ui32> map;

            map[(ui16)1] = 2;
            map[(ui16)2] = 3;
            map[(ui16)3] = 4;

            Save(&S_, map);
        }

        {
            ymultimap<ui16, ui32> multimap;

            multimap.emplace((ui16)1, 2);
            multimap.emplace((ui16)2, 3);
            multimap.emplace((ui16)2, 4);
            multimap.emplace((ui16)2, 5);
            multimap.emplace((ui16)3, 6);

            Save(&S_, multimap);
        }

        {
            TSaveHelper helper;

            Save(&S_, helper);
        }

        {
            TString val("123456");

            Save(&S_, val);
        }

        {
            TBuffer buf;

            buf.Append("asdf", 4);
            Save(&S_, buf);
        }

        {
            yvector<const char*> vec;

            vec.push_back("1");
            vec.push_back("123");
            vec.push_back("4567");

            Save(&S_, vec);
        }

        {
            ydeque<ui16> deq;

            deq.push_back(1);
            deq.push_back(2);
            deq.push_back(4);
            deq.push_back(5);

            Save(&S_, deq);
        }

        {
            TMaybe<size_t> h(10);
            Save(&S_, h);
        }

        {
            TMaybe<size_t> h(20);
            Save(&S_, h);
        }

        {
            TMaybe<size_t> h;
            Save(&S_, h);
        }

        {
            TMaybe<size_t> h;
            Save(&S_, h);
        }

        {
            yhash_mm<TString, int> mm;

            mm.insert({"one", 1});
            mm.insert({"two", 2});
            mm.insert({"two", 22});

            Save(&S_, mm);
        }

        //load part
        {
            ui8 val;

            Load(&S_, val);
            UNIT_ASSERT_EQUAL(val, 1);
        }

        {
            ui16 val;

            Load(&S_, val);
            UNIT_ASSERT_EQUAL(val, 2);
        }

        {
            ui32 val;

            Load(&S_, val);
            UNIT_ASSERT_EQUAL(val, 3);
        }

        {
            ui64 val;

            Load(&S_, val);
            UNIT_ASSERT_EQUAL(val, 4);
        }

        {
            yvector<ui16> vec;

            Load(&S_, vec);
            UNIT_ASSERT_EQUAL(+vec, 3);
            UNIT_ASSERT_EQUAL(vec[0], 1);
            UNIT_ASSERT_EQUAL(vec[1], 2);
            UNIT_ASSERT_EQUAL(vec[2], 4);
        }

        {
            ymap<ui16, ui32> map;

            Load(&S_, map);
            UNIT_ASSERT_EQUAL(map.size(), 3);
            UNIT_ASSERT_EQUAL(map[(ui16)1], 2);
            UNIT_ASSERT_EQUAL(map[(ui16)2], 3);
            UNIT_ASSERT_EQUAL(map[(ui16)3], 4);
        }

        {
            ymultimap<ui16, ui32> multimap;

            Load(&S_, multimap);
            UNIT_ASSERT_EQUAL(multimap.size(), 5);
            UNIT_ASSERT_EQUAL(multimap.find((ui16)1)->second, 2);
            UNIT_ASSERT_EQUAL(multimap.find((ui16)3)->second, 6);

            yhash_set<ui32> values;
            auto range = multimap.equal_range((ui16)2);
            for (auto i = range.first; i != range.second; ++i) {
                values.insert(i->second);
            }

            UNIT_ASSERT_EQUAL(values.size(), 3);
            UNIT_ASSERT_EQUAL(values.has(3), true);
            UNIT_ASSERT_EQUAL(values.has(4), true);
            UNIT_ASSERT_EQUAL(values.has(5), true);
        }

        {
            TSaveHelper helper;

            Load(&S_, helper);
        }

        {
            TString val;

            Load(&S_, val);
            UNIT_ASSERT_EQUAL(val, "123456");
        }

        {
            TBuffer buf;

            Load(&S_, buf);
            UNIT_ASSERT_EQUAL(+buf, 4);
            UNIT_ASSERT_EQUAL(memcmp(~buf, "asdf", 4), 0);
        }

        {
            yvector<const char*> vec;
            TMemoryPool pool(1024);

            Load(&S_, vec, pool);

            UNIT_ASSERT_EQUAL(+vec, 3);
            UNIT_ASSERT_EQUAL(vec[0], TString("1"));
            UNIT_ASSERT_EQUAL(vec[1], TString("123"));
            UNIT_ASSERT_EQUAL(vec[2], TString("4567"));
        }

        {
            ydeque<ui16> deq;

            Load(&S_, deq);

            UNIT_ASSERT_EQUAL(+deq, 4);
            UNIT_ASSERT_EQUAL(deq[0], 1);
            UNIT_ASSERT_EQUAL(deq[1], 2);
            UNIT_ASSERT_EQUAL(deq[2], 4);
            UNIT_ASSERT_EQUAL(deq[3], 5);
        }

        {
            TMaybe<size_t> h(5);
            Load(&S_, h);
            UNIT_ASSERT_EQUAL(*h, 10);
        }

        {
            TMaybe<size_t> h;
            Load(&S_, h);
            UNIT_ASSERT_EQUAL(*h, 20);
        }

        {
            TMaybe<size_t> h;
            UNIT_ASSERT(!h);
            Load(&S_, h);
            UNIT_ASSERT(!h);
        }

        {
            TMaybe<size_t> h(7);
            UNIT_ASSERT(!!h);
            Load(&S_, h);
            UNIT_ASSERT(!h);
        }

        {
            yhash_mm<TString, int> mm;

            Load(&S_, mm);

            UNIT_ASSERT_EQUAL(mm.size(), 3);
            UNIT_ASSERT_EQUAL(mm.count("one"), 1);
            auto oneIter = mm.equal_range("one").first;
            UNIT_ASSERT_EQUAL(oneIter->second, 1);
            UNIT_ASSERT_EQUAL(mm.count("two"), 2);
            auto twoIter = mm.equal_range("two").first;
            UNIT_ASSERT_EQUAL(twoIter->second, 2);
            UNIT_ASSERT_EQUAL((++twoIter)->second, 22);
        }
    }

    void TestList() {
        TBufferStream s;

        ylist<int> list = {0, 1, 10};
        Save(&s, list);

        list.clear();
        Load(&s, list);

        UNIT_ASSERT_VALUES_EQUAL(list.size(), 3);
        UNIT_ASSERT_VALUES_EQUAL(*std::next(list.begin(), 0), 0);
        UNIT_ASSERT_VALUES_EQUAL(*std::next(list.begin(), 1), 1);
        UNIT_ASSERT_VALUES_EQUAL(*std::next(list.begin(), 2), 10);
    }
};

UNIT_TEST_SUITE_REGISTRATION(TSaveLoadTest);
