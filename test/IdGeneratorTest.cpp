#include <gtest/gtest.h>

#ifdef GTEST_IS_THREADSAFE
#pragma message("pthread is available")
#else
#pragma message("pthread is NOT available")
#endif

#include "General.h"

#include "Common.h"
#include "Database.h"
#include "Logger.h"

#include <chrono>
#include <thread>
#include <vector>

namespace PapierMache::DbStuff {

    class IdGeneratorTest : public ::testing::Test {
    protected:
        IdGeneratorTest()
        {
        }

        ~IdGeneratorTest() override
        {
        }

        void SetUp() override
        {
        }

        void TearDown() override
        {
        }
    };

    TEST_F(IdGeneratorTest, getId_001)
    {
        IdGenerator<short> idg{0};
        ASSERT_EQ(0, idg.getId());
        ASSERT_EQ(1, idg.getId());
        ASSERT_EQ(2, idg.getId());
    }

    TEST_F(IdGeneratorTest, getId_002)
    {
        IdGenerator<short> idg{10};
        ASSERT_EQ(10, idg.getId());
        ASSERT_EQ(11, idg.getId());
        ASSERT_EQ(12, idg.getId());
    }

    TEST_F(IdGeneratorTest, getId_003)
    {
        IdGenerator<char> idg{0};
        for (char c = 0; c < 120; ++c) {
            idg.getId();
        }
        ASSERT_EQ(120, idg.getId());
        ASSERT_EQ(121, idg.getId());
        ASSERT_EQ(122, idg.getId());
        ASSERT_EQ(123, idg.getId());
        ASSERT_EQ(124, idg.getId());
        ASSERT_EQ(125, idg.getId());
        ASSERT_EQ(126, idg.getId());

        char id = -1;
        try {
            id = idg.getId();
            ASSERT_FALSE(true);
        }
        catch (const std::exception &e) {
            LOG << e.what();
        }
        ASSERT_EQ(-1, id);
    }

    TEST_F(IdGeneratorTest, getId_004)
    {
        IdGenerator<char> idg{0, true};
        for (char c = 0; c < 120; ++c) {
            idg.getId();
        }
        ASSERT_EQ(120, idg.getId());
        ASSERT_EQ(121, idg.getId());
        ASSERT_EQ(122, idg.getId());
        ASSERT_EQ(123, idg.getId());
        ASSERT_EQ(124, idg.getId());
        ASSERT_EQ(125, idg.getId());
        ASSERT_EQ(126, idg.getId());
        char id = -1;
        try {
            id = idg.getId();
            ASSERT_FALSE(true);
        }
        catch (const std::exception &e) {
            LOG << e.what();
        }
        ASSERT_EQ(-1, id);
    }

    TEST_F(IdGeneratorTest, getId_005)
    {
        IdGenerator<char> idg{0, true};
        for (char c = 0; c < 120; ++c) {
            idg.getId();
        }
        ASSERT_EQ(120, idg.getId());
        ASSERT_EQ(121, idg.getId());
        ASSERT_EQ(122, idg.getId());
        ASSERT_EQ(123, idg.getId());
        ASSERT_EQ(124, idg.getId());
        ASSERT_EQ(125, idg.getId());
        ASSERT_EQ(126, idg.getId());

        idg.release(3);
        idg.release(122);
        idg.release(125);
        char id = -1;
        try {
            ASSERT_EQ(3, idg.getId());
            ASSERT_EQ(122, idg.getId());
            ASSERT_EQ(125, idg.getId());
            id = idg.getId();
            ASSERT_FALSE(true);
        }
        catch (const std::exception &e) {
            LOG << e.what();
        }
        ASSERT_EQ(-1, id);
    }

