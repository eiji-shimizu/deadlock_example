#ifndef DEADLOCK_EXAMPLE_DATABASE_INCLUDED
#define DEADLOCK_EXAMPLE_DATABASE_INCLUDED

#include "Common.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace PapierMache::DbStuff {

    class Database;

    bool sendData(Database &db, const int connectionId, const std::vector<std::byte> &data);
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

        bool send(const std::vector<std::byte> &data)
        {
            return sendData(refDb_, id_, data);
        }

        bool isClosed()
        {
            return isClosedConnection(refDb_, id_);
        }

        int id() const { return id_; }

    private:
        Connection(int id, Database &refDb)
            : id_{id},
              refDb_{refDb}
        {
        }

        int id_;
        Database &refDb_;
    };

    class Database {
    public:
        using DataStream = std::vector<std::byte>;

        class Transaction {
        private:
            int id_;
        };

        class Table {
        private:
            int id_;
            std::string name_;
        };

        Database()
            : connectionId_{0},
              transactionId_{0},
              tableId_{0}
        {
        }
        ~Database()
        {
            logger.stream().out() << "~Database()";
            // noop
        }

        // コピー禁止
        Database(const Database &) = delete;
        Database &operator=(const Database &) = delete;
        // ムーブ禁止
        Database(Database &&) = delete;
        Database &operator=(Database &&) = delete;

        const Connection createConnection()
        {
            std::lock_guard<std::mutex> lock{mt_};
            Connection con{connectionId_++, *this};
            connectionList_.push_back(con);
            DataStream ds;
            dataStreams_.insert(std::make_pair(con.id(), ds));
            return con;
        }

        const Transaction createTransaction()
        {
            std::lock_guard<std::mutex> lock{mt_};
            Transaction t{};
            transactionList_.push_back(t);
            return t;
        }

        bool recieve(const int connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            logger.stream().out() << "connectionId: " << connectionId << " recieve BEFOER";
            DataStream ds;
            swap(connectionId, ds);
            std::ostringstream oss{""};
            for (const std::byte b : ds) {
                oss << static_cast<char>(b);
            }
            logger.stream().out() << oss.str();

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

        int connectionId_;
        int transactionId_;
        int tableId_;
        std::vector<Connection> connectionList_;
        std::vector<Transaction> transactionList_;
        std::vector<Table> tableList_;
        // int: ConnectionのId, DataStream: 送受信データ
        std::map<int, DataStream> dataStreams_;
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

    class DbDriver {
    public:
        DbDriver(const Connection con)
            : con_{con}
        {
        }

    private:
        Connection con_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_DATABASE_INCLUDED