#include "General.h"

#include "Common.h"
#include "Connections.h"
#include "Database.h"
#include "Logger.h"
#include "UUID.h"
#include "Utils.h"
#include "WebServer.h"

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

PapierMache::Logger<std::ostream> logger{std::cout};

const std::map<std::string, std::map<std::string, std::string>> webConfiguration{PapierMache::readConfiguration("./webconfig/server.ini")};

PapierMache::DbStuff::Database *database;

PapierMache::Connections controller;

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
        // グローバル変数にこのデータベースをセット
        database = &db;

        // TODO: 実装が進んだらWebServerにdbの参照をセットしてから開始する
        if (server.start() != 0) {
            LOG << "server start failed.";
            return 1;
        }
        char c = ' ';
        while (std::cin >> c) {
            if (c == 'q') {
                break;
            }
            else if (c == 'd') {
                controller.terminateAll();
                LOG << "controller.terminateAll";
            }
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