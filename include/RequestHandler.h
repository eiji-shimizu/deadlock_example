#ifndef DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED
#define DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED

#include "Http.h"
#include "Utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace PapierMache {

    struct HandlerResult {
        HttpResponseStatusCode status;
        std::string mediaType;
        std::vector<std::byte> responseBody;
    };

    class RequestHandler {
    public:
        RequestHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : supportMethods_{supportMethods}
        {
        }
        virtual ~RequestHandler() {}

        bool isSupport(HttpRequestMethod method)
        {
            for (const HttpRequestMethod m : supportMethods_) {
                if (m == method) {
                    return true;
                }
            }
            return false;
        }

        virtual HandlerResult handle(const HttpRequest request) = 0;

    private:
        std::vector<HttpRequestMethod> supportMethods_;
    };

    class DefaultHandler : public RequestHandler {
    public:
        DefaultHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : RequestHandler{supportMethods}
        {
        }

        virtual ~DefaultHandler() {}

        // 単にurlで指定されたリソースを読み込んで返す
        // ただしルート(/)が指定された場合はindex.htmlを返す
        virtual HandlerResult handle(const HttpRequest request)
        {
            logger.stream().out() << "----------------------DefaultHandler::handle";

            std::filesystem::path resourcePath{"sites"};
            if (request.path == "/") {
                resourcePath /= "index.html";
            }
            else {
                if (request.path.length() <= 253) {
                    resourcePath /= trim(request.path, '/');
                }
            }
            std::byte b;
            std::ifstream ifs{resourcePath, std::ios_base::binary};
            std::vector<std::byte> v;
            while (ifs.read(as_bytes(b), sizeof(std::byte))) {
                v.push_back(b);
            }

            HandlerResult hr{};
            hr.responseBody = std::move(v);
            // メディアタイプの判別
            // 以下の文字列処理は非常に簡易的なもの
            std::string extension = resourcePath.extension().string();
            extension = trim(extension, '.');
            logger.stream().out() << "extension: " << extension;
            if (extension == "ico") {
                hr.mediaType = std::string{"image/vnd.microsoft."} + extension;
            }
            else if (extension == "png") {
                hr.mediaType = std::string{"image/"} + extension;
            }
            else if (resourcePath.extension() == "html") {
                hr.mediaType = std::string{"text/"} + extension;
            }
            return hr;
        }
    };

    class RootHandler : public DefaultHandler {
    public:
        RootHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : DefaultHandler{supportMethods}
        {
        }

        virtual ~RootHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            logger.stream().out() << "----------------------RootHandler::handle";
            return DefaultHandler::handle(request);
        }
    };

    class HelloWorldRootHandler : public DefaultHandler {
    public:
        HelloWorldRootHandler(std::initializer_list<HttpRequestMethod> supportMethods)
            : DefaultHandler{supportMethods}
        {
        }

        virtual ~HelloWorldRootHandler() {}

        virtual HandlerResult handle(const HttpRequest request)
        {
            logger.stream().out() << "----------------------HelloWorldRootHandler::handle";
            return DefaultHandler::handle(request);
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
            return *this;
        }

        ~HandlerTreeNode()
        {
            // noop
        }

        // コピー禁止
        HandlerTreeNode(const HandlerTreeNode &) = delete;
        HandlerTreeNode &operator=(const HandlerTreeNode &) = delete;

        static HandlerTreeNode &nullObject()
        {
            static HandlerTreeNode dummy{"", nullptr, std::map<std::string, HandlerTreeNode>{}};
            return dummy;
        }

        // 引数のノードを子として追加する
        // すでに同じパスで子ノードが存在していた場合は上書きされる
        void addChildNode(HandlerTreeNode &&childNode)
        {
            std::string path = removeSpace(childNode.pathName());
            if (path == "") {
                throw std::runtime_error{"path is empty."};
            }
            childNodes_.insert_or_assign(childNode.pathName_, std::move(childNode));
        }

        std::string pathName() const { return pathName_; }

        RequestHandler &handler()
        {
            if (pHandler_) {
                return *pHandler_;
            }
            throw std::runtime_error{"handler is null."};
        }

        bool isHandlerNull() const
        {
            if (!pHandler_) {
                return true;
            }
            return false;
        }

        HandlerTreeNode &findNode(const std::string relativePath)
        {
            std::ostringstream oss{""};
            for (const char c : relativePath) {
                if (c == '/') {
                    if (oss.str() != "") {
                        if (pathName_ == oss.str()) {
                            if (oss.str().length() == relativePath.length()) {
                                return *this;
                            }
                            else {
                                std::ostringstream childRelativePath{""};
                                std::for_each(std::next(relativePath.cbegin(), oss.str().length() + 1), relativePath.cend(), [&childRelativePath](char c) {
                                    childRelativePath << c;
                                });
                                logger.stream().out() << "childRelativePath: " << childRelativePath.str();
                                for (auto &e : childNodes_) {
                                    HandlerTreeNode &result = e.second.findNode(childRelativePath.str());
                                    if (&result != &nullObject()) {
                                        return result;
                                    }
                                }
                            }
                        }
                    }
                    else {
                        // ここに来た場合は絶対パスが指定されているのでエラー
                        throw std::runtime_error{"path parameter must be relative path."};
                    }
                }
                else {
                    oss << c;
                }
            }
            if (oss.str() != "") {
                if (pathName_ == oss.str()) {
                    return *this;
                }
            }
            return nullObject();
        }

    private:
        std::string pathName_;
        std::unique_ptr<RequestHandler> pHandler_;
        std::map<std::string, HandlerTreeNode> childNodes_;
    };

    class HandlerTree {
    public:
        HandlerTree()
            : defaultHandlerNode_{"", std::make_unique<DefaultHandler>(std::initializer_list<HttpRequestMethod>({HttpRequestMethod::GET}))}
        {
        }
        ~HandlerTree() {}

        // 引数のノードをHttpリクエストメソッドごとのルートノードとして追加する
        // すでにルートノードが存在していた場合は例外を投げる
        void addRootNode(HandlerTreeNode &&rootNode)
        {
            std::string path = removeSpace(rootNode.pathName());
            if (path == "") {
                throw std::runtime_error{"path is empty."};
            }
            if (rootNodes_.find(rootNode.pathName()) != rootNodes_.end()) {

                std::ostringstream oss{""};
                oss << "root node : " << rootNode.pathName() << " is already exists.";
                throw std::runtime_error{oss.str()};
            }
            rootNodes_.insert(std::make_pair(rootNode.pathName(), std::move(rootNode)));
        }

        HandlerTreeNode &findHandlerNode(const std::string absolutePath)
        {
            for (auto &e : rootNodes_) {
                HandlerTreeNode &result = e.second.findNode(removeExtension(trim(absolutePath, '/')));
                if (&result != &HandlerTreeNode::nullObject()) {
                    return result;
                }
            }
            return defaultHandlerNode_;
        }

        // コピー禁止
        HandlerTree(const HandlerTree &) = delete;
        HandlerTree &operator=(const HandlerTree &) = delete;
        // ムーブ禁止
        HandlerTree(HandlerTree &&) = delete;
        HandlerTree &operator=(HandlerTree &&) = delete;

    private:
        std::map<std::string, HandlerTreeNode> rootNodes_;
        HandlerTreeNode defaultHandlerNode_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_REQUESTHANDLER_INCLUDED