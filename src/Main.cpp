#define _UNICODE
#define UNICODE

#pragma comment(lib, "ws2_32.lib")

#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

int main()
{
    const int DEFAULT_BUFLEN = 512;
    const std::string DEFAULT_PORT = "27015";

    try {
        WSADATA wsaData;
        int iResult;

        SOCKET listenSocket = INVALID_SOCKET;
        SOCKET clientSocket = INVALID_SOCKET;
        struct addrinfo *result = NULL;
        struct addrinfo hints;

        int iSendResult;
        char buf[DEFAULT_BUFLEN];
        char recvBuf[DEFAULT_BUFLEN];

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
        memset(buf, 0, sizeof(buf));
        std::ostringstream oss{""};
        oss << "HTTP/1.0 200 OK\r\n"
            << "Content-Length: 20\r\n"
            << "Content-Type: text/html\r\n"
            << "\r\n"
            << "HELLO\r\n";

        std::cout << "here" << std::endl;
        while (1) {

            std::cout << "there" << std::endl;

            clientSocket = accept(listenSocket, NULL, NULL);
            if (clientSocket == INVALID_SOCKET) {
                std::cout << "accept failed with error: " << WSAGetLastError() << std::endl;
                closesocket(listenSocket);
                WSACleanup();
                return 1;
            }

            do {
                memset(recvBuf, 0, sizeof(recvBuf));
                iResult = recv(clientSocket, recvBuf, sizeof(recvBuf), 0);
                if (iResult > 0) {
                    // 本来ならばクライアントからの要求内容をパースすべきです
                    std::cout << recvBuf << std::endl;

                    // 相手が何を言おうとダミーHTTPメッセージ送信
                    size_t len = sizeof(buf);
                    if (sizeof(buf) > oss.str().size()) {
                        len = oss.str().size();
                    }
                    std::memcpy(buf, oss.str().c_str(), len);
                    iSendResult = send(clientSocket, buf, iResult, 0);
                    if (iSendResult == SOCKET_ERROR) {
                        std::cout << "send failed with error: " << WSAGetLastError() << std::endl;
                        closesocket(clientSocket);
                        WSACleanup();
                        return 1;
                    }
                }
                else if (iResult == 0) {
                    std::cout << "Connection closing..." << std::endl;
                }
                else {
                    std::cout << "recv failed with error: " << WSAGetLastError() << std::endl;
                    closesocket(clientSocket);
                    WSACleanup();
                    return 1;
                }
            } while (iResult > 0);

            closesocket(clientSocket);
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