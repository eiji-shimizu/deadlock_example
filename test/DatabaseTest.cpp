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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace PapierMache::DbStuff {

    class DatabaseTest : public ::testing::Test {
    protected:
        DatabaseTest()
        {
        }

        ~DatabaseTest() override
        {
        }

        void SetUp() override
        {
            // 空のデータファイルを新規作成する
            std::vector<std::string> files = {"user", "order"};
            std::string dataFilePath = "./database/data/";
            for (const std::string fileName : files) {
                std::filesystem::remove(dataFilePath + fileName);
                std::ofstream ofs{dataFilePath + fileName};
            }
        }

        void TearDown() override
        {
        }

        std::string dq(const std::string &s) const
        {
            return std::string{'\"'} + s + std::string{'\"'};
        }
    };

    TEST_F(DatabaseTest, start_001)
    {
        Database db{};
        db.start();
        db.start();
        db.start();
        db.start();
        db.start();
    }

    TEST_F(DatabaseTest, insert_001)
    {
        Database db{};
        db.start();
        PapierMache::DbStuff::Connection con = db.getConnection();
        Driver driver{con};
        Driver::Result r = driver.sendQuery("please:user admin adminpass");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        ASSERT_EQ(2, r.rows.size());
        std::sort(r.rows.begin(), r.rows.end(),
                  [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                      return a.at("order_name") < b.at("order_name");
                  });

        for (int i = 0; i < r.rows.size(); ++i) {
            const auto row = r.rows[i];
            switch (i) {
            case 0:
                ASSERT_STREQ("order1", row.at("order_name").c_str());
                ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                ASSERT_STREQ("商品いろはにほへと", row.at("product_name").c_str());
                break;
            case 1:
                ASSERT_STREQ("order2", row.at("order_name").c_str());
                ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                ASSERT_STREQ("商品えひもせすん", row.at("product_name").c_str());
                break;
            default:
                FAIL() << "We shouldn't get here.";
            }
        }
    }

    TEST_F(DatabaseTest, update_001)
    {
        Database db{};
        db.start();
        PapierMache::DbStuff::Connection con = db.getConnection();
        Driver driver{con};
        Driver::Result r = driver.sendQuery("please:user admin adminpass");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:update order  (PRODUCT_NAME=" + dq("テスト商品００８") + ") (CUSTOMER_NAME=" + dq("お客様B") + ") ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        ASSERT_EQ(3, r.rows.size());
        std::sort(r.rows.begin(), r.rows.end(),
                  [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                      return a.at("order_name") < b.at("order_name");
                  });

        for (int i = 0; i < r.rows.size(); ++i) {
            const auto row = r.rows[i];
            switch (i) {
            case 0:
                ASSERT_STREQ("order1", row.at("order_name").c_str());
                ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                ASSERT_STREQ("商品いろはにほへと", row.at("product_name").c_str());
                break;
            case 1:
                ASSERT_STREQ("order2", row.at("order_name").c_str());
                ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                ASSERT_STREQ("テスト商品００８", row.at("product_name").c_str());
                break;
            case 2:
                ASSERT_STREQ("order3", row.at("order_name").c_str());
                ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                ASSERT_STREQ("テスト商品００８", row.at("product_name").c_str());
                break;
            default:
                FAIL() << "We shouldn't get here.";
            }
        }
    }

    TEST_F(DatabaseTest, update_002)
    {
        Database db{};
        db.start();
        PapierMache::DbStuff::Connection con = db.getConnection();
        Driver driver{con};
        Driver::Result r = driver.sendQuery("please:user admin adminpass");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:update order  (PRODUCT_NAME=" + dq("テスト商品００９") + ")  ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        ASSERT_EQ(3, r.rows.size());
        std::sort(r.rows.begin(), r.rows.end(),
                  [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                      return a.at("order_name") < b.at("order_name");
                  });

        for (int i = 0; i < r.rows.size(); ++i) {
            const auto row = r.rows[i];
            switch (i) {
            case 0:
                ASSERT_STREQ("order1", row.at("order_name").c_str());
                ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                ASSERT_STREQ("テスト商品００９", row.at("product_name").c_str());
                break;
            case 1:
                ASSERT_STREQ("order2", row.at("order_name").c_str());
                ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                ASSERT_STREQ("テスト商品００９", row.at("product_name").c_str());
                break;
            case 2:
                ASSERT_STREQ("order3", row.at("order_name").c_str());
                ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                ASSERT_STREQ("テスト商品００９", row.at("product_name").c_str());
                break;
            default:
                FAIL() << "We shouldn't get here.";
            }
        }
    }

    TEST_F(DatabaseTest, delete_001)
    {
        Database db{};
        db.start();
        PapierMache::DbStuff::Connection con = db.getConnection();
        Driver driver{con};
        Driver::Result r = driver.sendQuery("please:user admin adminpass");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:delete order  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        ASSERT_EQ(1, r.rows.size());

        for (int i = 0; i < r.rows.size(); ++i) {
            const auto row = r.rows[i];
            switch (i) {
            case 0:
                ASSERT_STREQ("order1", row.at("order_name").c_str());
                ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                ASSERT_STREQ("商品いろはにほへと", row.at("product_name").c_str());
                break;
            default:
                FAIL() << "We shouldn't get here.";
            }
        }
    }

    TEST_F(DatabaseTest, delete_002)
    {
        Database db{};
        db.start();
        PapierMache::DbStuff::Connection con = db.getConnection();
        Driver driver{con};
        Driver::Result r = driver.sendQuery("please:user admin adminpass");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:delete order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) FAIL();
        ASSERT_EQ(0, r.rows.size());
    }

    TEST_F(DatabaseTest, parallel_operation_001)
    {
        try {
            Database db{};
            db.start();

            std::random_device seed_gen;
            std::default_random_engine engine(seed_gen());
            // 0以上5以下の値を等確率で発生させる
            std::uniform_int_distribution<> dist(0, 5);

            std::vector<std::thread> threads;
            const int maxThreads = 50;
            for (int i = 0; i < maxThreads; ++i) {
                std::thread t{
                    [&db, &dist, &engine, threadId = i, maxThreads, this] {
                        try {
                            // 乱数によるスリープ
                            std::this_thread::sleep_for(std::chrono::milliseconds((maxThreads - threadId) * dist(engine)));
                            Connection con = db.getConnection();
                            Driver driver{con};
                            Driver::Result r = driver.sendQuery("please:user admin adminpass");
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:transaction   ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            std::string orderName = "order" + std::to_string(threadId);
                            std::string orderName__x = "order";
                            if (threadId < 10) {
                                orderName__x += "__";
                            }
                            else {
                                orderName__x += "_";
                            }
                            orderName__x += std::to_string(threadId);
                            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName) + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName) + ")");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName__x) + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName__x) + ")");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:commit");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            bool b = con.close();
                            if (!b) FAIL();
                        }
                        catch (std::exception &e) {
                            LOG << e.what();
                        }
                    }};
                threads.push_back(std::move(t));
            }
            for (std::thread &ref : threads) {
                ref.join();
            }

            threads.clear();
            Connection con = db.getConnection();
            Driver driver{con};
            Driver::Result r = driver.sendQuery("please:user admin adminpass");
            if (!r.isSucceed) FAIL();
            r = driver.sendQuery("please:transaction");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            r = driver.sendQuery("please: select order   ");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            ASSERT_EQ(100, r.rows.size());

            std::sort(r.rows.begin(), r.rows.end(),
                      [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                          return a.at("order_name") < b.at("order_name");
                      });

            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                std::string orderName = row.at("order_name").c_str();
                ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
                if (i < maxThreads) {
                    // 0から49
                    ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                }
                else {
                    // 50から99
                    ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                }
                ASSERT_STREQ(("商品いろはにほへと:" + orderName).c_str(), row.at("product_name").c_str());
            }

            // updateを実行
            for (int i = 0; i < maxThreads; ++i) {
                std::thread t{
                    [&db, &dist, &engine, threadId = i, maxThreads, this] {
                        try {
                            // 乱数によるスリープ
                            std::this_thread::sleep_for(std::chrono::milliseconds((maxThreads - threadId) * dist(engine)));
                            Connection con = db.getConnection();
                            Driver driver{con};
                            Driver::Result r = driver.sendQuery("please:user admin adminpass");
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:transaction   ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            std::string orderName = "order" + std::to_string(threadId);
                            std::string orderName__x = "order";
                            if (threadId < 10) {
                                orderName__x += "__";
                            }
                            else {
                                orderName__x += "_";
                            }
                            orderName__x += std::to_string(threadId);
                            r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:commit");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            bool b = con.close();
                            if (!b) FAIL();
                        }
                        catch (std::exception &e) {
                            LOG << e.what();
                        }
                    }};
                threads.push_back(std::move(t));
            }
            for (std::thread &ref : threads) {
                ref.join();
            }

            threads.clear();

            Connection con2 = db.getConnection();
            Driver driver2{con2};
            r = driver2.sendQuery("please:user admin adminpass");
            if (!r.isSucceed) FAIL();
            r = driver2.sendQuery("please:transaction");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            r = driver2.sendQuery("please: select order   ");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            ASSERT_EQ(100, r.rows.size());

            std::sort(r.rows.begin(), r.rows.end(),
                      [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                          return a.at("order_name") < b.at("order_name");
                      });

            int eqCount = 0;
            int eq__Count = 0;
            int eqId = -1;
            int eq__Id = -1;
            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                std::string orderName = row.at("order_name").c_str();
                ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
                if (i < maxThreads) {
                    // 0から49
                    ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                }
                else {
                    // 50から99
                    ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                }
                std::ostringstream oss{""};
                for (const char c : row.at("product_name")) {
                    if (c == 0) {
                        break;
                    }
                    oss << c;
                }
                if (i < maxThreads) {
                    // 0から49
                    int id = i;
                    if (("商品いろはにほへと:id" + std::to_string(id)) == oss.str()) {
                        ++eqCount;
                        eqId = id;
                    }
                }
                else {
                    // 50から99
                    int id = i - maxThreads;
                    if (("商品えひもせすん:id" + std::to_string(id)) == oss.str()) {
                        ++eq__Count;
                        eq__Id = id;
                    }
                }
            }
            ASSERT_EQ(1, eqCount);
            ASSERT_EQ(1, eq__Count);
            LOG << "eqId: " << eqId;
            LOG << "eq__Id: " << eq__Id;
            ASSERT_EQ(eqId, eq__Id);
            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                if (i < maxThreads) {
                    // 0から49
                    int id = eqId;
                    ASSERT_STREQ(("商品いろはにほへと:id" + std::to_string(id)).c_str(), row.at("product_name").c_str());
                }
                else {
                    // 50から99
                    int id = eqId;
                    ASSERT_STREQ(("商品えひもせすん:id" + std::to_string(id)).c_str(), row.at("product_name").c_str());
                }
            }
        }
        catch (std::exception &e) {
            LOG << e.what();
            FAIL() << "We shouldn't get here.";
        }
    }

    TEST_F(DatabaseTest, parallel_operation_002)
    {
        try {
            Database db{};
            db.start();

            std::vector<std::thread> threads;
            const int maxThreads = 2;
            for (int i = 0; i < maxThreads; ++i) {
                std::thread t{
                    [&db, threadId = i, maxThreads, this] {
                        try {
                            Connection con = db.getConnection();
                            Driver driver{con};
                            Driver::Result r = driver.sendQuery("please:user admin adminpass");
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:transaction   ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            std::string orderName = "order" + std::to_string(threadId);
                            std::string orderName__x = "order";
                            if (threadId < 10) {
                                orderName__x += "__";
                            }
                            else {
                                orderName__x += "_";
                            }
                            orderName__x += std::to_string(threadId);
                            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName) + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName) + ")");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName__x) + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName__x) + ")");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:commit");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            bool b = con.close();
                            if (!b) FAIL();
                        }
                        catch (std::exception &e) {
                            LOG << e.what();
                        }
                    }};
                threads.push_back(std::move(t));
            }
            for (std::thread &ref : threads) {
                ref.join();
            }

            threads.clear();
            Connection con = db.getConnection();
            Driver driver{con};
            Driver::Result r = driver.sendQuery("please:user admin adminpass");
            if (!r.isSucceed) FAIL();
            r = driver.sendQuery("please:transaction");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            r = driver.sendQuery("please: select order   ");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            ASSERT_EQ(4, r.rows.size());

            std::sort(r.rows.begin(), r.rows.end(),
                      [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                          return a.at("order_name") < b.at("order_name");
                      });

            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                std::string orderName = row.at("order_name").c_str();
                ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
                if (i < maxThreads) {
                    // 0から1
                    ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                }
                else {
                    // 2から3
                    ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                }
                ASSERT_STREQ(("商品いろはにほへと:" + orderName).c_str(), row.at("product_name").c_str());
            }

            // updateを実行
            for (int i = 0; i < maxThreads; ++i) {
                std::thread t{
                    [&db, threadId = i, maxThreads, this] {
                        try {
                            Connection con = db.getConnection();
                            Driver driver{con};
                            Driver::Result r = driver.sendQuery("please:user admin adminpass");
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:transaction   ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            std::string orderName = "order" + std::to_string(threadId);
                            std::string orderName__x = "order";
                            if (threadId < 10) {
                                orderName__x += "__";
                            }
                            else {
                                orderName__x += "_";
                            }
                            orderName__x += std::to_string(threadId);
                            if (threadId % 2 == 0) {
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:commit");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                            }
                            else {
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:commit");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                            }

                            bool b = con.close();
                            if (!b) FAIL();
                        }
                        catch (std::exception &e) {
                            LOG << e.what();
                        }
                    }};
                threads.push_back(std::move(t));
            }
            for (std::thread &ref : threads) {
                ref.join();
            }

            threads.clear();

            Connection con2 = db.getConnection();
            Driver driver2{con2};
            r = driver2.sendQuery("please:user admin adminpass");
            if (!r.isSucceed) FAIL();
            r = driver2.sendQuery("please:transaction");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            r = driver2.sendQuery("please: select order   ");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            ASSERT_EQ(4, r.rows.size());

            std::sort(r.rows.begin(), r.rows.end(),
                      [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                          return a.at("order_name") < b.at("order_name");
                      });

            int eqCount = 0;
            int eq__Count = 0;
            int eqId = -1;
            int eq__Id = -1;
            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                std::string orderName = row.at("order_name").c_str();
                ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
                if (i < maxThreads) {
                    // 0から1
                    ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                }
                else {
                    // 2から3
                    ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                }
                std::ostringstream oss{""};
                for (const char c : row.at("product_name")) {
                    if (c == 0) {
                        break;
                    }
                    oss << c;
                }
                if (i < maxThreads) {
                    // 0から1
                    int id = i;
                    if (("商品えひもせすん:id" + std::to_string(id)) == oss.str()) {
                        ++eqCount;
                        eqId = id;
                    }
                }
                else {
                    // 2から3
                    int id = i - maxThreads;
                    if (("商品いろはにほへと:id" + std::to_string(id)) == oss.str()) {
                        ++eq__Count;
                        eq__Id = id;
                    }
                }
            }
            ASSERT_EQ(1, eqCount);
            ASSERT_EQ(1, eq__Count);
            LOG << "eqId: " << eqId;
            LOG << "eq__Id: " << eq__Id;
            ASSERT_EQ(eqId, eq__Id);
            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                if (i < maxThreads) {
                    // 0から1
                    int id = eqId;
                    ASSERT_STREQ(("商品えひもせすん:id" + std::to_string(id)).c_str(), row.at("product_name").c_str());
                }
                else {
                    // 2から3
                    int id = eqId;
                    ASSERT_STREQ(("商品いろはにほへと:id" + std::to_string(id)).c_str(), row.at("product_name").c_str());
                }
            }
        }
        catch (std::exception &e) {
            LOG << e.what();
            FAIL() << "We shouldn't get here.";
        }
    }

    TEST_F(DatabaseTest, parallel_operation_003)
    {
        try {
            Database db{};
            db.start();

            std::vector<std::thread> threads;
            const int maxThreads = 2;
            for (int i = 0; i < maxThreads; ++i) {
                std::thread t{
                    [&db, threadId = i, maxThreads, this] {
                        try {
                            Connection con = db.getConnection();
                            Driver driver{con};
                            Driver::Result r = driver.sendQuery("please:user admin adminpass");
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:transaction   ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            std::string orderName = "order" + std::to_string(threadId);
                            std::string orderName__x = "order";
                            if (threadId < 10) {
                                orderName__x += "__";
                            }
                            else {
                                orderName__x += "_";
                            }
                            orderName__x += std::to_string(threadId);
                            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName) + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName) + ")");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName__x) + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName__x) + ")");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:commit");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            bool b = con.close();
                            if (!b) FAIL();
                        }
                        catch (std::exception &e) {
                            LOG << e.what();
                        }
                    }};
                threads.push_back(std::move(t));
            }
            for (std::thread &ref : threads) {
                ref.join();
            }

            threads.clear();
            Connection con = db.getConnection();
            Driver driver{con};
            Driver::Result r = driver.sendQuery("please:user admin adminpass");
            if (!r.isSucceed) FAIL();
            r = driver.sendQuery("please:transaction");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            r = driver.sendQuery("please: select order   ");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            ASSERT_EQ(4, r.rows.size());

            std::sort(r.rows.begin(), r.rows.end(),
                      [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                          return a.at("order_name") < b.at("order_name");
                      });

            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                std::string orderName = row.at("order_name").c_str();
                ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
                if (i < maxThreads) {
                    // 0から1
                    ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                }
                else {
                    // 2から3
                    ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                }
                ASSERT_STREQ(("商品いろはにほへと:" + orderName).c_str(), row.at("product_name").c_str());
            }

            // updateを実行
            for (int i = 0; i < maxThreads; ++i) {
                std::thread t{
                    [&db, threadId = i, maxThreads, this] {
                        try {
                            Connection con = db.getConnection();
                            Driver driver{con};
                            Driver::Result r = driver.sendQuery("please:user admin adminpass");
                            if (!r.isSucceed) FAIL();
                            r = driver.sendQuery("please:transaction   ");
                            LOG << r.isSucceed << ": " << r.message;
                            if (!r.isSucceed) FAIL();
                            std::string orderName = "order" + std::to_string(threadId);
                            std::string orderName__x = "order";
                            if (threadId < 10) {
                                orderName__x += "__";
                            }
                            else {
                                orderName__x += "_";
                            }
                            orderName__x += std::to_string(threadId);
                            if (threadId % 2 == 0) {
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:commit");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                            }
                            else {
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                                r = driver.sendQuery("please:commit");
                                LOG << r.isSucceed << ": " << r.message;
                                if (!r.isSucceed) FAIL();
                            }

                            bool b = con.close();
                            if (!b) FAIL();
                        }
                        catch (std::exception &e) {
                            LOG << e.what();
                        }
                    }};
                threads.push_back(std::move(t));
            }
            for (std::thread &ref : threads) {
                ref.join();
            }

            threads.clear();

            Connection con2 = db.getConnection();
            Driver driver2{con2};
            r = driver2.sendQuery("please:user admin adminpass");
            if (!r.isSucceed) FAIL();
            r = driver2.sendQuery("please:transaction");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            r = driver2.sendQuery("please: select order   ");
            LOG << r.isSucceed << ": " << r.message;
            if (!r.isSucceed) FAIL();
            ASSERT_EQ(4, r.rows.size());

            std::sort(r.rows.begin(), r.rows.end(),
                      [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
                          return a.at("order_name") < b.at("order_name");
                      });

            int eqCount = 0;
            int eq__Count = 0;
            int eqId = -1;
            int eq__Id = -1;
            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                std::string orderName = row.at("order_name").c_str();
                ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
                if (i < maxThreads) {
                    // 0から1
                    ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
                }
                else {
                    // 2から3
                    ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
                }
                std::ostringstream oss{""};
                for (const char c : row.at("product_name")) {
                    if (c == 0) {
                        break;
                    }
                    oss << c;
                }
                if (i < maxThreads) {
                    // 0から1
                    int id = i;
                    if (("商品えひもせすん:id" + std::to_string(id)) == oss.str()) {
                        ++eqCount;
                        eqId = id;
                    }
                }
                else {
                    // 2から3
                    int id = i - maxThreads;
                    if (("商品いろはにほへと:id" + std::to_string(id)) == oss.str()) {
                        ++eq__Count;
                        eq__Id = id;
                    }
                }
            }
            ASSERT_EQ(1, eqCount);
            ASSERT_EQ(1, eq__Count);
            LOG << "eqId: " << eqId;
            LOG << "eq__Id: " << eq__Id;
            ASSERT_EQ(eqId, eq__Id);
            for (int i = 0; i < r.rows.size(); ++i) {
                const auto row = r.rows[i];
                if (i < maxThreads) {
                    // 0から1
                    int id = eqId;
                    ASSERT_STREQ(("商品えひもせすん:id" + std::to_string(id)).c_str(), row.at("product_name").c_str());
                }
                else {
                    // 2から3
                    int id = eqId;
                    ASSERT_STREQ(("商品いろはにほへと:id" + std::to_string(id)).c_str(), row.at("product_name").c_str());
                }
            }
        }
        catch (std::exception &e) {
            LOG << e.what();
            FAIL() << "We shouldn't get here.";
        }
    }

    // かなりの確率でデッドロックになるので普段は実行しない
    // TEST_F(DatabaseTest, parallel_operation_004)
    // {
    //     try {
    //         Database db{};
    //         db.start();

    //         std::vector<std::thread> threads;
    //         const int maxThreads = 2;
    //         for (int i = 0; i < maxThreads; ++i) {
    //             std::thread t{
    //                 [&db, threadId = i, maxThreads, this] {
    //                     try {
    //                         Connection con = db.getConnection();
    //                         Driver driver{con};
    //                         Driver::Result r = driver.sendQuery("please:user admin adminpass");
    //                         if (!r.isSucceed) FAIL();
    //                         r = driver.sendQuery("please:transaction   ");
    //                         LOG << r.isSucceed << ": " << r.message;
    //                         if (!r.isSucceed) FAIL();
    //                         std::string orderName = "order" + std::to_string(threadId);
    //                         std::string orderName__x = "order";
    //                         if (threadId < 10) {
    //                             orderName__x += "__";
    //                         }
    //                         else {
    //                             orderName__x += "_";
    //                         }
    //                         orderName__x += std::to_string(threadId);
    //                         r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName) + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName) + ")");
    //                         LOG << r.isSucceed << ": " << r.message;
    //                         if (!r.isSucceed) FAIL();
    //                         r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq(orderName__x) + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品いろはにほへと:" + orderName__x) + ")");
    //                         LOG << r.isSucceed << ": " << r.message;
    //                         if (!r.isSucceed) FAIL();
    //                         r = driver.sendQuery("please:commit");
    //                         LOG << r.isSucceed << ": " << r.message;
    //                         if (!r.isSucceed) FAIL();
    //                         bool b = con.close();
    //                         if (!b) FAIL();
    //                     }
    //                     catch (std::exception &e) {
    //                         LOG << e.what();
    //                     }
    //                 }};
    //             threads.push_back(std::move(t));
    //         }
    //         for (std::thread &ref : threads) {
    //             ref.join();
    //         }

    //         threads.clear();
    //         Connection con = db.getConnection();
    //         Driver driver{con};
    //         Driver::Result r = driver.sendQuery("please:user admin adminpass");
    //         if (!r.isSucceed) FAIL();
    //         r = driver.sendQuery("please:transaction");
    //         LOG << r.isSucceed << ": " << r.message;
    //         if (!r.isSucceed) FAIL();
    //         r = driver.sendQuery("please: select order   ");
    //         LOG << r.isSucceed << ": " << r.message;
    //         if (!r.isSucceed) FAIL();
    //         ASSERT_EQ(4, r.rows.size());

    //         std::sort(r.rows.begin(), r.rows.end(),
    //                   [](std::map<std::string, std::string> a, std::map<std::string, std::string> b) {
    //                       return a.at("order_name") < b.at("order_name");
    //                   });

    //         for (int i = 0; i < r.rows.size(); ++i) {
    //             const auto row = r.rows[i];
    //             std::string orderName = row.at("order_name").c_str();
    //             ASSERT_STREQ(orderName.c_str(), row.at("order_name").c_str());
    //             if (i < maxThreads) {
    //                 // 0から1
    //                 ASSERT_STREQ("お客様A", row.at("customer_name").c_str());
    //             }
    //             else {
    //                 // 2から3
    //                 ASSERT_STREQ("お客様B", row.at("customer_name").c_str());
    //             }
    //             ASSERT_STREQ(("商品いろはにほへと:" + orderName).c_str(), row.at("product_name").c_str());
    //         }

    //         // updateを実行
    //         for (int i = 0; i < maxThreads; ++i) {
    //             std::thread t{
    //                 [&db, threadId = i, maxThreads, this] {
    //                     try {
    //                         Connection con = db.getConnection();
    //                         Driver driver{con};
    //                         Driver::Result r = driver.sendQuery("please:user admin adminpass");
    //                         if (!r.isSucceed) FAIL();
    //                         r = driver.sendQuery("please:transaction   ");
    //                         LOG << r.isSucceed << ": " << r.message;
    //                         if (!r.isSucceed) FAIL();
    //                         std::string orderName = "order" + std::to_string(threadId);
    //                         std::string orderName__x = "order";
    //                         if (threadId < 10) {
    //                             orderName__x += "__";
    //                         }
    //                         else {
    //                             orderName__x += "_";
    //                         }
    //                         orderName__x += std::to_string(threadId);
    //                         if (threadId % 2 == 0) {
    //                             r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
    //                             LOG << r.isSucceed << ": " << r.message;
    //                             if (!r.isSucceed) FAIL();
    //                             std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    //                             r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
    //                             LOG << r.isSucceed << ": " << r.message;
    //                             if (!r.isSucceed) FAIL();
    //                             r = driver.sendQuery("please:commit");
    //                             LOG << r.isSucceed << ": " << r.message;
    //                             if (!r.isSucceed) FAIL();
    //                         }
    //                         else {
    //                             r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品いろはにほへと:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
    //                             LOG << r.isSucceed << ": " << r.message;
    //                             if (!r.isSucceed) FAIL();
    //                             std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    //                             r = driver.sendQuery("please:update  order (PRODUCT_NAME=" + dq("商品えひもせすん:id" + std::to_string(threadId)) + ")  (CUSTOMER_NAME=" + dq("お客様A") + ") ");
    //                             LOG << r.isSucceed << ": " << r.message;
    //                             if (!r.isSucceed) FAIL();
    //                             r = driver.sendQuery("please:commit");
    //                             LOG << r.isSucceed << ": " << r.message;
    //                             if (!r.isSucceed) FAIL();
    //                         }

    //                         bool b = con.close();
    //                         if (!b) FAIL();
    //                     }
    //                     catch (std::exception &e) {
    //                         LOG << e.what();
    //                     }
    //                 }};
    //             threads.push_back(std::move(t));
    //         }
    //         for (std::thread &ref : threads) {
    //             ref.join();
    //         }

    //         threads.clear();
    //     }
    //     catch (std::exception &e) {
    //         LOG << e.what();
    //         FAIL() << "We shouldn't get here.";
    //     }
    // }

} // namespace PapierMache::DbStuff