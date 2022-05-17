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
#include <string>
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
        }

        void TearDown() override
        {
            // 空のデータファイルを新規作成する
            std::vector<std::string> files = {"user", "order"};
            std::string dataFilePath = "./database/data/";
            for (const std::string fileName : files) {
                std::filesystem::remove(dataFilePath + fileName);
                std::ofstream ofs{dataFilePath + fileName};
            }
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
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
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
                ASSERT_FALSE(true);
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
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:update order  (PRODUCT_NAME=" + dq("テスト商品００８") + ") (CUSTOMER_NAME=" + dq("お客様B") + ") ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
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
                ASSERT_FALSE(true);
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
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:update order  (PRODUCT_NAME=" + dq("テスト商品００９") + ")  ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
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
                ASSERT_FALSE(true);
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
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:delete order  (CUSTOMER_NAME=" + dq("お客様B") + ") ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
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
                ASSERT_FALSE(true);
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
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:transaction   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order1") + ", CUSTOMER_NAME=" + dq("お客様A") + ", PRODUCT_NAME=" + dq("商品いろはにほへと") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order2") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品えひもせすん") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:insert  order (ORDER_NAME=" + dq("order3") + ", CUSTOMER_NAME=" + dq("お客様B") + ", PRODUCT_NAME=" + dq("商品XYZ") + ")");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:delete order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please:commit");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);

        r = driver.sendQuery("please:transaction");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        r = driver.sendQuery("please: select order   ");
        LOG << r.isSucceed << ": " << r.message;
        if (!r.isSucceed) ASSERT_FALSE(true);
        ASSERT_EQ(0, r.rows.size());
    }

} // namespace PapierMache::DbStuff