#ifndef DEADLOCK_EXAMPLE_HTTP_INCLUDED
#define DEADLOCK_EXAMPLE_HTTP_INCLUDED

#include "General.h"

#include <map>
#include <sstream>
#include <string>

namespace PapierMache {

    enum class HttpRequestMethod {
        GET,
        HEAD,
        POST,
        PUT,
#pragma push_macro("DELETE")
#undef DELETE
        DELETE,
#pragma pop_macro("DELETE")
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

    std::string toStringFromStatusCode(const HttpResponseStatusCode sc)
    {
        switch (sc) {
        case HttpResponseStatusCode::CONTINUE:
            return std::to_string(static_cast<int>(sc)) + " Continue";
        case HttpResponseStatusCode::OK:
            return std::to_string(static_cast<int>(sc)) + " OK";
        case HttpResponseStatusCode::CREATED:
            return std::to_string(static_cast<int>(sc)) + " Created";
        case HttpResponseStatusCode::MULTIPLE_CHOICE:
            return std::to_string(static_cast<int>(sc)) + " Multiple Choice";
        case HttpResponseStatusCode::BAD_REQUEST:
            return std::to_string(static_cast<int>(sc)) + " Bad Request";
        case HttpResponseStatusCode::UNAUTHORIZED:
            return std::to_string(static_cast<int>(sc)) + " Unauthorized";
        case HttpResponseStatusCode::FORBIDDEN:
            return std::to_string(static_cast<int>(sc)) + " Forbidden";
        case HttpResponseStatusCode::NOT_FOUND:
            return std::to_string(static_cast<int>(sc)) + " Not Found";
        case HttpResponseStatusCode::METHOD_NOT_ALLOWED:
            return std::to_string(static_cast<int>(sc)) + " Method Not Allowed";
        case HttpResponseStatusCode::INTERNAL_SERVER_ERROR:
            return std::to_string(static_cast<int>(sc)) + " Internal Server Error";
        case HttpResponseStatusCode::NOT_IMPLEMENTED:
            return std::to_string(static_cast<int>(sc)) + " Not Implemented";
        case HttpResponseStatusCode::BAD_GATEWAY:
            return std::to_string(static_cast<int>(sc)) + " Bad Gateway";
        case HttpResponseStatusCode::SERVICE_UNAVAILABLE:
            return std::to_string(static_cast<int>(sc)) + " Service Unavailable";
        }
        return std::to_string(static_cast<int>(sc));
    }

    struct HttpRequest {
        HttpRequestMethod method;
        std::string path;
        std::string protocol;
        std::map<std::string, std::string> headers;
        std::string body;

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
#pragma push_macro("DELETE")
#undef DELETE
                method = HttpRequestMethod::DELETE;
#pragma pop_macro("DELETE")
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
#pragma push_macro("DELETE")
#undef DELETE
            case HttpRequestMethod::DELETE:
#pragma pop_macro("DELETE")
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
            throw std::runtime_error("invalid http request method.");
        }

        std::string toString() const
        {
            std::ostringstream oss{""};
            oss << httpRequestMethodTextValue() << " "
                << path << " "
                << protocol << std::endl;
            for (const auto &e : headers) {
                oss << e.first << ": " << e.second << std::endl;
            }
            oss << body << std::endl;
            return oss.str();
        }
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_HTTP_INCLUDED