    TEST_F(IdGeneratorTest, getId_006)
    {
        IdGenerator<char> idg{0, false};
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            std::thread t{
                [&idg] {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    for (int j = 0; j < 11; ++j) {
                        idg.getId();
                    }
                }};
            threads.push_back(std::move(t));
        }
        for (std::thread &ref : threads) {
            ref.join();
        }
        // この時点で10(スレッド数) * 11(各スレッドでのgetId実行回数) = 110のidが生成されている
        // 0から始まるので次は110になる
        ASSERT_EQ(110, idg.getId());
        ASSERT_EQ(111, idg.getId());
        ASSERT_EQ(112, idg.getId());
        ASSERT_EQ(113, idg.getId());
        ASSERT_EQ(114, idg.getId());
        ASSERT_EQ(115, idg.getId());
        ASSERT_EQ(116, idg.getId());
        ASSERT_EQ(117, idg.getId());
        ASSERT_EQ(118, idg.getId());
        ASSERT_EQ(119, idg.getId());
        ASSERT_EQ(120, idg.getId());
        ASSERT_EQ(121, idg.getId());
        ASSERT_EQ(122, idg.getId());
        ASSERT_EQ(123, idg.getId());
        ASSERT_EQ(124, idg.getId());
        ASSERT_EQ(125, idg.getId());
        ASSERT_EQ(126, idg.getId());
        char id = -1;
        try {
            id = idg.getId();
            ASSERT_FALSE(true);
        }
        catch (const std::exception &e) {
            LOG << e.what();
        }
        ASSERT_EQ(-1, id);
    }

    TEST_F(IdGeneratorTest, getId_007)
    {
        IdGenerator<char> idg{0, true};
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            std::thread t{
                [&idg, threadId = i] {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    bool releaseFlag = false;
                    char id = -1;
                    for (int j = 0; j < 13; ++j) {
                        if (j == 11) {
                            // 12回目を退避
                            id = idg.getId();
                        }
                        else if (j == 12) {
                            // 13回目では上限値を超えうるので12回目で取得したidをリリース
                            idg.release(id);
                            releaseFlag = true;
                            // threadIdが偶数であれば再び新たに取得
                            if (threadId % 2 == 0) {
                                idg.getId();
                            }
                        }
                        else {
                            idg.getId();
                        }
                    }
                    ASSERT_TRUE(releaseFlag);
                }};
            threads.push_back(std::move(t));
        }
        for (std::thread &ref : threads) {
            ref.join();
        }
        // この時点で10(スレッド数) * 12(各スレッドでのgetId実行回数) = 120のidが生成
        // 10(スレッド数) * 1(各スレッドでのrelease実行回数) = 10のidをリリース
        // 5(偶数スレッド数) * 1(各スレッドでのgetId実行回数) = 5のidが生成
        // idは0から始まるので0から126で127回生成できるから
        // 残り127 - 120 + 10 - 5 = 12回id生成ができる
        for (int i = 0; i < 12; ++i) {
            idg.getId();
        }

        char id = -1;
        try {
            id = idg.getId();
            ASSERT_FALSE(true);
        }
        catch (const std::exception &e) {
            LOG << e.what();
        }
        ASSERT_EQ(-1, id);
    }

    TEST_F(IdGeneratorTest, release_001)
    {
        try {
            IdGenerator<short> idg{0};
            idg.release(6);
        }
        catch (...) {
            ASSERT_FALSE(true);
        }
    }

    TEST_F(IdGeneratorTest, release_002)
    {
        try {
            IdGenerator<short> idg{0};
            short id = idg.getId();
            ASSERT_EQ(0, id);
            idg.release(6);
            idg.release(id);
            id = idg.getId();
            ASSERT_EQ(1, id);
        }
        catch (...) {
            ASSERT_FALSE(true);
        }
    }

    TEST_F(IdGeneratorTest, release_003)
    {
        IdGenerator<short> idg{0, true};
        short id = idg.getId();
        ASSERT_EQ(0, id);
        idg.release(6);
        idg.release(id);
        id = idg.getId();
        ASSERT_EQ(1, id);
    }

} // namespace PapierMache::DbStuff
