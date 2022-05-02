#ifndef DEADLOCK_EXAMPLE_WEBSERVER_INCLUDED
#define DEADLOCK_EXAMPLE_WEBSERVER_INCLUDED

#include "General.h"

#pragma comment(lib, "ws2_32.lib")

#include "Common.h"
#include "DLEXRequestHandler.h"
#include "Http.h"
#include "Logger.h"
#include "RequestHandler.h"
#include "ThreadsMap.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace PapierMache {

    enum class SocketStatus {
        CREATED,
        RECV,
        RECEIVING,
        PROCESSING,
        SENDING,
        COMPLETED,
        TO_CLOSE,
        SOCKET_IS_NONE // ソケットが見つからない場合に用いる
    };

    class SocketManager;
    class SocketHolder {
        friend SocketManager;

    public:
        SocketHolder(SOCKET socket, SocketStatus status)
            : socket_{socket},
              status_{status},
              lastTime_{std::chrono::system_clock::now()}
        {
        }

        // コピー代入
        SocketHolder &operator=(const SocketHolder &rhs)
        {
            if (this == &rhs) {
                return *this;
            }
            socket_ = rhs.socket_;
            status_ = rhs.status_;
            lastTime_ = rhs.lastTime_;
            return *this;
        }

        SocketStatus status() const { return status_; }

        void setStatus(const SocketStatus status)
        {
            status_ = status;
        }

        void setLastTime(const std::chrono::system_clock::time_point lastTime)
        {
            lastTime_ = lastTime;
        }

        bool isTheSame(const SOCKET s) const
        {
            return socket_ == s;
        }

        void shutDownAndClose()
        {
            // コネクションをシャットダウン
            int shutDownResult = 0;
            logger.stream().out() << "socket : " << socket_ << " shutdown BEFORE";
            shutDownResult = shutdown(socket_, SD_BOTH);
            logger.stream().out() << "socket : " << socket_ << " shutdown AFTER";
            if (shutDownResult == SOCKET_ERROR) {
                logger.stream().out() << "socket : " << socket_ << " shutdown failed with error: " << WSAGetLastError();
            }
            // コネクションをクローズ
            logger.stream().out() << "socket : " << socket_ << " closesocket BEFORE";
            closesocket(socket_);
            logger.stream().out() << "socket : " << socket_ << " closesocket AFTER";
        }

    private:
        SOCKET socket_;
        SocketStatus status_;
        // このソケットで最後にクライアントとやり取りした時間を設定する
        std::chrono::system_clock::time_point lastTime_;
    };

    class SocketManager {
    public:
        SocketManager(int max, int timeout)
            : max_{max},
              timeout_{timeout}
        {
        }
        SocketManager()
            : max_{10},
              timeout_{15}
        {
        }
        ~SocketManager()
        {
            CATCH_ALL_EXCEPTIONS(
                if (thread_.joinable()) {
                    thread_.join();
                })
        }

        void startMonitor()
        {
            thread_ = std::thread{[this] {
                try {
                    monitor();
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
                }
            }};
        }

        bool addSocket(const SOCKET s)
        {
            std::lock_guard<std::mutex> lock{mt_};
            if (sockets_.size() < max_) {
                sockets_.push_back({s, SocketStatus::CREATED});
                return true;
            }
            return false;
        };

        bool setLastTime(SOCKET s, const std::chrono::system_clock::time_point lastTime)
        {
            std::lock_guard<std::mutex> lock{mt_};
            for (SocketHolder &sh : sockets_) {
                if (sh.isTheSame(s)) {
                    sh.setLastTime(lastTime);
                    return true;
                }
            }
            return false;
        }

        bool setStatus(SOCKET s, const SocketStatus status)
        {
            std::lock_guard<std::mutex> lock{mt_};
            for (SocketHolder &sh : sockets_) {
                if (sh.isTheSame(s)) {
                    sh.setStatus(status);
                    return true;
                }
            }
            return false;
        }

        // sの現在の状態に対して設定可能なものであれば新たに状態を設定する
        // 戻り値は設定前の状態
        SocketStatus setStatusIfEnable(SOCKET s, const SocketStatus status)
        {
            std::lock_guard<std::mutex> lock{mt_};
            for (SocketHolder &sh : sockets_) {
                if (sh.isTheSame(s)) {
                    if (status == SocketStatus::RECV) {
                        if (sh.status() == SocketStatus::RECEIVING) {
                            // 既に受信中の場合はRECVは設定しない
                            return sh.status();
                        }
                    }
                    if (sh.status() == SocketStatus::TO_CLOSE || sh.status() == SocketStatus::SOCKET_IS_NONE) {
                        // 有効なソケットではない場合
                        return sh.status();
                    }

                    sh.setStatus(status);
                    return sh.status();
                }
            }
            return SocketStatus::SOCKET_IS_NONE;
        }

        SocketStatus getStatus(SOCKET s)
        {
            std::lock_guard<std::mutex> lock{mt_};
            for (const SocketHolder &sh : sockets_) {
                if (sh.isTheSame(s)) {
                    return sh.status();
                }
            }
            return SocketStatus::SOCKET_IS_NONE;
        }

        void addOverCapacitySocket(const SOCKET s)
        {
            std::lock_guard<std::mutex> lock{mt_};
            overCapacity_.push_back({s, SocketStatus::TO_CLOSE});
        };

        // コピー禁止
        SocketManager(const SocketManager &) = delete;
        SocketManager &operator=(const SocketManager &) = delete;
        // ムーブ禁止
        SocketManager(SocketManager &&) = delete;
        SocketManager &operator=(SocketManager &&) = delete;

    private:
        void monitor()
        {
            try {
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{mt_};
                        // タイムアウトしたとみなせるソケットには状態を設定
                        std::for_each(sockets_.begin(), sockets_.end(), [timeout = timeout_](SocketHolder &ref) {
                            std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
                            std::chrono::system_clock::time_point tp = ref.lastTime_ + std::chrono::seconds(timeout);
                            if (!(now < tp) && ref.status() == SocketStatus::RECV) {
                                ref.setStatus(SocketStatus::TO_CLOSE);
                            }
                        });
                        // クローズすべきソケットを判別して処理する
                        std::vector<SocketHolder> temp;
                        std::remove_copy_if(sockets_.begin(), sockets_.end(), std::back_inserter(temp),
                                            [](SocketHolder sh) {
                                                return sh.status() != SocketStatus::TO_CLOSE;
                                            });
                        // クローズ処理
                        std::for_each(temp.begin(), temp.end(), [](SocketHolder sh) {
                            try {
                                sh.shutDownAndClose();
                            }
                            catch (std::exception &e) {
                                logger.stream().out() << e.what();
                            }
                        });

                        // リストに有効なソケットのみを残す
                        auto result = std::remove_if(sockets_.begin(), sockets_.end(),
                                                     [](SocketHolder sh) {
                                                         return sh.status() == SocketStatus::TO_CLOSE;
                                                     });
                        logger.stream().out() << "----------1.sockets_.size : " << sockets_.size();
                        sockets_.erase(result, sockets_.end());
                        logger.stream().out() << "----------2.sockets_.size : " << sockets_.size();

                        // 容量オーバーであったソケットがあればクローズ
                        std::for_each(overCapacity_.begin(), overCapacity_.end(), [](SocketHolder sh) {
                            try {
                                sh.shutDownAndClose();
                            }
                            catch (std::exception &e) {
                                logger.stream().out() << e.what();
                            }
                        });
                        overCapacity_.clear();

                    } // Scoped Lock end
                }
            }
            catch (std::exception &e) {
                logger.stream().out() << e.what();
            }
        }

        const int max_;
        std::vector<SocketHolder> sockets_;
        // 管理対象数を超えて生成されたソケットを一時的に格納してほどなくクローズするためのvector
        std::vector<SocketHolder> overCapacity_;
        const int timeout_;
        std::thread thread_;
        std::mutex mt_;
    };

    class Receiver {
    public:
        Receiver(SocketManager &refSocketManager, ThreadsMap &refThreadsMap, HandlerTree &refHandlerTree)
            : refSocketManager_{refSocketManager},
              refThreadsMap_{refThreadsMap},
              refHandlerTree_{refHandlerTree}
        {
        }

        ~Receiver()
        {
            CATCH_ALL_EXCEPTIONS(refThreadsMap_.setFinishedFlag(std::this_thread::get_id());)
        }

        void receive(const SOCKET clientSocket)
        {
            const int DEFAULT_BUFLEN = 512;

            try {
                char buf[DEFAULT_BUFLEN];
                char recvBuf[DEFAULT_BUFLEN];
                int result;
                int iSendResult = SOCKET_ERROR;

                memset(buf, 0, sizeof(buf));
                std::ostringstream recvData{""};
                int count = 0;
                do {
                    ++count;
                    memset(recvBuf, 0, sizeof(recvBuf));
                    logger.stream().out() << "socket : " << clientSocket << " recv BEFORE";
                    SocketStatus s = refSocketManager_.setStatusIfEnable(clientSocket, SocketStatus::RECV);
                    if (s == SocketStatus::TO_CLOSE || s == SocketStatus::SOCKET_IS_NONE) {
                        // この場合は処理を進められないのでループを抜ける
                        break;
                    }
                    result = recv(clientSocket, recvBuf, sizeof(recvBuf), 0);
                    s = refSocketManager_.setStatusIfEnable(clientSocket, SocketStatus::RECEIVING);
                    logger.stream().out() << "socket : " << clientSocket << " recv AFTER";
                    if (s == SocketStatus::TO_CLOSE || s == SocketStatus::SOCKET_IS_NONE) {
                        // この場合は処理を進められないのでrecvが正常終了であった場合はここでループを抜ける
                        if (result > 0) {
                            break;
                        }
                        // エラーがあった場合は以下でログ出力
                    }

                    if (result > 0) {

                        recvData << recvBuf;
                        //  仮の処理:終端が空行かであれば全て読み取ったとみなす
                        size_t lastIndex = recvData.str().length() - 1;
                        if (lastIndex > 4 &&
                            int{recvData.str().at(lastIndex - 3)} == 13 &&
                            int{recvData.str().at(lastIndex - 2)} == 10 &&
                            int{recvData.str().at(lastIndex - 1)} == 13 &&
                            int{recvData.str().at(lastIndex)} == 10) {

                            // 受信データをhttpリクエストとして解析
                            std::istringstream iss{recvData.str()};
                            HttpRequest request;
                            // 1行目
                            std::string line;
                            if (std::getline(iss, line)) {
                                std::istringstream text{line};
                                std::string word;
                                text >> word;
                                request.setHttpRequestMethodFromText(word);

                                text >> word;
                                request.path = word;
                                text >> word;
                                request.protocol = word;
                            }
                            else {
                                throw std::runtime_error{"error occurred while http request parse."};
                            }
                            // Httpリクエストヘッダー
                            std::ostringstream requestHeaderkey{""};
                            std::ostringstream requestHeaderValue{""};
                            char c = ' ';
                            while (iss >> c) {
                                if (c != ':') {
                                    requestHeaderkey << c;
                                }
                                else {
                                    // 値の先頭の空白を読みとばす
                                    while (iss >> c && c == ' ') {
                                        // noop
                                    }
                                    iss.putback(c);
                                    std::string str;
                                    if (std::getline(iss, str)) {
                                        requestHeaderValue << str;
                                    }

                                    request.headers.insert(std::make_pair(requestHeaderkey.str(), requestHeaderValue.str()));
                                    requestHeaderkey.str("");
                                    requestHeaderkey.clear(std::stringstream::goodbit);
                                    requestHeaderValue.str("");
                                    requestHeaderValue.clear(std::stringstream::goodbit);
                                }
                            }
                            // 受信完了したのでクリアする
                            recvData.str("");
                            recvData.clear(std::stringstream::goodbit);

                            // ハンドラーによるリクエスト処理
                            refSocketManager_.setStatus(clientSocket, SocketStatus::PROCESSING);
                            logger.stream().out() << "socket : " << clientSocket << " findHandlerNode BEFORE";
                            HandlerTreeNode &node = refHandlerTree_.findHandlerNode(request.path);
                            logger.stream().out() << "socket : " << clientSocket << " findHandlerNode AFTER";

                            HandlerResult hr;
                            if (node.isHandlerNull()) {
                                // ハンドラーのパスと同名のリソース要求である場合がありえるのでデフォルトハンドラーを呼ぶ
                                hr = refHandlerTree_.defaultHandlerNode().handler().handle(request);
                            }
                            else {
                                hr = node.handler().handle(request);
                            }

                            // logger.stream().out() << "----------------------" << count ;
                            //  仮の応答メッセージ(ヘッダ部分)
                            std::ostringstream oss{""};
                            oss << "HTTP/1.1 "
                                << toStringFromStatusCode(hr.status)
                                << "\r\n"
                                << "Content-Length:" << hr.responseBody.size() << "\r\n"
                                << "Content-Type: " << hr.mediaType << "\r\n"
                                << "\r\n";

                            std::vector<std::byte> v;
                            for (const char ch : oss.str()) {
                                v.push_back(static_cast<std::byte>(ch));
                            }
                            for (const std::byte b : hr.responseBody) {
                                v.push_back(b);
                            }

                            // 送信処理
                            size_t len = sizeof(buf);
                            size_t index = 0;
                            refSocketManager_.setStatus(clientSocket, SocketStatus::SENDING);
                            for (auto it = v.cbegin(); it != v.end(); ++it) {
                                if (index < len) {
                                    buf[index] = static_cast<char>(*it);
                                    ++index;
                                }
                                else {
                                    iSendResult = send(clientSocket, buf, index, 0);
                                    if (iSendResult == SOCKET_ERROR) {
                                        refSocketManager_.setStatus(clientSocket, SocketStatus::TO_CLOSE);
                                        logger.stream().out() << "socket : " << clientSocket << " send failed with error: " << WSAGetLastError();
                                        break;
                                    }
                                    index = 0;
                                    std::advance(it, -1);
                                }
                            }
                            if (index > 0) {
                                iSendResult = send(clientSocket, buf, index, 0);
                                if (iSendResult == SOCKET_ERROR) {
                                    refSocketManager_.setStatus(clientSocket, SocketStatus::TO_CLOSE);
                                    logger.stream().out() << "socket : " << clientSocket << " send failed with error: " << WSAGetLastError();
                                }
                            }

                            if (iSendResult != SOCKET_ERROR) {
                                // 最終送受信時刻を更新してから状態を設定する
                                refSocketManager_.setLastTime(clientSocket, std::chrono::system_clock::now());
                                refSocketManager_.setStatus(clientSocket, SocketStatus::COMPLETED);
                            }
                        }
                    }
                    else if (result == 0) {
                        recvData.str("");
                        recvData.clear(std::stringstream::goodbit);
                        refSocketManager_.setStatus(clientSocket, SocketStatus::TO_CLOSE);
                        logger.stream().out() << "socket : " << clientSocket << " Connection closing...";
                    }
                    else {
                        recvData.str("");
                        recvData.clear(std::stringstream::goodbit);
                        refSocketManager_.setStatus(clientSocket, SocketStatus::TO_CLOSE);
                        logger.stream().out() << "socket : " << clientSocket << " recv failed with error: " << WSAGetLastError();
                    }
                } while (result > 0);
            }
            catch (std::exception &e) {
                refSocketManager_.setStatus(clientSocket, SocketStatus::TO_CLOSE);
                logger.stream().out() << "socket : " << clientSocket << " error : " << e.what();
                throw;
            }
        }

        // コピー禁止
        Receiver(const Receiver &) = delete;
        Receiver &operator=(const Receiver &) = delete;
        // ムーブ禁止
        Receiver(Receiver &&rhs) = delete;
        Receiver &operator=(Receiver) = delete;

    private:
        SocketManager &refSocketManager_;
        ThreadsMap &refThreadsMap_;
        HandlerTree &refHandlerTree_;
    };

    class WebServer {
    public:
        WebServer(const std::string port, const int maxSockets)
            : port_{port},
              maxSockets_{maxSockets},
              listenSocket_{INVALID_SOCKET},
              isInitialized_{false}
        {
        }

        WebServer()
            : port_{"27015"},
              maxSockets_{10},
              listenSocket_{INVALID_SOCKET},
              isInitialized_{false}
        {
        }

        ~WebServer()
        {
            try {
                logger.stream().out() << "listen socket : " << listenSocket_ << " closesocket BEFORE";
                closesocket(listenSocket_);
                logger.stream().out() << "listen socket : " << listenSocket_ << " closesocket AFTER";
                WSACleanup();
            }
            catch (std::exception &e) {
                CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
            }
            catch (...) {
                CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
            }
        }

        // コピー禁止
        WebServer(const WebServer &) = delete;
        WebServer &operator=(const WebServer &) = delete;
        // ムーブ禁止
        WebServer(WebServer &&) = delete;
        WebServer &operator=(WebServer &&) = delete;

        int initialize()
        {
            try {
                std::lock_guard<std::mutex> lock{mt_};
                if (isInitialized_) {
                    return 0;
                }

                // RequestHandlerのセット
                handlerTree_.addRootNode({"/", std::make_unique<RootHandler>(std::initializer_list<HttpRequestMethod>({HttpRequestMethod::GET}))});
                // deadlock_exampleのRequestHandlerのセット
                handlerTree_.addRootNode({getValue<std::string>(webConfiguration, "sites", "ROOT_DLEX"), nullptr});
                handlerTree_.findHandlerNode(getValue<std::string>(webConfiguration, "sites", "ROOT_DLEX")).addChildNode({"top", std::make_unique<DLEXRootHandler>(std::initializer_list<HttpRequestMethod>({HttpRequestMethod::GET}))});
                // helloworldのRequestHandlerのセット
                handlerTree_.addRootNode({getValue<std::string>(webConfiguration, "sites", "ROOT_HELLOWORLD"), nullptr});
                handlerTree_.findHandlerNode(getValue<std::string>(webConfiguration, "sites", "ROOT_HELLOWORLD")).addChildNode({"top", std::make_unique<HelloWorldRootHandler>(std::initializer_list<HttpRequestMethod>({HttpRequestMethod::GET}))});

                WSADATA wsaData;
                int iResult;

                struct addrinfo *result = NULL;
                struct addrinfo hints;

                // winsockの初期化
                iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
                if (iResult != 0) {
                    logger.stream().out() << "WSAStartup failed with error: " << iResult;
                    return 1;
                }

                ZeroMemory(&hints, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;
                hints.ai_flags = AI_PASSIVE;

                // サーバーのアドレスとポートを解決する
                iResult = getaddrinfo(NULL, port_.c_str(), &hints, &result);
                if (iResult != 0) {
                    logger.stream().out() << "getaddrinfo failed with error: " << iResult;
                    return 1;
                }

                // ソケットの作成
                listenSocket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
                if (listenSocket_ == INVALID_SOCKET) {
                    logger.stream().out() << "socket failed with error: " << WSAGetLastError();
                    freeaddrinfo(result);
                    return 1;
                }

                // TCPリスニングソケットの設定
                iResult = bind(listenSocket_, result->ai_addr, (int)result->ai_addrlen);
                if (iResult == SOCKET_ERROR) {
                    logger.stream().out() << "bind failed with error: " << WSAGetLastError();
                    freeaddrinfo(result);
                    return 1;
                }

                freeaddrinfo(result);

                iResult = listen(listenSocket_, SOMAXCONN);
                if (iResult == SOCKET_ERROR) {
                    logger.stream().out() << "listen failed with error: " << WSAGetLastError();
                    return 1;
                }
                isInitialized_ = true;
                return 0;
            }
            catch (std::exception &e) {
                logger.stream().out() << e.what();
            }

            return 1;
        }

        int start()
        {
            try {
                SOCKET clientSocket = INVALID_SOCKET;
                ThreadsMap threadsMap{getValue<int>(webConfiguration, "threadsMap", "CLEAN_UP_POINT")};

                // SocketManagerを生成して開始する
                SocketManager socketManager{getValue<int>(webConfiguration, "socketManager", "MAX"),
                                            getValue<int>(webConfiguration, "socketManager", "TIMEOUT")};
                socketManager.startMonitor();
                while (1) {

                    logger.stream().out() << "accept BEFORE";
                    clientSocket = accept(listenSocket_, NULL, NULL);
                    logger.stream().out() << "accept AFTER clientSocket is : " << clientSocket;

                    if (clientSocket == INVALID_SOCKET) {
                        logger.stream().out() << "accept failed with error: " << WSAGetLastError();
                        return 1;
                    }

                    if (!socketManager.addSocket(clientSocket)) {
                        // socketManagerの管理ソケット数が上限であった場合
                        logger.stream().out() << "number of sockets is upper limits.";
                        socketManager.addOverCapacitySocket(clientSocket);
                        continue;
                    }

                    // 別スレッドで受信処理をする
                    std::thread t{[this, clientSocket, &socketManager, &threadsMap] {
                        try {
                            Receiver receiver{socketManager, threadsMap, this->handlerTree_};
                            receiver.receive(clientSocket);
                        }
                        catch (std::exception &e) {
                            CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
                        }
                        catch (...) {
                            CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
                        }
                    }};

                    threadsMap.addThread(std::move(t));
                    threadsMap.cleanUp();
                    logger.stream().out() << "number of threads is : " << threadsMap.size();
                }

                return 0;
            }
            catch (std::exception &e) {
                logger.stream().out() << e.what();
            }

            return 1;
        }

    private:
        const std::string port_;
        const int maxSockets_;
        SOCKET listenSocket_;
        HandlerTree handlerTree_;
        bool isInitialized_;
        std::mutex mt_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_WEBSERVER_INCLUDED