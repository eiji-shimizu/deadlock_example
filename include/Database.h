#ifndef DEADLOCK_EXAMPLE_DATABASE_INCLUDED
#define DEADLOCK_EXAMPLE_DATABASE_INCLUDED

#include "Common.h"
#include "Logger.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace PapierMache::DbStuff {

    class Database;

    bool sendData(Database &db, const int connectionId, const std::vector<std::byte> &data);
    bool receiveData(Database &db, const int connectionId, std::vector<std::byte> &out);
    bool isClosedConnection(Database &db, const int connectionId);

    class Connection {
        friend Database;

    public:
        ~Connection()
        {
            // noop
        }

        void close()
        {
            if (!isClosed()) {
                std::vector<std::byte> data;
                std::string closeRequest = "PLEASE:CLOSE";
                for (const char c : closeRequest) {
                    data.push_back(static_cast<std::byte>(c));
                }
                send(data);
            }
        }

        // 引数のデータを送信する
        bool send(const std::vector<std::byte> &data)
        {
            return sendData(refDb_, id_, data);
        }

        // 引数にデータを受信する
        bool receive(std::vector<std::byte> &data)
        {
            return receiveData(refDb_, id_, data);
        }

        bool isClosed()
        {
            return isClosedConnection(refDb_, id_);
        }

        int id() const { return id_; }

    private:
        Connection(int id, Database &refDb)
            : id_{id},
              refDb_{refDb},
              isInUse_{false}
        {
        }

        int id_;
        Database &refDb_;
        bool isInUse_;
    };

    class Database {
    public:
        using DataStream = std::vector<std::byte>;

        class Transaction {
        public:
            Transaction(int id, int connectionId)
                : id_{id},
                  connectionId_{connectionId}
            {
            }

        private:
            int id_;
            int connectionId_;
        };

        class Table {
        private:
            int id_;
            std::string name_;
        };

        Database()
            : connectionId_{0},
              transactionId_{0},
              tableId_{0},
              isRequiredConnection_{false},
              isStarted_{false}
        {
        }
        ~Database()
        {
            CATCH_ALL_EXCEPTIONS(
                if (thread_.joinable()) {
                    thread_.join();
                })
        }

        void start()
        {
            std::unique_lock<std::mutex> lock{mt_};
            if (isStarted_) {
                return;
            }
            thread_ = std::thread{[this] {
                try {
                    startService();
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
                }
            }};
            isStarted_ = true;
        }

        // コピー禁止
        Database(const Database &) = delete;
        Database &operator=(const Database &) = delete;
        // ムーブ禁止
        Database(Database &&) = delete;
        Database &operator=(Database &&) = delete;

        void acceptConnectionRequest()
        {
        }

        const Connection getConnection()
        {
            std::unique_lock<std::mutex> lock{mt_};
            logger.stream().out() << "Database::getConnection()";
            if (isRequiredConnection_) {
                throw std::runtime_error("Database::getConnection() : concurrency problems occurs.");
            }
            for (Connection &rc : connectionList_) {
                if (!rc.isInUse_) {
                    rc.isInUse_ = true;
                    return rc;
                }
            }
            isRequiredConnection_ = true;
            // コネクションが作成されるまでwaitする
            cond_.wait(lock, [this] { return isRequiredConnection_ == false; });
            for (Connection &rc : connectionList_) {
                if (!rc.isInUse_) {
                    rc.isInUse_ = true;
                    return rc;
                }
            }
            throw std::runtime_error("Database::getConnection() : cannot find connection.");
        }

        const Transaction createTransaction(const int conId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            Transaction t{transactionId_++, conId};
            transactionList_.push_back(t);
            return t;
        }

        bool receive(const int connectionId, std::vector<std::byte> &out)
        {
            std::lock_guard<std::mutex> lock{mt_};
            logger.stream().out() << "connectionId: " << connectionId << " receive BEFOER";
            DataStream ds;
            swap(connectionId, ds);
            ds.swap(out);
            return true;
        }

        bool send(const int connectionId, const std::vector<std::byte> &data)
        {
            std::lock_guard<std::mutex> lock{mt_};
            logger.stream().out() << "connectionId: " << connectionId << " send BEFOER";
            return swap(connectionId, data);
        }

        bool isClosed(const int connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            return connectionList_.end() == std::find_if(connectionList_.begin(),
                                                         connectionList_.end(),
                                                         [connectionId](const Connection c) { return c.id() == connectionId; });
        }

    private:
        bool swap(const int connectionId, const std::vector<std::byte> &data)
        {
            DataStream temp{data};
            logger.stream().out() << "connectionId: " << connectionId << " const swap BEFOER";
            dataStreams_.at(connectionId).swap(temp);
            logger.stream().out() << "connectionId: " << connectionId << " const swap AFTER";
            return true;
        }

        bool swap(const int connectionId, std::vector<std::byte> &data)
        {
            logger.stream().out() << "connectionId: " << connectionId << " swap BEFOER";
            dataStreams_.at(connectionId).swap(data);
            logger.stream().out() << "connectionId: " << connectionId << " swap AFTER";
            return true;
        }

        const Connection createConnection()
        {
            Connection con{connectionId_++, *this};
            connectionList_.push_back(con);
            DataStream ds;
            dataStreams_.insert(std::make_pair(con.id(), ds));
            return con;
        }

        void startService()
        {
            try {
                while (true) {
                    logger.stream().out() << "--------------------1.";
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    logger.stream().out() << "--------------------2.";
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{mt_};
                        logger.stream().out() << "--------------------3.";
                        if (isRequiredConnection_) {
                            createConnection();
                        }
                        isRequiredConnection_ = false;
                    } // Scoped Lock end
                    logger.stream().out() << "--------------------4.";
                    cond_.notify_all();
                    logger.stream().out() << "--------------------5.";
                }
            }
            catch (std::exception &e) {
                logger.stream().out() << e.what();
            }
        }

        int connectionId_;
        int transactionId_;
        int tableId_;
        std::vector<Connection> connectionList_;
        std::vector<Transaction> transactionList_;
        std::vector<Table> tableList_;
        // int: ConnectionのId, DataStream: 送受信データ
        std::map<int, DataStream> dataStreams_;
        // コネクション作成要求がある場合にtrue
        bool isRequiredConnection_;
        // 上記bool用の条件変数
        std::condition_variable cond_;
        // データベースサービスのメインスレッド
        std::thread thread_;
        // サービスが開始していればtrue
        bool isStarted_;
        std::mutex mt_;
    };

    inline bool sendData(Database &db, const int connectionId, const std::vector<std::byte> &data)
    {
        return db.send(connectionId, data);
    }

    inline bool isClosedConnection(Database &db, const int connectionId)
    {
        return db.isClosed(connectionId);
    }

    inline bool receiveData(Database &db, const int connectionId, std::vector<std::byte> &out)
    {
        return db.receive(connectionId, out);
    }

    class DbDriver {
    public:
        DbDriver(const Connection con)
            : con_{con}
        {
        }

        bool sendQuery(std::string query)
        {
            std::vector<std::byte> data;
            for (const char c : query) {
                data.push_back(static_cast<std::byte>(c));
            }
            return con_.send(data);
        }

        std::map<std::string, std::string> sendQuery(std::string query, bool &result)
        {
            std::map<std::string, std::string> tableRows;
            std::vector<std::byte> data;
            for (const char c : query) {
                data.push_back(static_cast<std::byte>(c));
            }
            if (!con_.send(data)) {
                result = false;
                return tableRows;
            }
            if (!con_.receive(data)) {
                result = false;
                return tableRows;
            }
            // TODO: 受信データをパースする
            return tableRows;
        }

    private:
        Connection con_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_DATABASE_INCLUDED