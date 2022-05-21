#ifndef DEADLOCK_EXAMPLE_DATAFILE_INCLUDED
#define DEADLOCK_EXAMPLE_DATAFILE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"
#include "Utils.h"

#include <windows.h>
#include <winnt.h>

#include <condition_variable>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace PapierMache::DbStuff {

    using TRANSACTION_ID = short;

    class DatafileException : public std::runtime_error {
    public:
        DatafileException(const char *message)
            : std::runtime_error(message)
        {
        }

        DatafileException(const std::string message)
            : std::runtime_error(message)
        {
        }
    };

    class Datafile {
    public:
        Datafile(const std::string dataFileName, const std::map<std::string, std::string> &tableInfo)
            : tableName_{toLower(dataFileName)},
              temp_{},
              toTerminateList_{},
              pMt_{new std::mutex},
              pControlMt_{new std::mutex},
              pCond_{new std::condition_variable},
              pDataSharedMt_{new std::shared_mutex}
        {
            hFile_ = createHandle(tableName_);
            std::vector<std::tuple<std::string, std::string, int, int>> vec;
            std::map<std::string, std::vector<std::string>> m;
            std::map<std::string, int> order;
            for (const auto &e : tableInfo) {
                std::vector<std::string> users;
                if (e.first == "INSERT" || e.first == "UPDATE" || e.first == "DELETE" || e.first == "SELECT") {
                    std::ostringstream oss{""};
                    for (const char c : e.second) {
                        if (c != ',') {
                            oss << c;
                        }
                        else {
                            users.push_back(oss.str());
                            oss.str("");
                        }
                    }
                    if (oss.str() != "") {
                        users.push_back(oss.str());
                        oss.str("");
                    }
                    m.insert(std::make_pair(toLower(e.first), users));
                }
                else if (e.first == "COLUMN_ORDER") {
                    int no = 0;
                    std::ostringstream oss{""};
                    for (const char c : e.second) {
                        if (c != ',') {
                            oss << c;
                        }
                        else {
                            order.insert(std::make_pair(toLower(oss.str()), no++));
                            oss.str("");
                        }
                    }
                    if (oss.str() != "") {
                        order.insert(std::make_pair(toLower(oss.str()), no++));
                        oss.str("");
                    }
                }
                else {
                    std::ostringstream oss{""};
                    std::string colType;
                    for (const char c : e.second) {
                        if (!std::isalnum(c) && c != '_' && c != ':') {
                            throw DatafileException{"parse error. cannot use '" + std::string{c} + "'" + FILE_INFO};
                        }
                        if (c != ':') {
                            oss << c;
                        }
                        else {
                            colType = oss.str();
                            oss.str("");
                        }
                    }
                    if (std::stoi(oss.str()) <= 0) {
                        throw DatafileException{"column size cannot be zero." + FILE_INFO};
                    }
                    vec.push_back(std::make_tuple(toLower(e.first), toLower(colType), std::stoi(oss.str()), 0));
                    oss.str("");
                }
            }
            std::sort(vec.begin(), vec.end(), [order](const auto &e1, const auto &e2) {
                return order.at(std::get<0>(e1)) < order.at(std::get<0>(e2));
            });
            int offset = 0;
            for (auto &e : vec) {
                std::get<3>(e) = offset;
                if (offset > INT_MAX - std::get<2>(e)) {
                    throw DatafileException{"arithmetic overflow" + FILE_INFO};
                }
                offset += std::get<2>(e);
            }

            tableInfo_ = {vec, m};
        }

        ~Datafile(){
            CATCH_ALL_EXCEPTIONS({
                DB_LOG << tableName_ << " CloseHandle BEFORE" << FILE_INFO;
                for (auto &e : handles_) {
                    CloseHandle(e.second);
                }
                CloseHandle(hFile_);
                DB_LOG << tableName_ << " CloseHandle AFTER" << FILE_INFO;
            })}

        // コピー演算禁止
        Datafile(const Datafile &) = delete;
        Datafile &operator=(const Datafile &) = delete;
        // ムーブコピー禁止
        Datafile &operator=(Datafile &&rhs) = delete;

        // ムーブコンストラクタ
        Datafile(Datafile &&rhs)
            : tableName_{std::move(rhs.tableName_)},
              tableInfo_{std::move(rhs.tableInfo_)},
              temp_{std::move(rhs.temp_)},
              toTerminateList_{std::move(rhs.toTerminateList_)},
              pMt_{std::move(rhs.pMt_)},
              pControlMt_{std::move(rhs.pControlMt_)},
              pCond_{std::move(rhs.pCond_)},
              pDataSharedMt_{std::move(rhs.pDataSharedMt_)},
              hFile_{rhs.hFile_},
              handles_{std::move(rhs.handles_)}
        {
            for (auto &e : rhs.handles_) {
                e.second = INVALID_HANDLE_VALUE;
            }
            rhs.hFile_ = INVALID_HANDLE_VALUE;
        }

        bool insert(const TRANSACTION_ID transactionId, const std::vector<std::byte> &data)
        {
            std::map<std::string, std::vector<std::byte>> m = parseKeyValueVector(data);
            int max = -1;
            std::string name;
            for (const auto &e : tableInfo_.columnDefinitions()) {
                if (max < std::get<3>(e)) {
                    max = std::get<3>(e);
                    name = std::get<0>(e);
                }
            }
            if (m.find(name) == m.end()) {
                std::vector<std::byte> value = tableInfo_.defaultValue(name);
                if (value.size() > tableInfo_.columnSize(name)) {
                    throw std::runtime_error{"column name: " + name + " definition has error"};
                }
                while (value.size() != tableInfo_.columnSize(name)) {
                    value.push_back(std::byte{});
                }
                m.insert(std::make_pair(name, value));
            }

            std::lock_guard<std::mutex> lock{*pMt_};
            temp_.emplace_back(-1LL, transactionId, m);
            return true;
        }

        bool update(const TRANSACTION_ID transactionId,
                    const std::vector<std::byte> &data,
                    const std::vector<std::byte> &where)
        {
            std::map<std::string, std::vector<std::byte>> mData = parseKeyValueVector(data);
            std::map<std::string, std::vector<std::byte>> mWhere = parseKeyValueVector(where);
            HANDLE h;
            BOOL bErrorFlag = FALSE;
            { // Scoped Lock start
                std::lock_guard<std::mutex> lock{*pMt_};
                h = getHandle(transactionId);
            } // Scoped Lock end
            // ファイル先頭へ移動
            LARGE_INTEGER zero;
            zero.QuadPart = 0LL;
            bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, NULL, FILE_BEGIN);
            if (FALSE == bErrorFlag) {
                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
            }

            BOOL bResult = true;
            DWORD dwBytesRead = 1;
            DWORD dwBytesWritten = 0;
            while (bResult && dwBytesRead != 0) {
                bResult = false;
                // update処理が成功した場合にtrue(コミットは別)
                bool isSucceed = false;

                // 行頭位置退避
                LARGE_INTEGER save;
                bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, &save, FILE_CURRENT);
                if (FALSE == bErrorFlag) {
                    throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                }

                dwBytesRead = 0;
                dwBytesWritten = 0;
                std::vector<std::byte> buffer;
                buffer.resize(2);
                { // Scoped Lock start
                    // 書き込みロック
                    std::unique_lock<std::mutex> lock{*pControlMt_};
                    bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                    if (FALSE == bResult) {
                        throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                    }
                    else {
                        if (dwBytesRead == 0) {
                            DB_LOG << "------------------EOF" << FILE_INFO;
                            break;
                        }
                        else if (dwBytesRead != buffer.size()) {
                            throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                        }
                    }
                    // 有効なデータであれば処理
                    if (static_cast<unsigned char>(buffer[0]) == 0) {
                        // トランザクションidの位置退避
                        LARGE_INTEGER tIdOffset;
                        bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, &tIdOffset, FILE_CURRENT);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        // 更新処理開始
                        // 条件に合致する行であればトランザクションIDを書き込む
                        bool isMatch = true;
                        for (const auto &e : mWhere) {
                            // 行頭に戻る
                            bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // 制御情報のサイズと対象列のオフセットを加える
                            LARGE_INTEGER offset;
                            offset.QuadPart = add(tableInfo_.offset(toLower(e.first)), tableInfo_.controlDataSize());
                            bErrorFlag = SetFilePointerEx(getHandle(transactionId), offset, NULL, FILE_CURRENT);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // 読み込んで値を確認する(whereでわたされた全ての列で等しければ更新対象となる)
                            buffer.clear();
                            buffer.resize(tableInfo_.columnSize(toLower(e.first)));
                            assertSizeLimits<DWORD>(buffer.size());
                            bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                            if (FALSE == bResult) {
                                throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesRead == 0) {
                                    break;
                                }
                                else if (dwBytesRead != buffer.size()) {
                                    throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                                }
                            }
                            if (!tableInfo_.isEqual(toLower(e.first), e.second, buffer)) {
                                isMatch = false;
                                break;
                            }
                        }
                        if (isMatch) {
                            // 他のトランザクションの更新対象でないかを確認する
                            bErrorFlag = SetFilePointerEx(getHandle(transactionId), tIdOffset, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            buffer.clear();
                            buffer.resize(2);
                            bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                            if (FALSE == bResult) {
                                throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesRead == 0) {
                                    DB_LOG << "------------------EOF" << FILE_INFO;
                                    break;
                                }
                                else if (dwBytesRead != buffer.size()) {
                                    throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                                }
                            }
                            TRANSACTION_ID s = toShort(buffer);
                            DEBUG_LOG << "s: " << s << FILE_INFO;
                            DEBUG_LOG << "transactionId: " << transactionId << FILE_INFO;
                            if (s >= 0 && s != transactionId) {
                                while (true) {
                                    if (std::find(toTerminateList_.begin(), toTerminateList_.end(), transactionId) == toTerminateList_.end()) {
                                        DB_LOG << "wait start." << transactionId << FILE_INFO;
                                        DB_LOG << "s: " << s << FILE_INFO;
                                        DB_LOG << "transactionId: " << transactionId << FILE_INFO;
                                        pCond_->wait(lock);
                                        DB_LOG << "wait end." << transactionId << FILE_INFO;
                                    }
                                    { // Scoped Lock start
                                        std::lock_guard<std::mutex> lk{*pMt_};
                                        if (std::find(toTerminateList_.begin(), toTerminateList_.end(), transactionId) != toTerminateList_.end()) {
                                            auto result = std::remove(toTerminateList_.begin(), toTerminateList_.end(), transactionId);
                                            toTerminateList_.erase(result, toTerminateList_.end());
                                            DB_LOG << "terminated transactionId: " << transactionId << FILE_INFO;
                                            return false;
                                        }
                                    } // Scoped Lock end
                                    // 再びこの行のトランザクションの状態を確認する
                                    bErrorFlag = SetFilePointerEx(getHandle(transactionId), tIdOffset, NULL, FILE_BEGIN);
                                    if (FALSE == bErrorFlag) {
                                        throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                                    }
                                    buffer.clear();
                                    buffer.resize(2);
                                    bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                                    if (FALSE == bResult) {
                                        throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                                    }
                                    else {
                                        if (dwBytesRead == 0) {
                                            DB_LOG << "------------------EOF" << FILE_INFO;
                                            break;
                                        }
                                        else if (dwBytesRead != buffer.size()) {
                                            throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                                        }
                                    }
                                    s = toShort(buffer);
                                    DB_LOG << "s: " << s << FILE_INFO;
                                    DB_LOG << "transactionId: " << transactionId << FILE_INFO;
                                    if (s < 0) {
                                        break;
                                    }
                                }
                                DB_LOG << "wait loop break." << transactionId << FILE_INFO;
                            }
                            // 行頭に戻る
                            bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // 自身のトランザクションIDを制御情報に書き込む
                            ControlData cd{0, transactionId};
                            bErrorFlag = WriteFile(getHandle(transactionId), &cd, sizeof(cd), &dwBytesWritten, NULL);
                            DEBUG_LOG << "set transaction Id: " << transactionId;
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesWritten != sizeof(cd)) {
                                    throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                                }
                                else {
                                    DEBUG_LOG << "succeed. transactionId: " << transactionId << FILE_INFO;
                                }
                            }
                            // TemporaryDataにこの行のポジションを設定して追加する
                            std::lock_guard<std::mutex> lk{*pMt_};
                            temp_.emplace_back(save.QuadPart, transactionId, mData);
                        }
                        isSucceed = true;
                    }
                } // Scoped Lock end

                // 次の行に進む
                save.QuadPart = tableInfo_.nextRow(save.QuadPart);
                bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                if (FALSE == bErrorFlag) {
                    throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                }

                if (isSucceed) {
                    pCond_->notify_all();
                }
            } // while loop end
            return true;
        };

        // この関数名はdeleteであるべきだがc++の予約語と重なるのでupdateとする
        bool update(const TRANSACTION_ID transactionId,
                    const std::vector<std::byte> &where)
        {
            DEBUG_LOG << "delete operation";
            return update(transactionId, std::vector<std::byte>{}, where);
        }

        std::vector<std::map<std::string, std::vector<std::byte>>> select(const TRANSACTION_ID transactionId, const std::vector<std::byte> &where)
        {
            std::map<std::string, std::vector<std::byte>> mWhere = parseKeyValueVector(where);
            HANDLE h;
            BOOL bErrorFlag = FALSE;
            { // Scoped Lock start
                std::lock_guard<std::mutex> lock{*pMt_};
                h = getHandle(transactionId);
            } // Scoped Lock end
            // ファイル先頭へ移動
            LARGE_INTEGER zero;
            zero.QuadPart = 0LL;
            bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, NULL, FILE_BEGIN);
            if (FALSE == bErrorFlag) {
                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
            }

            BOOL bResult = true;
            DWORD dwBytesRead = 1;
            // DWORD dwBytesWritten = 0;
            std::vector<std::map<std::string, std::vector<std::byte>>> result;
            while (bResult && dwBytesRead != 0) {
                bResult = false;
                // update処理が成功した場合にtrue(コミットは別)
                // bool isSucceed = false;

                // 行頭位置退避
                LARGE_INTEGER save;
                bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, &save, FILE_CURRENT);
                if (FALSE == bErrorFlag) {
                    throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                }

                dwBytesRead = 0;
                // dwBytesWritten = 0;
                std::vector<std::byte> buffer;
                buffer.resize(2);
                { // Scoped Lock start
                    std::unique_lock<std::mutex> lock{*pControlMt_};
                    bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                    if (FALSE == bResult) {
                        throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                    }
                    else {
                        if (dwBytesRead == 0) {
                            DB_LOG << "------------------EOF" << FILE_INFO;
                            break;
                        }
                        else if (dwBytesRead != buffer.size()) {
                            throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                        }
                    }
                } // Scoped Lock end
                // 有効なデータであれば処理
                if (static_cast<unsigned char>(buffer[0]) == 0) {
                    // 条件に合致する行であれば戻り値に加える
                    bool isMatch = true;
                    for (const auto &e : mWhere) { // Scoped Lock start
                        // 読み込みロック
                        std::shared_lock<std::shared_mutex> lock{*pDataSharedMt_};
                        // 行頭に戻る
                        bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        // 制御情報のサイズと対象列のオフセットを加える
                        LARGE_INTEGER offset;
                        offset.QuadPart = add(tableInfo_.offset(toLower(e.first)), tableInfo_.controlDataSize());
                        bErrorFlag = SetFilePointerEx(getHandle(transactionId), offset, NULL, FILE_CURRENT);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        // 読み込んで値を確認する(whereでわたされた全ての列で等しければ更新対象となる)
                        buffer.clear();
                        buffer.resize(tableInfo_.columnSize(toLower(e.first)));
                        assertSizeLimits<DWORD>(buffer.size());
                        bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                        if (FALSE == bResult) {
                            throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        else {
                            if (dwBytesRead == 0) {
                                break;
                            }
                            else if (dwBytesRead != buffer.size()) {
                                throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                            }
                        }
                        if (!tableInfo_.isEqual(toLower(e.first), e.second, buffer)) {
                            isMatch = false;
                            break;
                        }
                    } // Scoped Lock end

                    if (isMatch) {
                        std::map<std::string, std::vector<std::byte>> lines;
                        for (const auto &e : tableInfo_.columnDefinitions()) { // Scoped Lock start
                            // 読み込みロック
                            std::shared_lock<std::shared_mutex> lock{*pDataSharedMt_};
                            // 行頭に戻る
                            bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // 制御情報のサイズと対象列のオフセットを加える
                            LARGE_INTEGER offset;
                            offset.QuadPart = add(tableInfo_.offset(toLower(std::get<0>(e))), tableInfo_.controlDataSize());
                            bErrorFlag = SetFilePointerEx(getHandle(transactionId), offset, NULL, FILE_CURRENT);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // 読み込んで値を戻り値に入れる
                            buffer.clear();
                            buffer.resize(tableInfo_.columnSize(toLower(std::get<0>(e))));
                            assertSizeLimits<DWORD>(buffer.size());
                            bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);
                            if (FALSE == bResult) {
                                throw std::runtime_error{"ReadFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesRead == 0) {
                                    break;
                                }
                                else if (dwBytesRead != buffer.size()) {
                                    throw std::runtime_error{"ReadFile() : Error: number of bytes to read != number of bytes that were read" + FILE_INFO};
                                }
                            }
                            lines.insert(std::make_pair(toLower(std::get<0>(e)), buffer));
                        } // Scoped Lock end
                        result.push_back(lines);
                    }
                }

                // 次の行に進む
                save.QuadPart = tableInfo_.nextRow(save.QuadPart);
                bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                if (FALSE == bErrorFlag) {
                    throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                }
            } // while loop end
            return result;
        }

        bool setToTerminate(const TRANSACTION_ID transactionId)
        {
            { // Scoped Lock start
                std::lock_guard<std::mutex> lockControl{*pControlMt_};
                std::lock_guard<std::mutex> lock{*pMt_};
                if (std::find(toTerminateList_.begin(), toTerminateList_.end(), transactionId) == toTerminateList_.end()) {
                    toTerminateList_.push_back(transactionId);
                }
            } //  Scoped Lock end
            rollback(transactionId);
            return true;
        }

        bool commit(const TRANSACTION_ID transactionId)
        {
            { // Scoped Lock start
                std::lock_guard<std::mutex> lockControl{*pControlMt_};
                std::lock_guard<std::mutex> lock{*pMt_};
                for (TemporaryData &td : temp_) {
                    if (td.transactionId() == transactionId) {
                        td.setToCommit();
                    }
                }
                std::lock_guard<std::shared_mutex> lockData{*pDataSharedMt_};
                write(transactionId);
            } // Scoped Lock end
            pCond_->notify_all();
            return true;
        }

        bool rollback(const TRANSACTION_ID transactionId)
        {
            { // Scoped Lock start
                std::lock_guard<std::mutex> lockControl{*pControlMt_};
                std::lock_guard<std::mutex> lock{*pMt_};
                for (TemporaryData &td : temp_) {
                    if (td.transactionId() == transactionId) {
                        td.setToCommit(false);
                    }
                }
                std::lock_guard<std::shared_mutex> lockData{*pDataSharedMt_};
                write(transactionId);
            } // Scoped Lock end
            pCond_->notify_all();
            return true;
        }

        const std::string tableName() const
        {
            return tableName_;
        }

        const std::string columnType(const std::string colName) const
        {
            return tableInfo_.columnType(toLower(colName));
        }

        const int columnSize(const std::string colName) const
        {
            return tableInfo_.columnSize(toLower(colName));
        }

        const int columnSizeTotal() const
        {
            return tableInfo_.columnSizeTotal();
        }

        bool isPermitted(const std::string operation, const std::string user) const
        {
            return tableInfo_.isPermitted(operation, user);
        }

        std::string tableInfo() const
        {
            std::ostringstream oss{""};
            for (const auto &e : tableInfo_.columnDefinitions()) {
                for (const char c : std::get<0>(e)) {
                    oss << c;
                }
                oss << '=';
                oss << std::to_string(std::get<2>(e));
                oss << ',';
            }
            std::string result = oss.str();
            if (result.length() > 1) {
                result.erase(result.end() - 1);
            }
            return result;
        }

    private:
        class TableInfo {
        public:
            TableInfo(const std::vector<std::tuple<std::string, std::string, int, int>> &columnDefinitions,
                      const std::map<std::string, std::vector<std::string>> &permissions)
                : columnDefinitions_{columnDefinitions},
                  permissions_{permissions}
            {
            }

            TableInfo() : columnDefinitions_{}, permissions_{}
            {
            }

            ~TableInfo()
            {
            }

            const std::string columnType(const std::string colName) const
            {
                for (const auto &e : columnDefinitions_) {
                    if (std::get<0>(e) == colName) {
                        return std::get<1>(e);
                    }
                }
                throw DatafileException{"cannot find column : " + colName + FILE_INFO};
            }

            const int columnSize(const std::string colName) const
            {
                for (const auto &e : columnDefinitions_) {
                    if (std::get<0>(e) == colName) {
                        return std::get<2>(e);
                    }
                }
                throw DatafileException{"cannot find column : " + colName + FILE_INFO};
            }

            const int columnSizeTotal() const
            {
                int total = 0;
                for (const auto &e : columnDefinitions_) {
                    total += std::get<2>(e);
                }
                return total;
            }

            bool isPermitted(const std::string operation, const std::string user) const
            {
                const auto &it = permissions_.find(operation);
                if (it != permissions_.end()) {
                    for (const std::string &s : it->second) {
                        if (s == user) {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool isEqual(const std::string colName,
                         const std::vector<std::byte> &lhs,
                         const std::vector<std::byte> &rhs)
            {
                const std::string colType = columnType(colName);
                if (colType == "string") {
                    // NULL終端文字を無視する
                    if (lhs.size() == rhs.size()) {
                        return lhs == rhs;
                    }
                    else if (lhs.size() > rhs.size()) {
                        size_t point = rhs.size();
                        for (size_t s = 0; s < point; ++s) {
                            if (lhs[s] != rhs[s]) {
                                return false;
                            }
                        }
                        for (size_t s = point; s < lhs.size(); ++s) {
                            if (static_cast<unsigned char>(lhs[s]) != 0) {
                                return false;
                            }
                        }
                        return true;
                    }
                    else {
                        size_t point = lhs.size();
                        for (size_t s = 0; s < point; ++s) {
                            if (lhs[s] != rhs[s]) {
                                return false;
                            }
                        }
                        for (size_t s = point; s < rhs.size(); ++s) {
                            if (static_cast<unsigned char>(rhs[s]) != 0) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
                else if (colType == "password") {
                    return lhs == rhs;
                }
                else if (colType == "datetime") {
                    return lhs == rhs;
                }
                else {
                    throw DatafileException{"unknown column type" + FILE_INFO};
                }
            }

            const std::vector<std::byte> defaultValue(const std::string colName) const
            {
                const std::string colType = columnType(colName);
                if (colType == "string") {
                    return std::vector<std::byte>{};
                }
                else if (colType == "password") {
                    throw DatafileException{"password cannot have default value" + FILE_INFO};
                }
                else if (colType == "datetime") {
                    std::vector<std::byte> v;
                    for (const char c : getLocalTimeStr()) {
                        v.push_back(static_cast<std::byte>(c));
                    }
                    return v;
                }
                else {
                    throw DatafileException{"unknown column type" + FILE_INFO};
                }
            }

            size_t controlDataSize() const
            {
                return sizeof(ControlData);
            }

            // 現在の行における現在位置に対応する次の行の位置を返す
            LONGLONG nextRow(LONGLONG current) const
            {
                if (current > LLONG_MAX - (controlDataSize() + columnSizeTotal())) {
                    return LLONG_MAX;
                }
                return current + controlDataSize() + columnSizeTotal();
            }

            // 引数の列名の行頭からのオフセットを返す
            LONGLONG offset(const std::string &colName) const
            {
                for (const auto &e : columnDefinitions_) {
                    if (std::get<0>(e) == colName) {
                        return std::get<3>(e);
                    }
                }
                throw DatafileException{"cannot find column : " + colName + FILE_INFO};
            }

            const std::vector<std::tuple<std::string, std::string, int, int>> &columnDefinitions() const
            {
                return columnDefinitions_;
            }

        private:
            // 列定義のベクタ 要素は<列名,型名,サイズ,行頭からのオフセット>
            std::vector<std::tuple<std::string, std::string, int, int>> columnDefinitions_;
            // 権限定義 key:操作名, value:その操作が可能なユーザー名
            std::map<std::string, std::vector<std::string>> permissions_;
        };

        struct ControlData {
            // 有効であれば0
            const unsigned char flag_;
            const unsigned char alignment_;
            const TRANSACTION_ID transactionId_;

            ControlData(const unsigned char flag, const TRANSACTION_ID transactionId)
                : flag_{flag}, alignment_{0}, transactionId_{transactionId}
            {
            }
        };

        class TemporaryData {
        public:
            TemporaryData(const LONGLONG position,
                          const TRANSACTION_ID transactionId,
                          const std::map<std::string, std::vector<std::byte>> &m)
                : position_{position},
                  transactionId_{transactionId},
                  m_{m},
                  toCommit_{false},
                  isFinished_{false}
            {
            }

            TemporaryData(const LONGLONG position,
                          const TRANSACTION_ID transactionId,
                          const std::map<std::string, std::vector<std::byte>> &m,
                          const bool toCommit,
                          const bool isFinished)
                : position_{position},
                  transactionId_{transactionId},
                  m_{m},
                  toCommit_{toCommit},
                  isFinished_{isFinished}
            {
            }

            // ムーブコンストラクタ
            TemporaryData(TemporaryData &&) = default;

            // コピー演算禁止
            TemporaryData(const TemporaryData &) = delete;
            TemporaryData &operator=(const TemporaryData &) = delete;
            // ムーブコピー禁止
            TemporaryData &operator=(TemporaryData &&) = delete;

            void setToCommit(bool b = true)
            {
                toCommit_ = b;
            }

            // このTemporaryDataを無効化する
            void finish()
            {
                isFinished_ = true;
            }

            const LONGLONG position() const { return position_; }
            const TRANSACTION_ID transactionId() const { return transactionId_; }
            const std::map<std::string, std::vector<std::byte>> m() const { return m_; }
            const bool toCommit() const { return toCommit_; }
            const bool isFinished() const { return isFinished_; }

        private:
            // 変更対象行のファイルポインタの位置(ファイル先頭から数える)
            const LONGLONG position_;
            // トランザクションID
            const TRANSACTION_ID transactionId_;
            // 変更用データ
            const std::map<std::string, std::vector<std::byte>> m_;
            // コミットする場合はtrue
            bool toCommit_;
            // コミット後の廃棄に利用する 廃棄する場合はtrue
            bool isFinished_;
        };

        LONGLONG add(const LONGLONG value1, const LONGLONG value2)
        {
            if ((value1 >= 0 && value2 <= 0) || (value1 <= 0 && value2 >= 0)) {
                return value1 + value2;
            }
            if (value1 >= 0 && value2 >= 0) {
                if (value1 <= LLONG_MAX - value2) {
                    return value1 + value2;
                }
            }
            if (value1 <= 0 && value2 <= 0) {
                if (value1 >= LLONG_MIN - value2) {
                    return value1 + value2;
                }
            }
            throw DatafileException("arithmetic overflow" + FILE_INFO);
        }

        HANDLE createHandle(const std::string &dataFileName)
        {
            const std::string dataFilePath = "./database/data/" + dataFileName;
            const std::filesystem::path p{dataFilePath};
            HANDLE h = CreateFile(p.wstring().c_str(),                // ファイル名
                                  GENERIC_READ | GENERIC_WRITE,       // 読み書きアクセスモード
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, // 読み書き共有モード
                                  NULL,                               // default security
                                  OPEN_EXISTING,                      // ファイルがなければエラー
                                  FILE_ATTRIBUTE_NORMAL,              // normal file
                                  NULL);

            if (h == INVALID_HANDLE_VALUE) {
                if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                    throw std::runtime_error{"GetLastError() : " + std::to_string(ERROR_FILE_NOT_FOUND) + " ERROR_FILE_NOT_FOUND" + FILE_INFO};
                }
                throw std::runtime_error{"GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
            }
            return h;
        }

        HANDLE &getHandle(const TRANSACTION_ID transactionId)
        {
            if (handles_.find(transactionId) == handles_.end()) {
                handles_.insert(std::make_pair(transactionId, createHandle(tableName_)));
            }
            return handles_.at(transactionId);
        }

        std::map<std::string, std::vector<std::byte>> parseKeyValueVector(const std::vector<std::byte> &vec)
        {
            // 中身が
            // key1="value1",key2="value2"...
            // である前提でパースしてマップにして返す
            std::map<std::string, std::vector<std::byte>> result;
            std::ostringstream oss{""};
            std::vector<std::byte> value;
            // 直前がエスケープシーケンスであった場合にtrue
            bool isESMode = false;
            // ""の内部にいる場合にtrue
            bool isInnerDq = false;
            // keyを処理中の場合にtrue
            bool isKey = true;
            // valueを処理中の場合にtrue
            bool isValue = false;
            for (const std::byte b : vec) {
                if (isValue) {
                    if (tableInfo_.columnType(toLower(oss.str())) == "password") {
                        if (value.size() != 32) {
                            value.push_back(b);
                            continue;
                        }
                    }
                }

                if (isKey) {
                    char c = static_cast<char>(b);
                    // keyは英数字とアンダーバーのみ可 イコールは以下で処理
                    if (!std::isalnum(c) && c != '_' && c != '=') {
                        if (trim(oss.str(), ' ') != "" || c != ' ') {
                            throw DatafileException{"parse error. key cannot contain '" + std::string{c} + "'" + FILE_INFO};
                        }
                    }
                }

                if (static_cast<char>(b) == '=') {
                    if (isKey) {
                        isKey = false;
                        isValue = true;
                        continue;
                    }
                    if (isValue) {
                        value.push_back(b);
                    }
                    isESMode = false;
                }
                else if (static_cast<char>(b) == '\\') {
                    if (isESMode) {
                        value.push_back(b);
                        isESMode = false;
                    }
                    else {
                        isESMode = true;
                    }
                }
                else if (static_cast<char>(b) == '"') {
                    if (isESMode) {
                        value.push_back(b);
                    }
                    else {
                        if (isInnerDq) {
                            isInnerDq = false;
                        }
                        else {
                            isInnerDq = true;
                        }
                    }
                    isESMode = false;
                }
                else if (static_cast<char>(b) == ',') {
                    if (isInnerDq) {
                        value.push_back(b);
                    }
                    else {
                        isKey = true;
                        isValue = false;
                        if (oss.str() == "") {
                            throw DatafileException{"parse error. key is empty." + FILE_INFO};
                        }
                        if (value.size() <= 0) {
                            throw DatafileException{"parse error. value is empty." + FILE_INFO};
                        }
                        result.insert(std::make_pair(oss.str(), value));
                        oss.str("");
                        value.clear();
                    }
                    isESMode = false;
                }
                else {
                    if (isKey && static_cast<char>(b) != ' ') {
                        oss << static_cast<char>(b);
                    }
                    if (isValue) {
                        value.push_back(b);
                    }
                    isESMode = false;
                }
            }
            if (oss.str() != "") {
                if (value.size() <= 0) {
                    throw DatafileException{"parse error. value is empty." + FILE_INFO};
                }
                result.insert(std::make_pair(oss.str(), value));
                oss.str("");
                value.clear();
            }
            return result;
        }

        // データファイルにトランザクションでコミットされた内容を書き込む
        // commit関数からのみ呼び出すこと
        void write(const TRANSACTION_ID id)
        {
            DWORD dwBytesWritten = 0;
            BOOL bErrorFlag = FALSE;
            for (TemporaryData &td : temp_) {
                if (td.transactionId() == id && td.toCommit()) {
                    // ファイル操作そのものをトランザクション操作するのは今回は難しいので
                    // ここで事前に発生を予測できる例外は全て発生させる
                    // 可能な限りファイル操作後の例外発生を回避する
                    for (const auto &e : td.m()) {
                        add(add(td.position(), tableInfo_.controlDataSize()), tableInfo_.offset(toLower(e.first)));
                    }
                    // 上記処理ここまで

                    if (td.position() == -1LL) {
                        // 追記なので最初にシーケンスをファイル末尾に移動する
                        LARGE_INTEGER li;
                        li.QuadPart = 0LL;
                        bErrorFlag = SetFilePointerEx(getHandle(id), li, NULL, FILE_END);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        // コントロールデータの作成
                        DEBUG_LOG << "-----------------------------" << id << FILE_INFO;
                        ControlData cd{0, -1};
                        bErrorFlag = WriteFile(getHandle(id), &cd, sizeof(cd), &dwBytesWritten, NULL);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        else {
                            if (dwBytesWritten != sizeof(cd)) {
                                throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                            }
                            else {
                                DEBUG_LOG << "succeed. transactionId: " << id << FILE_INFO;
                            }
                        }
                        // データの行頭位置を退避(制御情報ではない)
                        LARGE_INTEGER save;
                        li.QuadPart = 0LL;
                        bErrorFlag = SetFilePointerEx(getHandle(id), li, &save, FILE_CURRENT);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                        }
                        for (const auto &e : td.m()) {
                            // 列のオフセットを加える
                            LARGE_INTEGER position;
                            position.QuadPart = add(save.QuadPart, tableInfo_.offset(toLower(e.first)));
                            bErrorFlag = SetFilePointerEx(getHandle(id), position, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }

                            bErrorFlag = WriteFile(getHandle(id), e.second.data(), e.second.size(), &dwBytesWritten, NULL);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesWritten != e.second.size()) {
                                    throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                                }
                                else {
                                    DEBUG_LOG << "succeed. transactionId: " << id << FILE_INFO;
                                }
                            }
                        }
                    }
                    else {
                        // 更新の場合
                        if (td.m().size() > 0) {
                            // td.position()には制御情報も含めた開始位置が入っているのでデータ部分の先頭に移動する
                            LARGE_INTEGER li;
                            li.QuadPart = add(td.position(), tableInfo_.controlDataSize());
                            bErrorFlag = SetFilePointerEx(getHandle(id), li, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // データ部分の先頭位置退避
                            LARGE_INTEGER save = li;
                            for (const auto &e : td.m()) {
                                // 更新対象列のオフセットを加える
                                LARGE_INTEGER position;
                                position.QuadPart = add(save.QuadPart, tableInfo_.offset(toLower(e.first)));
                                bErrorFlag = SetFilePointerEx(getHandle(id), position, NULL, FILE_BEGIN);
                                if (FALSE == bErrorFlag) {
                                    throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                                }
                                // 最初に0埋めする
                                std::vector<std::byte> zeroSeq;
                                zeroSeq.resize(tableInfo_.columnSize(toLower(e.first)));
                                bErrorFlag = WriteFile(getHandle(id), zeroSeq.data(), zeroSeq.size(), &dwBytesWritten, NULL);
                                if (FALSE == bErrorFlag) {
                                    throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                                }
                                else {
                                    if (dwBytesWritten != zeroSeq.size()) {
                                        throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                                    }
                                    else {
                                        DEBUG_LOG << "succeed. transactionId: " << id << FILE_INFO;
                                    }
                                }
                                // データを書き込む
                                bErrorFlag = SetFilePointerEx(getHandle(id), position, NULL, FILE_BEGIN);
                                if (FALSE == bErrorFlag) {
                                    throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                                }
                                bErrorFlag = WriteFile(getHandle(id), e.second.data(), e.second.size(), &dwBytesWritten, NULL);
                                if (FALSE == bErrorFlag) {
                                    throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                                }
                                else {
                                    if (dwBytesWritten != e.second.size()) {
                                        throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                                    }
                                    else {
                                        DEBUG_LOG << "succeed. transactionId: " << id << FILE_INFO;
                                    }
                                }
                            }
                            // トランザクションIDを-1に戻す
                            li.QuadPart = td.position();
                            bErrorFlag = SetFilePointerEx(getHandle(id), li, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            ControlData cd{0, -1};
                            bErrorFlag = WriteFile(getHandle(id), &cd, sizeof(cd), &dwBytesWritten, NULL);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesWritten != sizeof(cd)) {
                                    throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                                }
                                else {
                                    DEBUG_LOG << "succeed. transactionId: " << id << FILE_INFO;
                                }
                            }
                        }
                        else {
                            // 削除の場合
                            LARGE_INTEGER li;
                            li.QuadPart = td.position();
                            bErrorFlag = SetFilePointerEx(getHandle(id), li, NULL, FILE_BEGIN);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            // 有効フラグを無効の状態にしてトランザクションIDを-1に戻す
                            ControlData cd{1, -1};
                            bErrorFlag = WriteFile(getHandle(id), &cd, sizeof(cd), &dwBytesWritten, NULL);
                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                            }
                            else {
                                if (dwBytesWritten != sizeof(cd)) {
                                    throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                                }
                                else {
                                    DEBUG_LOG << "delete-----------------------" << FILE_INFO;
                                    DEBUG_LOG << "succeed. transactionId: " << id << FILE_INFO;
                                }
                            }
                        }
                    }
                    td.setToCommit(false);
                    td.finish();
                }
                else if (td.transactionId() == id) {
                    // ロールバック処理
                    // このトランザクションでの更新対象となっている行のコントロールデータをデフォルト値に戻す
                    LARGE_INTEGER li;
                    li.QuadPart = td.position();
                    bErrorFlag = SetFilePointerEx(getHandle(id), li, NULL, FILE_BEGIN);
                    if (FALSE == bErrorFlag) {
                        throw std::runtime_error{"SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                    }
                    ControlData cd{0, -1};
                    bErrorFlag = WriteFile(getHandle(id), &cd, sizeof(cd), &dwBytesWritten, NULL);
                    if (FALSE == bErrorFlag) {
                        throw std::runtime_error{"WriteFile() -> GetLastError() : " + std::to_string(GetLastError()) + FILE_INFO};
                    }
                    else {
                        if (dwBytesWritten != sizeof(cd)) {
                            throw std::runtime_error{"WriteFile() : Error: number of bytes to write != number of bytes that were written" + FILE_INFO};
                        }
                        else {
                            DEBUG_LOG << "ROLLBACK succeed. transactionId: " << id << FILE_INFO;
                        }
                    }
                }
            }
            // temp_を更新する
            std::vector<TemporaryData> v;
            for (const TemporaryData &cRef : temp_) {
                if (!cRef.isFinished() || (cRef.transactionId() != id)) {
                    v.emplace_back(cRef.position(), cRef.transactionId(), cRef.m(), cRef.toCommit(), cRef.isFinished());
                }
            }
            temp_.swap(v);
        }

        template <typename T>
        void assertSizeLimits(const size_t size) const
        {
#pragma push_macro("max")
#undef max
            if (std::numeric_limits<T>::max() < size) {
#pragma pop_macro("max")
                throw DatafileException("size overflow" + FILE_INFO);
            }
        }

        bool isLittleEndian() const
        {
            const int bom = 1;
            return *reinterpret_cast<const char *>(&bom) == 1;
        }

        short toShort(const std::vector<std::byte> &bytes) const
        {
            if (bytes.size() != 2) {
                throw DatafileException{"cannot parse to short from the bytes." + FILE_INFO};
            }
            short array[2];
            for (int i = 0; i < 2; ++i) {
                array[i] = static_cast<unsigned char>(bytes.at(i));
            }
            if (isLittleEndian()) {
                return (array[1] << 8) | array[0];
            }
            return (array[0] << 8) | array[1];
        }

        std::string tableName_;
        TableInfo tableInfo_;
        std::vector<TemporaryData> temp_;
        // 何らかの原因で終了すべきトランザクションのリスト
        std::vector<TRANSACTION_ID> toTerminateList_;
        std::unique_ptr<std::mutex> pMt_;
        // 制御情報用のミューテックス
        std::unique_ptr<std::mutex> pControlMt_;
        // 制御情報用の条件変数
        std::unique_ptr<std::condition_variable> pCond_;

        // データ用のミューテックス
        std::unique_ptr<std::shared_mutex> pDataSharedMt_;

        HANDLE hFile_;
        // key: トランザクションID, value: トランザクションごとのファイルハンドル
        std::map<short, HANDLE> handles_;
    };

} // namespace PapierMache::DbStuff

#endif // DEADLOCK_EXAMPLE_DATAFILE_INCLUDED