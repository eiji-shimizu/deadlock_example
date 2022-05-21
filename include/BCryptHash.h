#ifndef DEADLOCK_EXAMPLE_BCRYPT_HASH_INCLUDED
#define DEADLOCK_EXAMPLE_BCRYPT_HASH_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"

#pragma comment(lib, "Bcrypt.lib")

// bcrypt.hより先にインクルードする
#include <windows.h>

#include <bcrypt.h>

#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace PapierMache {

    inline std::string getHexValue(const NTSTATUS &s)
    {
        std::ostringstream oss{""};
        oss << std::hex << s;
        return oss.str();
    }

    inline bool isSuccess(const NTSTATUS &s)
    {
        return s >= 0;
    }

    inline void toBCryptHash(const std::string plainText, std::string &result)
    {
        BCRYPT_ALG_HANDLE hAesAlg = NULL;
        try {
            NTSTATUS status = ((NTSTATUS)0xC0000001L);
            DWORD cbHashOrMAC = 0;
            DWORD cbData = 0;

            // アルゴリズムのハンドルを取得する
            status = BCryptOpenAlgorithmProvider(&hAesAlg,
                                                 BCRYPT_SHA256_ALGORITHM,
                                                 NULL,
                                                 0);
            if (!isSuccess(status)) {
                throw std::runtime_error{"**** Error " + getHexValue(status) + " returned by BCryptOpenAlgorithmProvider" + FILE_INFO};
            }

            // ハッシュ値を保存するバッファのサイズを計算する
            status = BCryptGetProperty(hAesAlg,
                                       BCRYPT_HASH_LENGTH,
                                       (PBYTE)&cbHashOrMAC,
                                       sizeof(DWORD),
                                       &cbData,
                                       0);
            if (!isSuccess(status)) {
                throw std::runtime_error{"**** Error " + getHexValue(status) + " returned by BCryptGetProperty" + FILE_INFO};
            }
            std::vector<BYTE> hash;
            hash.resize(cbHashOrMAC);

            // 平文からハッシュ値を生成する
            std::vector<BYTE> text;
            for (const char c : plainText) {
                text.push_back(c);
            }
            if (text.size() > ULONG_MAX) {
                throw std::runtime_error("arithmetic overflow" + FILE_INFO);
            }
#pragma warning(push)
#pragma warning(disable : 4267)
            status = BCryptHash(hAesAlg,
                                NULL,
                                0,
                                text.data(),
                                text.size(),
                                hash.data(),
                                hash.size());
#pragma warning(pop)
            if (!isSuccess(status)) {
                throw std::runtime_error{"**** Error " + getHexValue(status) + " returned by BCryptHash" + FILE_INFO};
            }

            result.resize(hash.size());
            auto it = result.begin();
            for (const BYTE b : hash) {
                *it = b;
                ++it;
            }
            if (hAesAlg) {
                BCryptCloseAlgorithmProvider(hAesAlg, 0);
            }
        }
        catch (std::exception &e) {
            UNREFERENCED_PARAMETER(e);
            BCryptCloseAlgorithmProvider(hAesAlg, 0);
            throw;
        }
        catch (...) {
            BCryptCloseAlgorithmProvider(hAesAlg, 0);
            throw;
        }
    }

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_BCRYPT_HASH_INCLUDED