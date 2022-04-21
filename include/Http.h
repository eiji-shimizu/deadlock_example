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

    struct HttpRequest {
        HttpRequestMethod method;
        std::string path;
        std::string protocol;
        std::map<std::string,std::string> headers;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_HTTP_INCLUDED