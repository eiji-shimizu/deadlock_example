#include "Common.h"
#include "WebServer.h"

#include <string>

int main()
{
    const std::string DEFAULT_PORT = "27015";
    const int MAX_THREADS = 10;

    try {

        PapierMache::WebServer server{DEFAULT_PORT, MAX_THREADS};
        server.start();
        return 0;
    }
    catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }
    catch (...) {
        std::cout << "unexpected error." << std::endl;
    }

    return 1;

}