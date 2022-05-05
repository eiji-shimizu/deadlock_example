#ifndef DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED

#include "General.h"

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
            DEBUG_LOG << "----------------------DLEXRootHandler::handle";
            return DefaultHandler::handle(request);
        }
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED