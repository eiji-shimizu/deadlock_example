#include "Common.h"
#include "Logger.h"
#include "WebServer.h"

#include <iostream>
#include <string>

PapierMache::Logger<std::ostream> logger{std::cout};

int main()
{
    const std::string DEFAULT_PORT = "27015";
    const int MAX_THREADS = 10;

    try {

        PapierMache::WebServer server{DEFAULT_PORT, MAX_THREADS};
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