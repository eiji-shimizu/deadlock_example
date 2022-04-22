#ifndef DEADLOCK_EXAMPLE_HTTP_INCLUDED
#define DEADLOCK_EXAMPLE_HTTP_INCLUDED

#include <map>
#include <string>

namespace PapierMache {

    enum class HttpRequestMethod {
        GET,
        HEAD,
        POST,
        PUT,
        DELETE,
        CONNECT,
        OPTIONS,
        TRACE,
        PATCH
    };

    enum class HttpResponseStatusCode {
        CONTINUE = 100,
        OK = 200,
        CREATED = 201,
        MULTIPLE_CHOICE = 300,
        BAD_REQUEST = 400,
        UNAUTHORIZED = 401,
        FORBIDDEN = 403,
        NOT_FOUND = 404,
        METHOD_NOT_ALLOWED = 405,
        INTERNAL_SERVER_ERROR = 500,
        NOT_IMPLEMENTED = 501,
        BAD_GATEWAY = 502,
        SERVICE_UNAVAILABLE = 503
    };

    struct HttpRequest {
        HttpRequestMethod method;
        std::string path;
        std::string protocol;
        std::map<std::string, std::string> headers;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_HTTP_INCLUDED