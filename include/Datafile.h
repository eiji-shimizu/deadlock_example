#ifndef DEADLOCK_EXAMPLE_DATAFILE_INCLUDED
#define DEADLOCK_EXAMPLE_DATAFILE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"

#include <windows.h>

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

        bool isPermitted(const std::string operation, const std::string user)
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

            bool isPermitted(const std::string operation, const std::string user)
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

        std::string tableName_;
        TableInfo tableInfo_;
        std::unique_ptr<std::mutex> pMt_;
        HANDLE hFile_;
    };

} // namespace PapierMache::DbStuff

#endif // DEADLOCK_EXAMPLE_DATAFILE_INCLUDED