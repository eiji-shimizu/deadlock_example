#include "Common.h"
#include "Logger.h"
#include "Utils.h"
#include "WebServer.h"

#include <iostream>
#include <string>

PapierMache::Logger<std::ostream> logger{std::cout};

std::map<std::string, std::map<std::string, std::string>> webConfiguration;

int main()
{
    try {
        webConfiguration = PapierMache::readConfiguration("./webconfig/server.ini");
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
        CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
    }
    catch (...) {
        CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
    }

    return 1;
}