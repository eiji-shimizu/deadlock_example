#ifndef DEADLOCK_EXAMPLE_WEBSERVER_INCLUDED
#define DEADLOCK_EXAMPLE_WEBSERVER_INCLUDED

#pragma comment(lib, "ws2_32.lib")

#include "Common.h"
#include "DLEXRequestHandler.h"
#include "Http.h"
#include "RequestHandler.h"

#include <cstring>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace PapierMache {

    class ThreadsMap {
    public:
        ThreadsMap(int max)
            : max_{max}
        {
        }
        ThreadsMap()
            : max_{10}
        {
        }
        ~ThreadsMap()
        {
            // noop
        }

        // コピー禁止
        ThreadsMap(const ThreadsMap &) = delete;
        ThreadsMap &operator=(const ThreadsMap &) = delete;
        // ムーブ禁止
        ThreadsMap(ThreadsMap &&) = delete;
        ThreadsMap &operator=(ThreadsMap &&) = delete;

        size_t size() const
        {
            // この関数はデバッグ用なのでロック無し
            return threads_.size();
        }

        bool isFull()
        {
            std::lock_guard<std::mutex> lock{mt_};
            return threads_.size() == max_;
        }

        bool addThread(std::thread &&t)
        {
            std::lock_guard<std::mutex> lock{mt_};
            if (threads_.size() < max_) {
                threads_.insert(std::make_pair(t.get_id(), std::move(t)));
                finishedFlag_.try_emplace(t.get_id(), false);
                return true;
            }
            return false;
        }

        void setFinishedFlag(const std::thread::id id)
        {
            std::lock_guard<std::mutex> lock{mt_};
            finishedFlag_.insert_or_assign(id, true);
        }

        void cleanUp()
        {
            std::lock_guard<std::mutex> lock{mt_};
            // スレッド数が最大の半分より多ければクリーンアップする
            if (threads_.size() > (max_ / 2)) {
                std::vector<std::thread::id> vec;
                for (const auto &p : finishedFlag_) {
                    if (p.second) {
                        vec.push_back(p.first);
                        // finishedFlgがtrueのスレッドのみjoin
                        std::cout << "thread id : " << p.first << " join for cleanup." << std::endl;
                        threads_.at(p.first).join();
                    }
                }
                for (const auto id : vec) {
                    threads_.erase(id);
                    finishedFlag_.erase(id);
                }
            }
        }

    private:
        const int max_;
        std::map<std::thread::id, std::thread> threads_;
        std::map<std::thread::id, bool> finishedFlag_;
        std::mutex mt_;
    };

    class Receiver {
    public:
        Receiver(const SOCKET clientSocket, ThreadsMap &refThreadMap)
            : clientSocket_{clientSocket},
              refThreadsMap_{refThreadMap}
        {
        }

        ~Receiver()
        {
            try {
                // コネクションをシャットダウン
                int shutDownResult = 0;
                std::cout << "socket : " << clientSocket_ << " shutdown BEFORE" << std::endl;
                shutDownResult = shutdown(clientSocket_, SD_BOTH);
                std::cout << "socket : " << clientSocket_ << " shutdown AFTER" << std::endl;
                if (shutDownResult == SOCKET_ERROR) {
                    std::cout << "socket : " << clientSocket_ << " shutdown failed with error: " << WSAGetLastError() << std::endl;
                }
                // コネクションをクローズ
                std::cout << "socket : " << clientSocket_ << " closesocket BEFORE" << std::endl;
                closesocket(clientSocket_);
                std::cout << "socket : " << clientSocket_ << " closesocket AFTER" << std::endl;

                refThreadsMap_.setFinishedFlag(std::this_thread::get_id());
            }
            catch (std::exception &e) {
            }
            catch (...) {
            }
        }

        void receive()
        {
            const int DEFAULT_BUFLEN = 512;
            // const int DEFAULT_BUFLEN = 512;

            try {
                char buf[DEFAULT_BUFLEN];
                char recvBuf[DEFAULT_BUFLEN];
                int result;
                int iSendResult;

                // 仮の応答用メッセージ
                memset(buf, 0, sizeof(buf));
                std::ostringstream oss{""};
                oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Length: 7\r\n"
                    << "Content-Type: text/html\r\n"
                    << "\r\n"
                    << "HELLO\r\n";

                std::ostringstream recvData{""};
                int count = 0;
                do {
                    ++count;
                    memset(recvBuf, 0, sizeof(recvBuf));
                    std::cout << "-----" << count << ": " << recvBuf << std::endl;
                    std::cout << "socket : " << clientSocket_ << " recv BEFORE" << std::endl;
                    result = recv(clientSocket_, recvBuf, sizeof(recvBuf), 0);
                    std::cout << "socket : " << clientSocket_ << " recv AFTER" << std::endl;
                    std::cout << "-----" << count << ": " << recvBuf << std::endl;
                    if (result > 0) {
                        // 本来ならばクライアントからの要求内容をパースすべきです
                        // std::cout << "-----" << count << ": " << recvBuf << std::endl;
                        // std::cout << "socket : " << clientSocket_ << " recv success." << std::endl;

                        recvData << recvBuf;
                        // std::cout << "-----" << count << ": recvData " << recvData.str() << std::endl;
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
                                    std::string s;
                                    if (std::getline(iss, s)) {
                                        requestHeaderValue << s;
                                    }

                                    request.headers.insert(std::make_pair(requestHeaderkey.str(), requestHeaderValue.str()));
                                    requestHeaderkey.str("");
                                    requestHeaderkey.clear(std::stringstream::goodbit);
                                    requestHeaderValue.str("");
                                    requestHeaderValue.clear(std::stringstream::goodbit);
                                }
                            }

                            // ハンドラーによるリクエスト処理
                            std::cout << "-----" << count << ": request " << request.toString() << std::endl;

                            std::cout << "----------------------" << count << std::endl;
                            size_t len = sizeof(buf);
                            if (sizeof(buf) > oss.str().size()) {
                                len = oss.str().size();
                            }
                            std::memcpy(buf, oss.str().c_str(), len);
                            std::cout << "-----" << count << ": " << buf << std::endl;
                            iSendResult = send(clientSocket_, buf, len, 0);
                            if (iSendResult == SOCKET_ERROR) {
                                std::cout << "socket : " << clientSocket_ << " send failed with error: " << WSAGetLastError() << std::endl;
                            }
                            break;
                        }
                        // std::cout << recvData.str() << std::endl;
                        //   if (recvData.str().at(recvData.str().length() -1) == '\r\n') {
                        //       // 相手が何を言おうとダミーHTTPメッセージ送信
                        //       oss << recvBuf;

                        // }
                    }
                    else if (result == 0) {
                        // 相手が何を言おうとダミーHTTPメッセージ送信
                        // size_t len = sizeof(buf);
                        // if (sizeof(buf) > oss.str().size()) {
                        //     len = oss.str().size();
                        // }
                        // std::memcpy(buf, oss.str().c_str(), len);
                        // iSendResult = send(clientSocket_, buf, len, 0);
                        // // iSendResult = send(clientSocket_, buf, result, 0);
                        // if (iSendResult == SOCKET_ERROR) {
                        //     std::cout << "socket : " << clientSocket_ << " send failed with error: " << WSAGetLastError() << std::endl;
                        // }
                        // std::cout << recvData.str() << std::endl;
                        recvData.str("");
                        recvData.clear(std::stringstream::goodbit);
                        std::cout << "socket : " << clientSocket_ << " Connection closing..." << std::endl;
                    }
                    else {
                        // std::cout << recvData.str() << std::endl;
                        recvData.str("");
                        recvData.clear(std::stringstream::goodbit);
                        std::cout << "socket : " << clientSocket_ << " recv failed with error: " << WSAGetLastError() << std::endl;
                    }
                } while (result > 0);
            }
            catch (std::exception &e) {
                std::cout << "socket : " << clientSocket_ << " error : " << e.what() << std::endl;
            }
            catch (...) {
                std::cout << "socket : " << clientSocket_ << " unexpected error." << std::endl;
            }
        }

        // コピー禁止
        Receiver(const Receiver &) = delete;
        Receiver &operator=(const Receiver &) = delete;
        // ムーブ禁止
        Receiver(Receiver &&rhs) = delete;
        Receiver &operator=(Receiver) = delete;

    private:
        HttpRequest parseRecvData(const std::string &s) const
        {
        }

        const SOCKET clientSocket_;
        ThreadsMap &refThreadsMap_;
    };

    class WebServer {
    public:
        WebServer(const std::string port, const int maxThreads)
            : port_{port},
              maxThreads_{maxThreads},
              listenSocket_{INVALID_SOCKET},
              isInitialized_{false}
        {
        }

        WebServer()
            : port_{"27015"},
              maxThreads_{10},
              listenSocket_{INVALID_SOCKET},
              isInitialized_{false}
        {
        }

        ~WebServer()
        {
            try {
                std::cout << "listen socket : " << listenSocket_ << " closesocket BEFORE" << std::endl;
                closesocket(listenSocket_);
                std::cout << "listen socket : " << listenSocket_ << " closesocket AFTER" << std::endl;
                WSACleanup();
            }
            catch (std::exception &e) {
            }
            catch (...) {
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
                handlerTree_.addRootNode({"dlex", std::make_unique<DLEXRootHandler>(std::initializer_list<HttpRequestMethod>({HttpRequestMethod::GET}))});

                WSADATA wsaData;
                int iResult;

                struct addrinfo *result = NULL;
                struct addrinfo hints;

                // winsockの初期化
                iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
                if (iResult != 0) {
                    std::cout << "WSAStartup failed with error: " << iResult << std::endl;
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
                    std::cout << "getaddrinfo failed with error: " << iResult << std::endl;
                    return 1;
                }

                // ソケットの作成
                listenSocket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
                if (listenSocket_ == INVALID_SOCKET) {
                    std::cout << "socket failed with error: " << WSAGetLastError() << std::endl;
                    freeaddrinfo(result);
                    return 1;
                }

                // TCPリスニングソケットの設定
                iResult = bind(listenSocket_, result->ai_addr, (int)result->ai_addrlen);
                if (iResult == SOCKET_ERROR) {
                    std::cout << "bind failed with error: " << WSAGetLastError() << std::endl;
                    freeaddrinfo(result);
                    return 1;
                }

                freeaddrinfo(result);

                iResult = listen(listenSocket_, SOMAXCONN);
                if (iResult == SOCKET_ERROR) {
                    std::cout << "listen failed with error: " << WSAGetLastError() << std::endl;
                    return 1;
                }
                isInitialized_ = true;
                return 0;
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
            }
            catch (...) {
                std::cout << "unexpected error." << std::endl;
            }

            return 1;
        }

        int start()
        {
            HandlerTreeNode n0{"a", nullptr};
            HandlerTreeNode n1{std::move(n0)};
            HandlerTreeNode n2{"b", nullptr};
            // n2 = n1;

            try {
                SOCKET clientSocket = INVALID_SOCKET;
                ThreadsMap threads;
                while (1) {

                    std::cout << "accept BEFORE" << std::endl;
                    clientSocket = accept(listenSocket_, NULL, NULL);
                    std::cout << "accept AFTER clientSocket is : " << clientSocket << std::endl;

                    if (clientSocket == INVALID_SOCKET) {
                        std::cout << "accept failed with error: " << WSAGetLastError() << std::endl;
                        return 1;
                    }

                    if (threads.isFull()) {
                        std::cout << "number of threads is upper limits." << std::endl;
                        return 1;
                    }

                    std::thread t{[clientSocket, &threads] {
                        try {
                            Receiver receiver{clientSocket, threads};
                            receiver.receive();
                        }
                        catch (std::exception &e) {
                        }
                        catch (...) {
                        }
                    }};

                    threads.addThread(std::move(t));
                    threads.cleanUp();
                    std::cout << "number of threads is : " << threads.size() << std::endl;
                }

                return 0;
            }
            catch (std::exception &e) {
                std::cout << e.what() << std::endl;
            }
            catch (...) {
                std::cout << "unexpected error." << std::endl;
            }

            return 1;
        }

    private:
        const std::string port_;
        const int maxThreads_;
        SOCKET listenSocket_;
        bool isInitialized_;
        HandlerTree handlerTree_;
        std::mutex mt_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_WEBSERVER_INCLUDED