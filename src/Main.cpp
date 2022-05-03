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
        }

        // throw std::runtime_error{"-----------------------AAAAAAAAAAAAAAA"};

        logger.stream().out() << "web configuration is";
        logger.stream().out() << "webServer PORT: " << PapierMache::getValue<std::string>(webConfiguration, "webServer", "PORT");
        logger.stream().out() << "webServer MAX_SOCKETS: " << PapierMache::getValue<int>(webConfiguration, "webServer", "MAX_SOCKETS");
        logger.stream().out() << "threadsMap CLEAN_UP_POINT: " << PapierMache::getValue<int>(webConfiguration, "threadsMap", "CLEAN_UP_POINT");
        logger.stream().out() << "socketManager MAX: " << PapierMache::getValue<int>(webConfiguration, "socketManager", "MAX");
        logger.stream().out() << "socketManager TIMEOUT: " << PapierMache::getValue<int>(webConfiguration, "socketManager", "TIMEOUT");

        PapierMache::WebServer server{PapierMache::getValue<std::string>(webConfiguration, "webServer", "PORT"),
                                      PapierMache::getValue<int>(webConfiguration, "webServer", "MAX_SOCKETS")};
        logger.stream().out() << "server initialization start.";
        if (server.initialize() != 0) {
            logger.stream().out() << "server initialization failed.";
            return 1;
        }
        logger.stream().out() << "server initialization end.";
        server.start();
        return 0;
    }
    catch (std::exception &e) {
        logger.stream().out() << "----------------------------2.";
        CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
    }
    catch (...) {
        CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
    }
    logger.stream().out() << "----------------------------3.";

    return 1;
}