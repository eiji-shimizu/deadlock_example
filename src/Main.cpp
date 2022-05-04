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

PapierMache::DbStuff::Database database{};

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
            PapierMache::DbStuff::Connection con = db.getConnection();
            std::vector<std::byte> data;
            std::string req = "PLEASE:INSERT";
            for (const char c : req) {
                data.push_back(static_cast<std::byte>(c));
            }
            con.beginTransaction();
            con.send(data);
            con.request();
            con.wait();
            con.receive(data);
            // std::ostringstream oss{""};
            // for (const auto b : data) {
            //     oss << static_cast<char>(b);
            // }
            // logger.stream().out() << oss.str();
            // oss.str("");
            con.close();

            PapierMache::DbStuff::Connection con2 = db.getConnection();
            data.clear();
            req = "PLEASE:UPDATE";
            for (const char c : req) {
                data.push_back(static_cast<std::byte>(c));
            }
            con2.beginTransaction();
            con2.send(data);
            con2.request();
            con2.wait();
            con2.receive(data);
            // for (const auto b : data) {
            //     oss << static_cast<char>(b);
            // }
            // logger.stream().out() << oss.str();
            // oss.str("");
            con2.close();

            PapierMache::DbStuff::Connection con3 = db.getConnection();
            data.clear();
            req = "PLEASE:DELETE";
            for (const char c : req) {
                data.push_back(static_cast<std::byte>(c));
            }
            con3.beginTransaction();
            con3.send(data);
            con3.request();
            con3.wait();
            con3.receive(data);
            // for (const auto b : data) {
            //     oss << static_cast<char>(b);
            // }
            // logger.stream().out() << oss.str();
            // oss.str("");
            con3.close();
        }

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