#ifndef DEADLOCK_EXAMPLE_HTTP_INCLUDED
#define DEADLOCK_EXAMPLE_HTTP_INCLUDED

#include <map>
#include <sstream>
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

        void setHttpRequestMethodFromText(std::string s)
        {
            if (s == "GET")
                method = HttpRequestMethod::GET;
            else if (s == "HEAD")
                method = HttpRequestMethod::HEAD;
            else if (s == "POST")
                method = HttpRequestMethod::POST;
            else if (s == "PUT")
                method = HttpRequestMethod::PUT;
            else if (s == "DELETE")
                method = HttpRequestMethod::DELETE;
            else if (s == "CONNECT")
                method = HttpRequestMethod::CONNECT;
            else if (s == "OPTIONS")
                method = HttpRequestMethod::OPTIONS;
            else if (s == "TRACE")
                method = HttpRequestMethod::TRACE;
            else if (s == "PATCH")
                method = HttpRequestMethod::PATCH;
            else
                throw std::runtime_error("invalid http request method.");
        }

        std::string httpRequestMethodTextValue() const
        {
            switch (method) {
            case HttpRequestMethod::GET:
                return "GET";
            case HttpRequestMethod::HEAD:
                return "HEAD";
            case HttpRequestMethod::POST:
                return "POST";
            case HttpRequestMethod::PUT:
                return "PUT";
            case HttpRequestMethod::DELETE:
                return "DELETE";
            case HttpRequestMethod::CONNECT:
                return "CONNECT";
            case HttpRequestMethod::OPTIONS:
                return "OPTIONS";
            case HttpRequestMethod::TRACE:
                return "TRACE";
            case HttpRequestMethod::PATCH:
                return "PATCH";
            }
        }

        std::string toString() const
        {
            std::ostringstream oss{""};
            oss << httpRequestMethodTextValue() << " "
                << path << " "
                << protocol << std::endl;
            for (const auto &e : headers) {
                oss << e.first << ": " << e.second << std ::endl;
            }
            return oss.str();
        }
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_HTTP_INCLUDED