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

//PapierMache::DbStuff::Database database{};

void testFunc(PapierMache::DbStuff::Connection con, std::string name, std::string message)
{
    int i = 5;
    std::vector<std::byte> data;
    for (const char c : message) {
        data.push_back(static_cast<std::byte>(c));
    }
    std::ostringstream oss{""};
    // logger.stream().out() << name << ": send() BEFORE";
    con.send(data);
    // logger.stream().out() << name << ": send() AFTER";
    // logger.stream().out() << name << ": request() BEFORE";
    bool requestResult = con.request();
    // std::cout << requestResult << std::endl;
    // if (name != "con1") {
    //     // requestResult = requestResult;
    //     // if (!requestResult) {
    //     //     // return name + ": request() failed";
    //     //     //  std::cout << "@" << std::endl;
    //     //     throw std::runtime_error{name + ": request() failed"};
    //     // }
    //     // requestResult = !requestResult;
    // }
    // // std::cout << "@" << std::endl;
    if (!requestResult) {
        // return name + ": request() failed";
        //  std::cout << "@" << std::endl;
        // std::cerr << name << ": request() failed...";
        throw std::runtime_error{name + ": request() failed"};
    }
    // logger.stream().out() << name << ": request() AFTER";
    if (requestResult) {
        std::cout << name << ": wait() BEFORE" << std::endl;
        int waitResult = con.wait();
        logger.stream().out() << name << ": wait() AFTER";
        if (waitResult == 0) {
            con.receive(data);

            for (const auto b : data) {
                oss << static_cast<char>(b);
            }
            logger.stream().out() << name << ": " << con.id() << " " << oss.str();
        }
        else if (waitResult == -1) {
            // リトライ
            logger.stream().out() << name << ": wait() failed first";
            if (con.wait() == 0) {
                con.receive(data);
                for (const auto b : data) {
                    oss << static_cast<char>(b);
                }
                logger.stream().out() << name << ": " << con.id() << " " << oss.str();
            }
            else {
                throw std::runtime_error{name + ": wait() failed twice."};
            }
        }
    }
}

int main()
{
    try {
        LOG << "normal";
        WEB_LOG << "web";
        DB_LOG << "db";
        {
            PapierMache::DbStuff::Database db{};

            db.start();
            db.start();
            db.start();
            db.start();

            // std::this_thread::sleep_for(std::chrono::seconds(3));

            PapierMache::DbStuff::Connection con = db.getConnection();
            testFunc(con, "con1", "PLEASE:TRANSACTION");
            // // testFunc(con, "con1", "PLEASE:INSERT");
            // //  con.close();

            PapierMache::DbStuff::Connection con2 = db.getConnection();
            testFunc(con2, "con2", "PLEASE:UPDATE");
            // // con2.close();

            PapierMache::DbStuff::Connection con3 = db.getConnection();
            testFunc(con3, "con3", "PLEASE:TRANSACTION");
            testFunc(con3, "con3", "PLEASE:TRANSACTION");
            testFunc(con3, "con3", "PLEASE:DELETE");
            // con3.close();
        }

        LOG << "------------------------------";

        // throw std::runtime_error{"-----------------------AAAAAAAAAAAAAAA"};

        WEB_LOG << "web configuration is";
        WEB_LOG << "webServer PORT: " << PapierMache::getValue<std::string>(webConfiguration, "webServer", "PORT");
        WEB_LOG << "webServer MAX_SOCKETS: " << PapierMache::getValue<int>(webConfiguration, "webServer", "MAX_SOCKETS");
        WEB_LOG << "threadsMap CLEAN_UP_POINT: " << PapierMache::getValue<int>(webConfiguration, "threadsMap", "CLEAN_UP_POINT");
        WEB_LOG << "socketManager MAX: " << PapierMache::getValue<int>(webConfiguration, "socketManager", "MAX");
        WEB_LOG << "socketManager TIMEOUT: " << PapierMache::getValue<int>(webConfiguration, "socketManager", "TIMEOUT");

        PapierMache::WebServer server{PapierMache::getValue<std::string>(webConfiguration, "webServer", "PORT"),
                                      PapierMache::getValue<int>(webConfiguration, "webServer", "MAX_SOCKETS")};
        WEB_LOG << "server initialization start.";
        if (server.initialize() != 0) {
            WEB_LOG << "server initialization failed.";
            return 1;
        }
        WEB_LOG << "server initialization end.";
        server.start();
        return 0;
    }
    catch (std::exception &e) {
        LOG << "----------------------------2.";
        CATCH_ALL_EXCEPTIONS(LOG << e.what();)
    }
    catch (...) {
        CATCH_ALL_EXCEPTIONS(LOG << "unexpected error or SEH exception.";)
    }
    LOG << "----------------------------3.";

    return 1;
}