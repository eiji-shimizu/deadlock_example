#ifndef DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED

#include "RequestHandler.h"

namespace PapierMache {

    class DLEXRootHandler : public DefaultHandler {
    public:
        DLEXRootHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : DefaultHandler{supportMethods}
        {
        }

        virtual ~DLEXRootHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            std::cout << "----------------------DLEXRootHandler::handle" << std::endl;
            return DefaultHandler::handle(request);
        }
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED