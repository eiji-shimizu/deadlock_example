#ifndef DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED

#include "Http.h"

#include <iostream>
#include <map>
#include <memory>
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

    class HandlerTreeNode {
    public:
        HandlerTreeNode(std::string pathName, std::unique_ptr<RequestHandler> &&pHandler)
            : pathName_{pathName},
              pHandler_{std::move(pHandler)}
        {
        }

        HandlerTreeNode(std::string pathName,
                        std::unique_ptr<RequestHandler> &&pHandler,
                        std::map<std::string, HandlerTreeNode> &&childNodes)
            : pathName_{pathName},
              pHandler_{std::move(pHandler)},
              childNodes_{std::move(childNodes)}
        {
        }

        // ムーブコンストラクタ
        HandlerTreeNode(HandlerTreeNode &&rhs)
            : pathName_{std::move(rhs.pathName_)},
              pHandler_{std::move(rhs.pHandler_)},
              childNodes_{std::move(rhs.childNodes_)}
        {
        }
        // ムーブ代入
        HandlerTreeNode &operator=(HandlerTreeNode &&rhs)
        {
            if (this == &rhs) {
                return *this;
            }
            pathName_ = std::move(rhs.pathName_);
            pHandler_ = std::move(rhs.pHandler_);
            childNodes_ = std::move(rhs.childNodes_);
        }

        ~HandlerTreeNode()
        {
            // noop
        }

        // コピー禁止
        HandlerTreeNode(const HandlerTreeNode &) = delete;
        HandlerTreeNode &operator=(const HandlerTreeNode &) = delete;

        // 引数のノードを子として追加する
        // すでに同じパスで子ノードが存在していた場合は上書きされる
        void addChildNode(HandlerTreeNode &&childNode)
        {
            childNodes_.insert_or_assign(childNode.pathName_, std::move(childNode));
        }

    private:
        std::string pathName_;
        std::unique_ptr<RequestHandler> pHandler_;
        std::map<std::string, HandlerTreeNode> childNodes_;
    };

    class HandlerTree {
    public:
        // 引数のノードをHttpリクエストメソッドごとのルートノードとして追加する
        // すでにルートノードが存在していた場合は上書きされる
        void addRootNode(HttpRequestMethod method, HandlerTreeNode root)
        {
            rootNodes_.insert_or_assign(method, std::move(root));
        }

    private:
        std::map<HttpRequestMethod, HandlerTreeNode> rootNodes_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED