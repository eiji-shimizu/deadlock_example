#ifndef DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED

#include "Http.h"

#include <iostream>
#include <string>

namespace PapierMache {

    struct HandlerResult {
        HttpResponseStatusCode status;
        std::string responseBody;
    };

    class RequestHandler {
    public:
        RequestHandler() {}
        virtual ~RequestHandler() {}

        virtual HandlerResult handle(const HttpRequest request) = 0;
    };

    class DefaultHandler : public RequestHandler {
        DefaultHandler() {}
        virtual ~DefaultHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            std::cout << "DefaultHandler::handle" << std::endl;
        }
    };

    class RootHandler : public RequestHandler {
    public:
        RootHandler() {}
        virtual ~RootHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            std::cout << "RootHandler::handle" << std::endl;
        }
    };

    struct HandlerTreeNode {
        std::string pathName;
    };

    class HandlerTree {

    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED