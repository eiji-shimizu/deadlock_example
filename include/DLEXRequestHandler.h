#ifndef DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED

#include "General.h"

#include "Database.h"
#include "RequestHandler.h"

#include <map>
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

    // 画面から送られてくる受注の内容のjson文字列をマップにして返す
    // valueにダブルコーテーションは使用できない
    inline std::map<std::string, std::string> parseOrderJson(const std::string s)
    {
        std::map<std::string, std::string> result;
        std::ostringstream key{""};
        std::ostringstream value{""};
        bool isValue = false;
        bool isInnerDq = false;
        std::string temp = s;
        WEB_LOG << temp;
        temp = trim(temp, '{');
        temp = trim(temp, '}');
        WEB_LOG << temp;
        for (const char c : temp) {
            if (c == ':') {
                if (!isInnerDq) {
                    isValue = true;
                }
            }
            else if (c == ',') {
                if (!isInnerDq) {
                    isValue = false;
                    result.insert(std::make_pair(key.str(), value.str()));
                    key.str("");
                    value.str("");
                }
            }
            else if (c == '"') {
                if (isInnerDq) {
                    isInnerDq = false;
                }
                else {
                    isInnerDq = true;
                }
            }
            else if (isValue) {
                value << c;
            }
            else if (!isValue) {
                key << c;
            }
        }
        if (key.str().length() > 0) {
            result.insert(std::make_pair(key.str(), value.str()));
            key.str("");
            value.str("");
        }
        return result;
    }

    class Cleaner {
    public:
        Cleaner(DbStuff::Connection &con)
            : con_{con}
        {
        }
        ~Cleaner()
        {
            CATCH_ALL_EXCEPTIONS(con_.close();)
        }

    private:
        DbStuff::Connection &con_;
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
            if (!isSupport(request.method)) {
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::METHOD_NOT_ALLOWED;
                return hr;
            }

            DbStuff::Connection con = db().getConnection();
            Cleaner cleaner{con};
            DbStuff::Driver driver(con);
            std::string user = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            std::string password = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            DbStuff::Driver::Result r = driver.sendQuery("please:user " + getValue<std::string>(webConfiguration, "database", "USER_NAME") +
                                                         " " + getValue<std::string>(webConfiguration, "database", "PASSWORD"));

            r = driver.sendQuery("please:transaction");
            if (!r.isSucceed) {
                std::string resultJson = "{\"result\": -1, \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_1")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            r = driver.sendQuery("please: select order");
            if (!r.isSucceed) {
                std::string resultJson = "{\"result\": -1, \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_1")) + "}";
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
            std::string result = "0";
            std::string messageCode = "MESSAGE_1";
            if (r.rows.size() == 0) {
                messageCode = "MESSAGE_3";
            }
            std::string resultJson = "{\"result\": " + result + ", \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", messageCode)) + "," +
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
            if (!isSupport(request.method)) {
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::METHOD_NOT_ALLOWED;
                return hr;
            }

            std::map<std::string, std::string> data = parseOrderJson(request.body);

            DbStuff::Connection con = db().getConnection();
            Cleaner cleaner{con};
            DbStuff::Driver driver(con);
            std::string user = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            std::string password = getValue<std::string>(webConfiguration, "database", "USER_NAME");
            DbStuff::Driver::Result r = driver.sendQuery("please:user " + getValue<std::string>(webConfiguration, "database", "USER_NAME") +
                                                         " " + getValue<std::string>(webConfiguration, "database", "PASSWORD"));

            r = driver.sendQuery("please:transaction");
            if (!r.isSucceed) {
                std::string resultJson = "{\"result\": -1, \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_2")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            r = driver.sendQuery("please:insert  order (ORDER_NAME=" + setDq(data.at("orderName")) + ", CUSTOMER_NAME=" + setDq(data.at("customerName")) + ", PRODUCT_NAME=" + setDq(data.at("productName")) + ")");
            if (!r.isSucceed) {
                std::string resultJson = "{\"result\": -1, \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_2")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            r = driver.sendQuery("please:commit");
            if (!r.isSucceed) {
                std::string resultJson = "{\"result\": -1, \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "ERROR_2")) + "}";
                HandlerResult hr{};
                hr.status = HttpResponseStatusCode::OK;
                hr.mediaType = "application/json";
                hr.responseBody = toBytesFromString(resultJson);
                return hr;
            }
            bool b = con.close();
            HandlerResult hr{};
            hr.status = HttpResponseStatusCode::OK;
            std::string resultJson = "{\"result\": 0, \"message\": " + setDq(getValue<std::string>(webConfiguration, "messages", "MESSAGE_2")) + "}";
            hr.status = HttpResponseStatusCode::OK;
            hr.mediaType = "application/json";
            hr.responseBody = toBytesFromString(resultJson);
            DEBUG_LOG << "----------------------DLEXAddOrderHandler::handle END";
            return hr;
        }
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_DLEX_REQUESTHANDLER_INCLUDED