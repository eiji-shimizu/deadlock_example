#ifndef DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED

#include "General.h"

#include "Database.h"
#include "RequestHandler.h"

#include <sstream>
#include <string>
#include <vector>

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

    class DLEXOrderHandler : public RequestHandler {
    public:
        DLEXOrderHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : RequestHandler{supportMethods}
        {
        }

        virtual ~DLEXOrderHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            DEBUG_LOG << "----------------------DLEXOrderHandler::handle";

            DbStuff::Connection con = db().getConnection();
            DbStuff::Driver driver(con);
            std::string user = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            std::string password = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            DbStuff::Driver::Result r = driver.sendQuery("please:user " + getValue<std::string>(webConfiguration, "database", "USER_NAME") +
                                                         " " + getValue<std::string>(webConfiguration, "database", "PASSWORD"));

            r = driver.sendQuery("please:transaction");
            if (!r.isSucceed) {
                std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_1")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            r = driver.sendQuery("please: select order");
            if (!r.isSucceed) {
                std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_1")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            bool b = con.close();
            HandlerResult hr{};
            hr.status = HttpResponseStatusCode::OK;

            std::vector<std::string> v;
            for (const auto &e : r.rows) {
                std::ostringstream oss{""};
                oss << "{";
                int count = e.size();
                for (const auto &p : e) {
                    if (p.first != "password") {
                        std::ostringstream value{""};
                        for (const char c : p.second) {
                            if (c == 0) {
                                break;
                            }
                            value << c;
                        }
                        oss << setDq(p.first) + ": " << setDq(value.str());
                        --count;
                        if (count != 0) {
                            oss << ",";
                        }
                    }
                }
                oss << "}";
                v.push_back(oss.str());
            }
            std::ostringstream data{""};
            data << "[";
            int count = v.size();
            for (const std::string e : v) {
                data << e;
                --count;
                if (count != 0) {
                    data << ",";
                }
            }
            data << "]";
            std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "MESSAGE_1")) + "," +
                                     "\"data\": " + data.str() + "}";
            hr.status = HttpResponseStatusCode::OK;
            hr.mediaType = "application/json";
            hr.responseBody = toBytesFromString(resultJson);
            DEBUG_LOG << "----------------------DLEXOrderHandler::handle END";
            return hr;
        }
    };

    class DLEXAddOrderHandler : public RequestHandler {
    public:
        DLEXAddOrderHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : RequestHandler{supportMethods}
        {
        }

        virtual ~DLEXAddOrderHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            DEBUG_LOG << "----------------------DLEXAddOrderHandler::handle";

            DbStuff::Connection con = db().getConnection();
            DbStuff::Driver driver(con);
            std::string user = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            std::string password = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            DbStuff::Driver::Result r = driver.sendQuery("please:user " + getValue<std::string>(webConfiguration, "database", "USER_NAME") +
                                                         " " + getValue<std::string>(webConfiguration, "database", "PASSWORD"));

            r = driver.sendQuery("please:transaction");
            if (!r.isSucceed) {
                std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_2")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + setDq("order1") + ", CUSTOMER_NAME=" + setDq("お客様A") + ", PRODUCT_NAME=" + setDq("商品いろはにほへと") + ")");
            if (!r.isSucceed) {
                std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_2")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            r = driver.sendQuery("please:commit");
            if (!r.isSucceed) {
                std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_2")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            bool b = con.close();
            HandlerResult hr{};
            hr.status = HttpResponseStatusCode::OK;
            std::string resultJson = "{\"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "MESSAGE_1")) + "}";
            hr.status = HttpResponseStatusCode::OK;
            hr.mediaType = "application/json";
            hr.responseBody = toBytesFromString(resultJson);
            DEBUG_LOG << "----------------------DLEXAddOrderHandler::handle END";
            return hr;
        }
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED