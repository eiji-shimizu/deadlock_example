#define _UNICODE
#define UNICODE

#pragma comment(lib, "ws2_32.lib")

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

int main()
{
    const std::string DEFAULT_PORT = "27015";
    const int MAX_THREADS = 10;

    try {
        WSADATA wsaData;
        int iResult;

        SOCKET listenSocket = INVALID_SOCKET;
        SOCKET clientSocket = INVALID_SOCKET;
        struct addrinfo *result = NULL;
        struct addrinfo hints;

        // int iSendResult;
        // char buf[DEFAULT_BUFLEN];
        // char recvBuf[DEFAULT_BUFLEN];

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
        iResult = getaddrinfo(NULL, DEFAULT_PORT.c_str(), &hints, &result);
        if (iResult != 0) {
            std::cout << "getaddrinfo failed with error: " << iResult << std::endl;
            WSACleanup();
            return 1;
        }

        // ソケットの作成
        listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (listenSocket == INVALID_SOCKET) {
            std::cout << "socket failed with error: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            WSACleanup();
            return 1;
        }

        // TCPリスニングソケットの設定
        iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            std::cout << "bind failed with error: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        }

        freeaddrinfo(result);

        iResult = listen(listenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            std::cout << "listen failed with error: " << WSAGetLastError() << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        }

        // 仮の応答用メッセージ
        // memset(buf, 0, sizeof(buf));
        // std::ostringstream oss{""};
        // oss << "HTTP/1.0 200 OK\r\n"
        //     << "Content-Length: 20\r\n"
        //     << "Content-Type: text/html\r\n"
        //     << "\r\n"
        //     << "HELLO\r\n";

        ThreadsMap threads;
        while (1) {

            std::cout << "accept BEFORE" << std::endl;
            clientSocket = accept(listenSocket, NULL, NULL);
            std::cout << "accept AFTER" << std::endl;
            std::cout << "clientSocket is : " << clientSocket << std::endl;

            if (clientSocket == INVALID_SOCKET) {
                std::cout << "accept failed with error: " << WSAGetLastError() << std::endl;
                closesocket(listenSocket);
                WSACleanup();
                return 1;
            }

            if (threads.isFull()) {
                std::cout << "number of threads is upper limits." << std::endl;
                closesocket(listenSocket);
                return 1;
            }

            std::thread t{[clientSocket, &threads] {
                const int DEFAULT_BUFLEN = 512;

                try {
                    char buf[DEFAULT_BUFLEN];
                    char recvBuf[DEFAULT_BUFLEN];
                    int result;
                    int iSendResult;

                    // 仮の応答用メッセージ
                    memset(buf, 0, sizeof(buf));
                    std::ostringstream oss{""};
                    oss << "HTTP/1.0 200 OK\r\n"
                        << "Content-Length: 20\r\n"
                        << "Content-Type: text/html\r\n"
                        << "\r\n"
                        << "HELLO\r\n";

                    do {
                        memset(recvBuf, 0, sizeof(recvBuf));
                        result = recv(clientSocket, recvBuf, sizeof(recvBuf), 0);
                        if (result > 0) {
                            // 本来ならばクライアントからの要求内容をパースすべきです
                            // std::cout << recvBuf << std::endl;

                            // 相手が何を言おうとダミーHTTPメッセージ送信
                            size_t len = sizeof(buf);
                            if (sizeof(buf) > oss.str().size()) {
                                len = oss.str().size();
                            }
                            std::memcpy(buf, oss.str().c_str(), len);
                            iSendResult = send(clientSocket, buf, result, 0);
                            if (iSendResult == SOCKET_ERROR) {
                                std::cout << "send failed with error: " << WSAGetLastError() << std::endl;
                                closesocket(clientSocket);
                                // WSACleanup();
                                // return 1;
                            }
                        }
                        else if (result == 0) {
                            std::cout << "Connection closing..." << std::endl;
                        }
                        else {
                            std::cout << "recv failed with error: " << WSAGetLastError() << std::endl;
                            closesocket(clientSocket);
                            // WSACleanup();
                            // return 1;
                        }
                    } while (result > 0);

                    closesocket(clientSocket);
                }
                catch (std::exception &e) {
                    // ignore
                    closesocket(clientSocket);
                }
                catch (...) {
                    // ignore
                    closesocket(clientSocket);
                }

                closesocket(clientSocket);
                threads.setFinishedFlag(std::this_thread::get_id());
            }};

            threads.addThread(std::move(t));
            threads.cleanUp();
            std::cout << "number of threads is : " << threads.size() << std::endl;
        }

        closesocket(listenSocket);
        WSACleanup();

        return 0;
    }
    catch (std::exception &e) {
        std::cout << e.what() << std::endl;
    }
    catch (...) {
        std::cout << "unexpected error." << std::endl;
    }

    return 1;

    // std::vector<std::string> msg{"Hello", "C++", "World", "from", "VS Code", "and the C++ extension!"};

    // for (const auto &word : msg) {
    //     std::cout << word << " ";
    // }
    // std::cout << std::endl;
}