#ifndef DEADLOCK_EXAMPLE_DATAFILE_INCLUDED
#define DEADLOCK_EXAMPLE_DATAFILE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"

#include <windows.h>
#include <winnt.h>

#include <condition_variable>
#include <exception>
#include <filesystem>
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

    class Datafile {
    public:
        Datafile(const std::string dataFileName, const std::map<std::string, std::string> &tableInfo)
            : tableName_{dataFileName},
              temp_{},
              pMt_{new std::mutex},
              pControlMt_{new std::mutex},
              isUpdating_{false},
              pCond_{new std::condition_variable},
              pDataSharedMt_{new std::shared_mutex}
        {
            hFile_ = createHandle(dataFileName);
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
                    m.insert(std::make_pair(e.first, users));
                }
                else if (e.first == "COLUMN_ORDER") {
                    int no = 0;
                    std::ostringstream oss{""};
                    for (const char c : e.second) {
                        if (c != ',') {
                            oss << c;
                        }
                        else {
                            order.insert(std::make_pair(oss.str(), no++));
                            oss.str("");
                        }
                    }
                    if (oss.str() != "") {
                        order.insert(std::make_pair(oss.str(), no++));
                        oss.str("");
                    }
                }
                else {
                    std::ostringstream oss{""};
                    std::string colType;
                    for (const char c : e.second) {
                        if (!std::isalnum(c) && c != '_' && c != ':') {
                            throw std::runtime_error{"parse error. cannot use '" + std::string{c} + "'"};
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
                        throw std::runtime_error{"column size cannot be zero."};
                    }
                    vec.push_back(std::make_tuple(e.first, colType, std::stoi(oss.str()), 0));
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
                    throw std::runtime_error{"Datafile::Datafile(): arithmetic overflow"};
                }
                offset += std::get<2>(e);
            }

            tableInfo_ = {vec, m};
        }
        ~Datafile(){
            CATCH_ALL_EXCEPTIONS({
                DB_LOG << "~Datafile(): " << tableName_ << " CloseHandle BEFORE";
                for (auto &e : handles_) {
                    CloseHandle(e.second);
                }
                CloseHandle(hFile_);
                DB_LOG << "~Datafile(): " << tableName_ << " CloseHandle AFTER";
            })}

        // コピー禁止
        Datafile(const Datafile &) = delete;
        Datafile &operator=(const Datafile &) = delete;

        // ムーブコンストラクタ
        Datafile(Datafile &&rhs)
            : tableName_{std::move(rhs.tableName_)},
              tableInfo_{std::move(rhs.tableInfo_)},
              pMt_{std::move(rhs.pMt_)},
              pControlMt_{std::move(rhs.pControlMt_)},
              isUpdating_{rhs.isUpdating_},
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
        // ムーブ代入
        Datafile &operator=(Datafile &&rhs)
        {
            std::lock_guard<std::mutex> lock{*pMt_};
            if (this == &rhs) {
                return *this;
            }
            tableName_ = std::move(rhs.tableName_);
            tableInfo_ = std::move(rhs.tableInfo_);
            pMt_ = std::move(rhs.pMt_);
            pControlMt_ = std::move(rhs.pControlMt_);
            isUpdating_ = rhs.isUpdating_;
            pCond_ = std::move(rhs.pCond_);
            pDataSharedMt_ = std::move(rhs.pDataSharedMt_);
            hFile_ = rhs.hFile_;
            handles_ = std::move(rhs.handles_);
            for (auto &e : rhs.handles_) {
                e.second = INVALID_HANDLE_VALUE;
            }
            rhs.hFile_ = INVALID_HANDLE_VALUE;
            return *this;
        }

        bool insert(const TRANSACTION_ID transactionId, const std::vector<std::byte> &data)
        {
            std::map<std::string, std::vector<std::byte>> m = parseKeyValueVector(data);
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
            LARGE_INTEGER zero;
            zero.QuadPart = 0LL;
            bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, NULL, FILE_BEGIN);
            if (FALSE == bErrorFlag) {
                throw std::runtime_error{"Datafile::update(): SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError())};
            }

            std::vector<LONGLONG> position;

            BOOL bResult = true;
            DWORD dwBytesRead = 1;
            while (bResult && dwBytesRead != 0) {
                bResult = false;
                // 処理が成功した場合にtrue
                bool isSucceed = false;

                // 行頭位置退避用
                LARGE_INTEGER save;
                bErrorFlag = SetFilePointerEx(getHandle(transactionId), zero, &save, FILE_CURRENT);
                if (FALSE == bErrorFlag) {
                    throw std::runtime_error{"Datafile::update(): SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError())};
                }

                dwBytesRead = 0;
                std::vector<std::byte> buffer;
                buffer.resize(2);

                { // Scoped Lock start
                    // 書き込みロック
                    std::unique_lock<std::mutex> lock{*pControlMt_};
                    bResult = ReadFile(h, buffer.data(), buffer.size(), &dwBytesRead, NULL);

                    if (FALSE == bResult) {
                        throw std::runtime_error{"Datafile::update(): ReadFile() -> GetLastError() : " + std::to_string(GetLastError())};
                    }
                    else {
                        if (dwBytesRead == 0) {
                            DB_LOG << "------------------EOF";
                            break;
                        }
                        else if (dwBytesRead != buffer.size()) {
                            throw std::runtime_error{"Datafile::write(): ReadFile() : Error: number of bytes to read != number of bytes that were read"};
                        }
                    }
                    const short s = toShort(buffer);
                    DB_LOG << "s: " << s;
                    DB_LOG << "transactionId: " << transactionId;
                    if (s >= 0 && s != transactionId) {
                        // TODO: この場合は別トランザクションがロックしているので待たなければならない
                        if (!isUpdating_) {
                            throw std::runtime_error{"Datafile::update(): isUpdating_ should be true but actually false."};
                        }
                        while (true) {
                            DB_LOG << "Datafile::update() wait start.";
                            pCond_->wait(lock, [this] { return !isUpdating_; });
                            DB_LOG << "Datafile::update() wait end.";

                            if (s == 0) {
                                break;
                            }
                            if (!isUpdating_) {
                                throw std::runtime_error{"Datafile::update(): isUpdating_ should be true but actually false."};
                            }
                        }

                        DB_LOG << "Overcame!!";

                    }
                    else {
                        if (isUpdating_) {
                            throw std::runtime_error{"Datafile::update(): isUpdating_ should be false but actually true."};
                        }
                        isUpdating_ = true;
                       
                        // TODO: 条件に合致する行であればトランザクションIDを書き込む

                        // TemporaryDataにこの行のポジションを設定して追加する

                        isSucceed = true;
                        isUpdating_ = false;
                    }
                } // Scoped Lock end

                // 次の行に進む
                save.QuadPart = tableInfo_.nextRow(save.QuadPart);
                bErrorFlag = SetFilePointerEx(getHandle(transactionId), save, NULL, FILE_BEGIN);
                if (FALSE == bErrorFlag) {
                    throw std::runtime_error{"Datafile::update(): SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError())};
                }

                if (isSucceed) {
                    pCond_->notify_all();
                }
            } // while loop end
            return true;
        };

        bool commit(const TRANSACTION_ID transactionId)
        {
            std::lock_guard<std::mutex> lock{*pMt_};
            for (TemporaryData &td : temp_) {
                if (td.transactionId() == transactionId) {
                    td.setToCommit();
                }
            }
            std::lock_guard<std::mutex> lockControl{*pControlMt_};
            std::lock_guard<std::shared_mutex> lockData{*pDataSharedMt_};
            write();
            return true;
        }

        const std::string tableName() const
        {
            return tableName_;
        }

        const std::string columnType(const std::string colName) const
        {
            return tableInfo_.columnType(colName);
        }

        const int columnSize(const std::string colName) const
        {
            return tableInfo_.columnSize(colName);
        }

        const int columnSizeTotal() const
        {
            return tableInfo_.columnSizeTotal();
        }

        bool isPermitted(const std::string operation, const std::string user) const
        {
            return tableInfo_.isPermitted(operation, user);
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
                // LOG << "columnDefinitions_";
                // for (const auto &e : columnDefinitions_) {
                //     LOG << std::get<0>(e) << ", " << std::get<1>(e) << ", " << std::get<2>(e);
                // }
                // LOG << "permissions_";
                // for (const auto &e : permissions_) {
                //     LOG << e.first;
                //     for (const auto &userName : e.second) {
                //         LOG << userName;
                //     }
                // }
            }

            const std::string columnType(const std::string colName) const
            {
                for (const auto &e : columnDefinitions_) {
                    if (std::get<0>(e) == colName) {
                        return std::get<1>(e);
                    }
                }
                throw std::runtime_error{"cannot find column : " + colName};
            }

            const int columnSize(const std::string colName) const
            {
                for (const auto &e : columnDefinitions_) {
                    if (std::get<0>(e) == colName) {
                        return std::get<2>(e);
                    }
                }
                throw std::runtime_error{"cannot find column : " + colName};
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
                throw std::runtime_error{"cannot find column : " + colName};
            }

        private:
            // 列定義のベクタ 要素は<列名,型名,サイズ,行頭からのオフセット>
            std::vector<std::tuple<std::string, std::string, int, int>> columnDefinitions_;
            // 権限定義 key:操作名, value:その操作が可能なユーザー名
            std::map<std::string, std::vector<std::string>> permissions_;
        };

        struct ControlData {
            const TRANSACTION_ID transactionId_;
            // 有効であれば0
            const unsigned char flag_;

            ControlData(const TRANSACTION_ID transactionId, const unsigned char flag)
                : transactionId_{transactionId}, flag_{flag}
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
                  toCommit_{false}
            {
            }

            void setToCommit(bool b = true)
            {
                toCommit_ = b;
            }

            const LONGLONG position() const { return position_; }
            const TRANSACTION_ID transactionId() const { return transactionId_; }
            const std::map<std::string, std::vector<std::byte>> m() const { return m_; }
            const bool toCommit() const { return toCommit_; }

        private:
            // 変更対象行のファイルポインタの位置(ファイル先頭から数える)
            const LONGLONG position_;
            // トランザクションID
            const TRANSACTION_ID transactionId_;
            // 変更用データ
            const std::map<std::string, std::vector<std::byte>> m_;
            // コミットする場合はtrue
            bool toCommit_;
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
            throw std::runtime_error("Datafile::add(): arithmetic overflow");
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
                    throw std::runtime_error{"Datafile::Datafile(): GetLastError() : " + std::to_string(ERROR_FILE_NOT_FOUND) + " ERROR_FILE_NOT_FOUND"};
                }
                throw std::runtime_error{"Datafile::Datafile(): GetLastError() : " + std::to_string(GetLastError())};
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
                if (isKey) {
                    char c = static_cast<char>(b);
                    // keyは英数字とアンダーバーのみ可 イコールは以下で処理
                    if (!std::isalnum(c) && c != '_' && c != '=') {
                        throw std::runtime_error{"Datafile::parseKeyValueVector(): parse error. key cannot contain '" + std::string{c} + "'"};
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
                        isESMode = false;
                    }
                    else {
                        if (isInnerDq) {
                            isInnerDq = false;
                        }
                        else {
                            isInnerDq = true;
                        }
                    }
                }
                else if (static_cast<char>(b) == ',') {
                    if (isInnerDq) {
                        value.push_back(b);
                    }
                    else {
                        isKey = true;
                        isValue = false;
                        if (oss.str() == "") {
                            throw std::runtime_error{"Datafile::parseKeyValueVector(): parse error. key is empty."};
                        }
                        if (value.size() <= 0) {
                            throw std::runtime_error{"Datafile::parseKeyValueVector(): parse error. value is empty."};
                        }
                        result.insert(std::make_pair(oss.str(), value));
                        oss.str("");
                        value.clear();
                    }
                }
                else {
                    if (isKey) {
                        oss << static_cast<char>(b);
                    }
                    if (isValue) {
                        value.push_back(b);
                    }
                }
            }
            if (oss.str() != "") {
                if (value.size() <= 0) {
                    throw std::runtime_error{"Datafile::parseKeyValueVector(): parse error. value is empty."};
                }
                result.insert(std::make_pair(oss.str(), value));
                oss.str("");
                value.clear();
            }
            return result;
        }

        void write()
        {
            std::vector<size_t> indexs;
            DWORD dwBytesWritten = 0;
            BOOL bErrorFlag = FALSE;
            for (TemporaryData &td : temp_) {
                if (td.toCommit()) {
                    // TODO: データのパースが必要
                    // std::vector<char> data;
                    // for (const std::byte b : td.v()) {
                    //     data.push_back(static_cast<char>(b));
                    // }
                    if (td.position() == -1LL) {
                        // 追記なので最初にシーケンスをファイル末尾に移動する
                        LARGE_INTEGER li;
                        li.QuadPart = 0LL;
                        bErrorFlag = SetFilePointerEx(getHandle(td.transactionId()), li, NULL, FILE_END);
                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"Datafile::write(): SetFilePointerEx() -> GetLastError() : " + std::to_string(GetLastError())};
                        }
                        // コントロールデータの作成
                        DB_LOG << "-----------------------------" << td.transactionId();
                        ControlData cd{-1, 0};
                        bErrorFlag = WriteFile(getHandle(td.transactionId()), // open file handle
                                               &cd,                           // start of data to write
                                               sizeof(cd),                    // number of bytes to write
                                               &dwBytesWritten,               // number of bytes that were written
                                               NULL);                         // no overlapped structure

                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"Datafile::write(): WriteFile() -> GetLastError() : " + std::to_string(GetLastError())};
                        }
                        else {
                            if (dwBytesWritten != sizeof(cd)) {
                                // microsoftのサンプルコードのコメントそのまま残す
                                // (https://docs.microsoft.com/ja-jp/windows/win32/fileio/opening-a-file-for-reading-or-writing)
                                // This is an error because a synchronous write that results in
                                // success (WriteFile returns TRUE) should write all data as
                                // requested. This would not necessarily be the case for
                                // asynchronous writes.
                                throw std::runtime_error{"Datafile::write(): WriteFile() : Error: number of bytes to write != number of bytes that were written"};
                            }
                            else {
                                DB_LOG << "Datafile::write(): succeed. transactionId: " << td.transactionId();
                            }
                        }
                        LARGE_INTEGER save;
                        li.QuadPart = 0LL;
                        SetFilePointerEx(getHandle(td.transactionId()), li, &save, FILE_CURRENT);
                        // const LONGLONG startPosition = save.QuadPart;
                        for (const auto &e : td.m()) {
                            DB_LOG << e.first;
                            LARGE_INTEGER position;
                            position.QuadPart = add(save.QuadPart, tableInfo_.offset(e.first));
                            // position.QuadPart = add(save.QuadPart, add(tableInfo_.offset(e.first), tableInfo_.columnSize(e.first)));
                            SetFilePointerEx(getHandle(td.transactionId()), position, NULL, FILE_BEGIN);

                            bErrorFlag = WriteFile(getHandle(td.transactionId()), // open file handle
                                                   e.second.data(),               // start of data to write
                                                   e.second.size(),               // number of bytes to write
                                                   &dwBytesWritten,               // number of bytes that were written
                                                   NULL);                         // no overlapped structure

                            if (FALSE == bErrorFlag) {
                                throw std::runtime_error{"Datafile::write(): WriteFile() -> GetLastError() : " + std::to_string(GetLastError())};
                            }
                            else {
                                if (dwBytesWritten != e.second.size()) {
                                    // microsoftのサンプルコードのコメントそのまま残す(https://docs.microsoft.com/ja-jp/windows/win32/fileio/opening-a-file-for-reading-or-writing)
                                    // This is an error because a synchronous write that results in
                                    // success (WriteFile returns TRUE) should write all data as
                                    // requested. This would not necessarily be the case for
                                    // asynchronous writes.
                                    throw std::runtime_error{"Datafile::write(): WriteFile() : Error: number of bytes to write != number of bytes that were written"};
                                }
                                else {
                                    DB_LOG << "Datafile::write(): succeed. transactionId: " << td.transactionId();
                                }
                            }
                        }
                    }
                    else {
                        // 更新または削除の場合
                    }

                    td.setToCommit(false);
                }
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
                throw std::runtime_error{"cannot parse to short from the bytes."};
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
        std::unique_ptr<std::mutex> pMt_;
        // 制御情報用のミューテックス
        std::unique_ptr<std::mutex> pControlMt_;
        // 制御情報用の条件変数
        bool isUpdating_;
        std::unique_ptr<std::condition_variable> pCond_;

        // データ用のミューテックス
        std::unique_ptr<std::shared_mutex> pDataSharedMt_;

        HANDLE hFile_;
        // key: トランザクションID, value: トランザクションごとのファイルハンドル
        std::map<short, HANDLE> handles_;
    };

} // namespace PapierMache::DbStuff

#endif // DEADLOCK_EXAMPLE_DATAFILE_INCLUDED