#ifndef DEADLOCK_EXAMPLE_UUID_INCLUDED
#define DEADLOCK_EXAMPLE_UUID_INCLUDED

#include "General.h"

#pragma comment(lib, "Rpcrt4.lib")

#include <rpc.h>
// #include <rpcdce.h>

#include <exception>
#include <iostream>
#include <sstream>
#include <string>

namespace PapierMache {

    class UUID {
    public:
        ~UUID() {}

        static UUID create()
        {
            ::UUID uuid;
            switch (UuidCreate(&uuid)) {
            case RPC_S_OK: {
                try {
                    std::ostringstream oss{""};
                    oss << std::hex << uuid.Data1;
                    oss << std::dec << '-';
                    oss << std::hex << uuid.Data2;
                    oss << std::dec << '-';
                    oss << std::hex << uuid.Data3;
                    oss << std::dec << '-';
                    oss << std::hex;
                    unsigned short s[8];
                    for (int i = 0; i < 2; ++i) {
                        s[i] = uuid.Data4[i];
                    }
                    unsigned short fh = (s[0] << 8) | s[1];
                    oss << fh;
                    oss << std::dec << '-';
                    oss << std::hex;
                    for (int i = 2; i < 8; ++i) {
                        s[i] = uuid.Data4[i];
                    }
                    for (int i = 2; i < 8; i = i + 2) {
                        unsigned short lh = (s[i] << 8) | s[i + 1];
                        oss << lh;
                    }
                    return UUID{oss.str()};
                }
                catch (...) {
                    throw std::runtime_error{"UUID::create() : " + std::string{"unexpected error."}};
                }
            }
            case RPC_S_UUID_LOCAL_ONLY:
                throw std::runtime_error{"UUID::create() : " + std::string{"error RPC_S_UUID_LOCAL_ONLY"}};
            case RPC_S_UUID_NO_ADDRESS:
                throw std::runtime_error{"UUID::create() : " + std::string{"error RPC_S_UUID_NO_ADDRESS"}};
            }
            throw std::runtime_error{"UUID::create() : " + std::string{"should not reach here."}};
        }

        std::string str()
        {
            return std::string{str_};
        };

    private:
        UUID(const std::string &str) : str_{str} {}
        // uuidの文字列表現
        std::string str_;
    };

} // namespace PapierMache

#endif DEADLOCK_EXAMPLE_UUID_INCLUDED