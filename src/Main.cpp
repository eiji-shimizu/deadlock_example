#include "General.h"

#include "Common.h"
#include "Database.h"
#include "Logger.h"
#include "UUID.h"
#include "Utils.h"
#include "WebServer.h"

#include <iostream>
#include <sstream>
#include <string>

PapierMache::Logger<std::ostream> logger{std::cout};

const std::map<std::string, std::map<std::string, std::string>> webConfiguration{PapierMache::readConfiguration("./webconfig/server.ini")};

// PapierMache::DbStuff::Database database{};

void testFunc0(PapierMache::DbStuff::Connection con, std::string name, std::string message)
{
    std::vector<std::byte> data;
    for (const char c : message) {
        data.push_back(static_cast<std::byte>(c));
    }
    std::ostringstream oss{""};
    con.send(data);
    bool requestResult = con.request();
    if (!requestResult) {
        LOG << name << "error";
        throw std::runtime_error{name + ": request() failed"};
    }
    if (requestResult) {
        LOG << name << ": wait() BEFORE";
        int waitResult = con.wait();
        LOG << name << ": wait() AFTER";
        if (waitResult == 0) {
            con.receive(data);

            for (const auto b : data) {
                oss << static_cast<char>(b);
            }
            LOG << name << ": " << con.id() << " " << oss.str();
        }
        else if (waitResult == -1) {
            // リトライ
            LOG << name << ": wait() failed first";
            if (con.wait() == 0) {
                con.receive(data);
                for (const auto b : data) {
                    oss << static_cast<char>(b);
                }
                LOG << name << ": " << con.id() << " " << oss.str();
            }
            else {
                throw std::runtime_error{name + ": wait() failed twice."};
            }
        }
    }
}

void testFunc(PapierMache::DbStuff::Connection con, std::string name, std::string query)
{
    PapierMache::DbStuff::Driver driver{con};
    const auto result = driver.sendQuery(query);
    if (!result.isSucceed) {
        LOG << result.message;
    }
    if (result.isSucceed) {
        LOG << result.message;
    }
}

