#include "Common.h"
#include "Logger.h"
#include "WebServer.h"

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
        server.start();
        return 0;
    }
    catch (std::exception &e) {
        logger.stream().out() << e.what();
    }
    catch (...) {
        logger.stream().out() << "unexpected error.";
    }

    return 1;
}