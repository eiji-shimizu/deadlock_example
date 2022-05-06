#ifndef DEADLOCK_EXAMPLE_DATAFILE_INCLUDED
#define DEADLOCK_EXAMPLE_DATAFILE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"

#include <windows.h>
#include <winnt.h>

#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace PapierMache::DbStuff {

    class Datafile {
    public:
        Datafile(const std::string dataFileName, const std::map<std::string, std::string> &tableInfo)
            : tableName_{dataFileName},
              temp_{},
              pMt_{new std::mutex}
        {
            const std::string dataFilePath = "./database/data/" + dataFileName;
            const std::filesystem::path p{dataFilePath};

            hFile_ = CreateFile(p.wstring().c_str(),                // ファイル名
                                GENERIC_READ | GENERIC_WRITE,       // 読み書きアクセスモード
                                FILE_SHARE_READ | FILE_SHARE_WRITE, // 読み書き共有モード
                                NULL,                               // default security
                                OPEN_EXISTING,                      // ファイルがなければエラー
                                FILE_ATTRIBUTE_NORMAL,              // normal file
                                NULL);                              // no attr. template

            if (hFile_ == INVALID_HANDLE_VALUE) {
                if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                    throw std::runtime_error{"Datafile::Datafile(): GetLastError() : " + std::to_string(ERROR_FILE_NOT_FOUND) + " ERROR_FILE_NOT_FOUND"};
                }
                throw std::runtime_error{"Datafile::Datafile(): GetLastError() : " + std::to_string(GetLastError())};
            }

            std::vector<std::tuple<std::string, std::string, int>> vec;
            std::map<std::string, std::vector<std::string>> m;
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
                else {
                    std::ostringstream oss{""};
                    std::string colType;
                    for (const char c : e.second) {
                        if (c != ':') {
                            oss << c;
                        }
                        else {
                            colType = oss.str();
                            oss.str("");
                        }
                    }
                    vec.push_back(std::make_tuple(e.first, colType, std::stoi(oss.str())));
                    oss.str("");
                }
            }
            tableInfo_ = {vec, m};
        }
        ~Datafile()
        {
            CATCH_ALL_EXCEPTIONS(
                DB_LOG << "~Datafile(): " << tableName_ << " CloseHandle BEFORE";
                CloseHandle(hFile_);
                DB_LOG << "~Datafile(): " << tableName_ << " CloseHandle AFTER";)
        }

        // コピー禁止
        Datafile(const Datafile &) = delete;
        Datafile &operator=(const Datafile &) = delete;

        // ムーブコンストラクタ
        Datafile(Datafile &&rhs)
            : tableName_{std::move(rhs.tableName_)},
              tableInfo_{std::move(rhs.tableInfo_)},
              pMt_{std::move(rhs.pMt_)},
              hFile_{rhs.hFile_}
        {
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
            hFile_ = rhs.hFile_;
            rhs.hFile_ = INVALID_HANDLE_VALUE;
            return *this;
        }

        bool insert(const short transactionId, const std::vector<std::byte> &data)
        {
            std::lock_guard<std::mutex> lock{*pMt_};
            temp_.emplace_back(ULONG_MAX, transactionId, data);
            return true;
        }

        bool commit(const short transactionId)
        {
            std::lock_guard<std::mutex> lock{*pMt_};
            for (TemporaryData &td : temp_) {
                if (td.transactionId() == transactionId) {
                    DB_LOG << "--------------------commit1.";
                    td.setToCommit();
                }
            }
            write();
            DB_LOG << "--------------------commit2.";
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
            TableInfo(const std::vector<std::tuple<std::string, std::string, int>> &columnDefinitions,
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

        private:
            // 列定義のベクタ 要素は<列名,型名,サイズ>
            std::vector<std::tuple<std::string, std::string, int>> columnDefinitions_;
            // 権限定義 key:操作名, value:その操作が可能なユーザー名
            std::map<std::string, std::vector<std::string>> permissions_;
        };

        class TemporaryData {
        public:
            TemporaryData(const DWORD position,
                          const short transactionId,
                          const std::vector<std::byte> &v)
                : position_{position},
                  transactionId_{transactionId},
                  v_{v},
                  toCommit_{false}
            {
            }

            void setToCommit()
            {
                toCommit_ = true;
            }

            const DWORD position() const { return position_; }
            const short transactionId() const { return transactionId_; }
            const std::vector<std::byte> v() const { return v_; }
            const bool toCommit() const { return toCommit_; }

        private:
            // 変更対象行のファイルポインタの位置(ファイル先頭から数える)
            const DWORD position_;
            // トランザクションID
            const short transactionId_;
            // 変更用データ
            const std::vector<std::byte> v_;
            // コミットする場合はtrue
            bool toCommit_;
        };

        void write()
        {
            std::vector<size_t> indexs;
            DWORD dwBytesWritten = 0;
            BOOL bErrorFlag = FALSE;
            for (TemporaryData &td : temp_) {
                if (td.toCommit()) {
                    // TODO: データのパースが必要
                    std::vector<char> data;
                    for (const std::byte b : td.v()) {
                        data.push_back(static_cast<char>(b));
                    }
                    DB_LOG << "----------------------------write " << data.size();
                    if (td.position() == ULONG_MAX) {
                        bErrorFlag = WriteFile(
                            hFile_,          // open file handle
                            data.data(),     // start of data to write
                            data.size(),     // number of bytes to write
                            &dwBytesWritten, // number of bytes that were written
                            NULL);           // no overlapped structure

                        if (FALSE == bErrorFlag) {
                            throw std::runtime_error{"Terminal failure: Unable to write to file."};
                        }
                        else {
                            if (dwBytesWritten != data.size()) {
                                // This is an error because a synchronous write that results in
                                // success (WriteFile returns TRUE) should write all data as
                                // requested. This would not necessarily be the case for
                                // asynchronous writes.
                                throw std::runtime_error{"Error: dwBytesWritten != dwBytesToWrite"};
                            }
                            else {
                                DB_LOG << "Datafile::write(): succeed";
                            }
                        }
                    }
                }
            }
        }

        std::string tableName_;
        TableInfo tableInfo_;
        std::vector<TemporaryData> temp_;
        std::unique_ptr<std::mutex> pMt_;
        HANDLE hFile_;
    };

} // namespace PapierMache::DbStuff

#endif // DEADLOCK_EXAMPLE_DATAFILE_INCLUDED