int main()
{
    try {

        LOG << "application start." << FILE_INFO;
        WEB_LOG << "web configuration is";
        WEB_LOG << "webServer PORT: " << PapierMache::getValue<std::string>(webConfiguration, "webServer", "PORT");
        WEB_LOG << "webServer MAX_SOCKETS: " << PapierMache::getValue<int>(webConfiguration, "webServer", "MAX_SOCKETS");
        WEB_LOG << "threadsMap CLEAN_UP_POINT: " << PapierMache::getValue<int>(webConfiguration, "threadsMap", "CLEAN_UP_POINT");
        WEB_LOG << "socketManager MAX: " << PapierMache::getValue<int>(webConfiguration, "socketManager", "MAX");
        WEB_LOG << "socketManager TIMEOUT: " << PapierMache::getValue<int>(webConfiguration, "socketManager", "TIMEOUT");

        PapierMache::WebServer server{PapierMache::getValue<std::string>(webConfiguration, "webServer", "PORT"),
                                      PapierMache::getValue<int>(webConfiguration, "webServer", "MAX_SOCKETS")};
        LOG << "server initialization start.";
        if (server.initialize() != 0) {
            LOG << "server initialization failed.";
            return 1;
        }
        LOG << "server initialization end.";

        LOG << "database initialization start.";
        PapierMache::DbStuff::Database db{};
        db.start();
        LOG << "database initialization end.";

        // 以下簡単なテストコード
        db.start();
        db.start();
        db.start();

        // PapierMache::DbStuff::Connection con = db.getConnection();
        // testFunc(con, "con1", "PLEASE:TRANSACTION");
        // con.close();
        // PapierMache::DbStuff::Connection con2 = db.getConnection();
        // testFunc(con2, "con2", "PLEASE:UPDATE");
        // PapierMache::DbStuff::Connection con3 = db.getConnection();
        // testFunc(con3, "con3", "PLEASE:TRANSACTION");
        // testFunc(con3, "con3", "PLEASE:TRANSACTION");
        // testFunc(con3, "con3", "PLEASE:DELETE");
        // con3.close();
        // con2.close();
        // testFunc(con, "con1", "PLEASE:TRANSACTION");

        PapierMache::DbStuff::Connection con4 = db.getConnection();
        testFunc(con4, "con4", "PLEASE:USER admin adminpass");
        testFunc(con4, "con4", "PLEASE:TRANSACTION");
        testFunc(con4, "con4", "please:insert User    (   USER_NAME=\"testuser999\" , DATETIME=\"30827:12:31:23:59:59:999\")");
        testFunc(con4, "con4", "PLEASE:commiT");

        testFunc(con4, "con4", "PLEASE:TRANSACTION");
        testFunc(con4, "con4", "please:update User    (   USER_NAME=\"testuser8787\" , DATETIME=\"30827:12:31:23:59:59:999\")(   USER_NAME=\"testuser999\" , DATETIME=\"30827:12:31:23:59:59:999\")");
        // testFunc(con4, "con4", "PLEASE:ROLLBACK");
        testFunc(con4, "con4", "PLEASE:commit");

        testFunc(con4, "con4", "PLEASE:TRANSACTION");
        testFunc(con4, "con4", "please:insert User    (   USER_NAME=\"testuser10\" , DATETIME=\"30827:12:31:23:59:59:999\")");
        testFunc(con4, "con4", "PLEASE:commit");

        PapierMache::DbStuff::Connection con5 = db.getConnection();
        testFunc(con5, "con5", "PLEASE:USER user1 user1pass");
        // testFunc(con5, "con5", "PLEASE:USER admin adminpass");
        testFunc(con5, "con5", "PLEASE:TRANSACTION");

        testFunc(con4, "con4", "PLEASE:TRANSACTION");
        testFunc(con4, "con4", "please:update User    (   USER_NAME=\"testuser777\" , DATETIME=\"30827:12:31:23:59:59:999\")(   USER_NAME=\"testuser10\" , DATETIME=\"30827:12:31:23:59:59:999\")");
        con4.terminate();
        testFunc(con5, "con5", "please:update User    (   USER_NAME=\"testuser555\" , DATETIME=\"30827:12:31:23:59:59:999\")(   USER_NAME=\"testuser10\" , DATETIME=\"30827:12:31:23:59:59:999\")");

        // testFunc(con4, "con4", "PLEASE:commit");
        testFunc(con5, "con5", "PLEASE:commit");
        // con4.close();
        testFunc(con4, "con4", "PLEASE:TRANSACTION");
        testFunc(con4, "con4", "please:delete User    (   USER_NAME=\"testuser8787\" , DATETIME=\"30827:12:31:23:59:59:999\")");
        testFunc(con4, "con4", "PLEASE:commit");

        testFunc(con4, "con4", "PLEASE:TRANSACTION");
        testFunc(con4, "con4", "please:delete User    (   USER_NAME=\"testuser888\")");
        testFunc(con4, "con4", "PLEASE:commit");

        testFunc(con5, "con5", "PLEASE:TRANSACTION");
        testFunc(con5, "con5", "please:delete User    (   USER_NAME=\"testuser555\")");
        testFunc(con5, "con5", "PLEASE:commit");
        //  テストコードここまで

        LOG << "------------------------------";

        // TODO: 実装が進んだらWebServerにdbの参照をセットしてから開始する
        if (server.start() != 0) {
            LOG << "server start failed.";
            return 1;
        }
        char quit = ' ';
        while (std::cin >> quit) {
            if (quit == 'q')
                break;
        }
        LOG << "application finish.";
        return 0;
    }
    catch (std::exception &e) {
        CATCH_ALL_EXCEPTIONS(LOG << e.what() << FILE_INFO;)
    }
    catch (...) {
        CATCH_ALL_EXCEPTIONS(LOG << "unexpected error or SEH exception." << FILE_INFO;)
    }
    return 1;
}