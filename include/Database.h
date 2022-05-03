#ifndef DEADLOCK_EXAMPLE_DATABASE_INCLUDED
#define DEADLOCK_EXAMPLE_DATABASE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"
#include "ThreadsMap.h"
#include "UUID.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace PapierMache::DbStuff {

    class Database;

    class Connection {
        friend Database;

    public:
        ~Connection()
        {
            // logger.stream().out() << " ~Connection()";
            //   noop
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

        // トランザクションを開始する
        bool beginTransaction();
        // 引数のデータを送信する
        bool send(const std::vector<std::byte> &data);
        // 引数にデータを受信する
        bool receive(std::vector<std::byte> &out);
        // 送信したデータの処理を依頼する
        void request();
        // 処理結果を待つ
        void wait();
        // このコネクションがクローズしていればtrue
        bool isClosed();

        const std::string id() const { return id_; }

    private:
        Connection(const std::string id, Database &refDb)
            : id_{id},
              refDb_{refDb},
              isInUse_{false}
        {
        }

        const std::string id_;
        Database &refDb_;
        bool isInUse_;
    };

    class Database {
    public:
        using DataStream = std::vector<std::byte>;

        // 0: ConnectionのId
        // 1: ミューテックス
        // 2: 条件変数
        // 3: クライアントから処理要求がある場合にtrue
        // このセッションが終了した場合はConnectionのIdは空文字列になる
        using SessionCondition = std::tuple<std::string, std::mutex, std::condition_variable, bool>;

        Database()
            : transactionId_{0},
              tableId_{0},
              isRequiredConnection_{false},
              isStarted_{false},
              toBeStoped_{false}
        {
        }
        ~Database()
        {
            CATCH_ALL_EXCEPTIONS({
                logger.stream().out() << " ~Database()";
                threads_.setFinishedFlagAll();
                { // Scoped Lock
                    std::lock_guard<std::mutex> lock{mt_};
                    for (int i = 0; i < conditions_.size(); ++i) {
                        {
                            std::shared_lock<std::shared_mutex> sh(sharedMt_);
                            if (std::get<0>(conditions_[i]) != "") {
                                logger.stream().out() << "notify_all() BEFORE";
                                std::lock_guard<std::mutex> lock2{std::get<1>(conditions_[i])};
                                if (!std::get<3>(conditions_[i])) {
                                    std::get<3>(conditions_[i]) = true;
                                }
                                std::get<2>(conditions_[i]).notify_all();
                                logger.stream().out() << "notify_all() AFTER";
                            }
                        }
                    }
                }

                toBeStoped_.store(true);
                if (thread_.joinable()) {
                    logger.stream().out() << " thread_.join() BEFORE";
                    thread_.join();
                }

                logger.stream().out() << " thread_.join() AFTER";
            })
        }

        void start()
        {
            std::lock_guard<std::mutex> lock{mt_};
            if (isStarted_) {
                return;
            }
            thread_ = std::thread{[this] {
                try {
                    startService();
                    logger.stream().out() << "Database service is stop.";
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

        // コネクションとのインターフェース関数 ここから
        bool setTransaction(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            Transaction t{transactionId_++, connectionId};
            transactionList_.push_back(t);
            return true;
        }

        bool getData(const std::string connectionId, std::vector<std::byte> &out)
        {
            std::lock_guard<std::mutex> lock{mt_};
            logger.stream().out() << "connectionId: " << connectionId << " getData";
            DataStream ds;
            swap(connectionId, ds);
            ds.swap(out);
            return true;
        }

        bool setData(const std::string connectionId, const std::vector<std::byte> &data)
        {
            std::lock_guard<std::mutex> lock{mt_};
            logger.stream().out() << "connectionId: " << connectionId << " setData";
            return swap(connectionId, data);
        }

        void toNotify(const std::string connectionId, const bool b = false)
        {
            notifyImpl(connectionId, b);
        }

        void wait(const std::string connectionId)
        {
            int i = 0;
            { // 読み込みロック
                std::shared_lock<std::shared_mutex> lock(sharedMt_);
                for (i = 0; i < conditions_.size(); ++i) {
                    const SessionCondition &sc = conditions_.at(i);
                    if (std::get<0>(sc) == connectionId) {
                        logger.stream().out() << "------------------------------wait 1.";
                        break;
                    }
                }
            }
            if (i == conditions_.size()) {
                return;
            }
            logger.stream().out() << "------------------------------wait 2.";
            std::unique_lock<std::mutex> lock{std::get<1>(conditions_.at(i))};
            if (std::get<3>(conditions_.at(i))) {
                logger.stream().out() << "------------------------------wait 3.";
                std::get<2>(conditions_.at(i)).wait(lock, [&refB = std::get<3>(conditions_.at(i))] {
                    return refB == false;
                });
            }
            logger.stream().out() << "------------------------------wait 4.";
        }

        bool isClosed(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            return connectionList_.end() == std::find_if(connectionList_.begin(),
                                                         connectionList_.end(),
                                                         [connectionId](const Connection c) { return c.id() == connectionId; });
        }
        // コネクションとのインターフェース関数 ここまで

    private:
        class Transaction {
        public:
            Transaction(const short id, const std::string connectionId)
                : id_{id},
                  connectionId_{connectionId}
            {
            }

            ~Transaction()
            {
                logger.stream().out() << " ~Transaction()";
                for (const auto &e : toCommit_) {
                    logger.stream().out() << e.first;
                    std::ostringstream oss{""};
                    for (const auto &v : e.second) {
                        for (const std::byte b : v) {
                            oss << static_cast<char>(b);
                        }
                    }
                    logger.stream().out() << oss.str();
                }
            }

            void commit()
            {
                // TODO:
            }

            void rollback()
            {
                toCommit_.clear();
            }

            void addToCommit(const std::string tableName, const std::vector<std::byte> &bytes)
            {
                if (toCommit_.find(tableName) == toCommit_.end()) {
                    std::vector<std::vector<std::byte>> vec{};
                    toCommit_.insert(std::make_pair(tableName, vec));
                }
                toCommit_.at(tableName).push_back(bytes);
            }

            const short id() const
            {
                return id_;
            }

            const std::string connectionId() const
            {
                return connectionId_;
            }

        private:
            const short id_;
            const std::string connectionId_;
            // key: テーブル名, value: コミット予定の値のベクタ
            std::map<std::string, std::vector<std::vector<std::byte>>> toCommit_;
        };

        class Table {
        public:
            Table(const short id,
                  const std::string name)
                : id_{id},
                  name_{name}
            {
            }

            ~Table()
            {
                logger.stream().out() << " ~Table()";
                // noop
            }

            bool setTarget(const short rowNo, const short transactionId)
            {
                // std::lock_guard<std::mutex> lock{mt_};
                if (isTarget_.find(rowNo) != isTarget_.end()) {
                    if (isTarget_.at(rowNo) >= 0) {
                        return false;
                    }
                    isTarget_.at(rowNo) = transactionId;
                    return true;
                }
                isTarget_.insert(std::make_pair(rowNo, transactionId));
                return true;
            }

            bool clearTarget(const short rowNo, const short transactionId)
            {
                // std::lock_guard<std::mutex> lock{mt_};
                if (isTarget_.find(rowNo) != isTarget_.end()) {
                    if (isTarget_.at(rowNo) != transactionId) {
                        return false;
                    }
                    isTarget_.at(rowNo) = -1;
                    return true;
                }
                return false;
            }

            bool isTarget(const short rowNo)
            {
                // std::lock_guard<std::mutex> lock{mt_};
                return isTarget_.find(rowNo) != isTarget_.end();
            }

            const std::string name() const
            {
                return name_;
            };

        private:
            const short id_;
            const std::string name_;
            // key:行番号, value: トランザクションによって変更対象になっていればそのトランザクションのid,それ以外は負の値
            std::map<short, short> isTarget_;
            // std::mutex mt_;
        };

        bool swap(const std::string connectionId, const std::vector<std::byte> &data)
        {
            DataStream temp{data};
            logger.stream().out() << "connectionId: " << connectionId << " const swap BEFOER";
            dataStreams_.at(connectionId).swap(temp);
            logger.stream().out() << "connectionId: " << connectionId << " const swap AFTER";
            return true;
        }

        bool swap(const std::string connectionId, std::vector<std::byte> &data)
        {
            logger.stream().out() << "connectionId: " << connectionId << " swap BEFOER";
            dataStreams_.at(connectionId).swap(data);
            logger.stream().out() << "connectionId: " << connectionId << " swap AFTER";
            return true;
        }

        const Connection createConnection()
        {
            std::string connectionId = UUID::create().str();
            Connection con{connectionId, *this};
            connectionList_.push_back(con);
            DataStream ds;
            dataStreams_.insert(std::make_pair(con.id(), ds));
            return con;
        }

        // 処理が完了したことをコネクションに通知する
        void notifyImpl(const std::string connectionId, const bool b)
        {
            logger.stream().out() << "------------------------------notifyImpl 1.";
            // 読み込みロック
            std::shared_lock<std::shared_mutex> shLock(sharedMt_);
            for (int i = 0; i < conditions_.size(); ++i) {
                const SessionCondition &sc = conditions_.at(i);
                if (std::get<0>(sc) == connectionId) {
                    // ここからは書き込み操作
                    logger.stream().out() << "------------------------------notifyImpl 2. " << b;
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{std::get<1>(conditions_.at(i))};
                        std::get<3>(conditions_.at(i)) = b;
                    } // Scoped Lock end
                    std::get<2>(conditions_.at(i)).notify_one();
                    logger.stream().out() << "------------------------------notifyImpl 3.";
                    break;
                }
            }
        }

        void addTransactionTarget(const std::string connectionId, const std::string tableName, const std::vector<std::byte> &bytes)
        {
            std::lock_guard<std::mutex> lock{mt_};
            short transactionId = -1;
            for (Transaction &t : transactionList_) {
                if (t.connectionId() == connectionId) {
                    transactionId = t.id();
                    t.addToCommit(tableName, bytes);
                }
            }
            for (Table &table : tableList_) {
                if (table.name() == tableName) {
                    table.setTarget(1, transactionId);
                }
            }
        }

        std::thread startChildThread(const std::string connectionId)
        {
            // クライアントとやり取りするスレッドを作成
            bool reuse = false;
            for (int i = 0; i < conditions_.size(); ++i) {
                const SessionCondition &sc = conditions_.at(i);
                if (std::get<0>(sc) == "") {
                    { // 書き込みロック
                        std::lock_guard<std::shared_mutex> lock(sharedMt_);
                        std::get<0>(conditions_.at(i)) = connectionId;
                    }
                    reuse = true;
                    break;
                }
            }
            if (!reuse) {
                throw std::runtime_error{"Database::startChildThread() : " + std::string{"number of sessions is upper limits."}};
            }

            int i = 0;
            for (i = 0; i < conditions_.size(); ++i) {
                const SessionCondition &sc = conditions_.at(i);
                if (std::get<0>(sc) == connectionId) {
                    logger.stream().out() << "------------------------------wait 1.";
                    break;
                }
            }
            if (i == conditions_.size()) {
                throw std::runtime_error{"Database::startChildThread() : " + std::string{"should not reach here."}};
            }

            logger.stream().out() << "------------------------------11.";
            std::thread t{[&condition = conditions_.at(i), this] {
                try {
                    logger.stream().out() << "------------------------------12.";
                    std::string id;
                    { // 読み込みロック
                        std::shared_lock<std::shared_mutex> lock(sharedMt_);
                        id = std::get<0>(condition);
                    }
                    while (true) {
                        if (threads_.shouldBeStopped(std::this_thread::get_id())) {
                            return;
                        }
                        { // Scoped Lock start
                            std::unique_lock<std::mutex> lock{std::get<1>(condition)};
                            logger.stream().out() << "------------------------------13.";
                            if (!std::get<3>(condition)) {
                                logger.stream().out() << "------------------------------14.";
                                std::get<2>(condition).wait(lock, [&refB = std::get<3>(condition)] {
                                    return refB;
                                });
                            }
                        } // Scoped Lock end
                        logger.stream().out() << "connection id: " << id << " is processing.";

                        // TODO: 要求を処理
                        std::vector<std::byte> data;
                        getData(id, data);

                        addTransactionTarget(id, "tableName", data);

                        std::string closeRequest = " TEST MESSAGE.";
                        for (const char c : closeRequest) {
                            data.push_back(static_cast<std::byte>(c));
                        }
                        setData(id, std::cref(data));
                        toNotify(id);
                    }
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(logger.stream().out() << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(logger.stream().out() << "unexpected error or SEH exception.";)
                }
            }};
            return std::move(t);
        }

        void startService()
        {
            try {
                for (SessionCondition &sc : conditions_) {
                    std::get<0>(sc) = "";
                    std::get<3>(sc) = false;
                }
                while (true) {
                    if (toBeStoped_.load()) {
                        logger.stream().out() << "toBeStoped_: " << toBeStoped_.load();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::string connectionId;
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{mt_};
                        if (!isRequiredConnection_) {
                            continue;
                        }
                        Connection con = createConnection();
                        connectionId = con.id();

                        isRequiredConnection_ = false;
                    } // Scoped Lock end
                    cond_.notify_all();

                    std::thread t;
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{mt_};
                        t = startChildThread(connectionId);
                    } // Scoped Lock end
                    threads_.addThread(std::move(t));
                    threads_.cleanUp();
                }
            }
            catch (std::exception &e) {
                logger.stream().out() << e.what();
            }
        }

        short transactionId_;
        short tableId_;
        std::vector<Connection> connectionList_;
        std::vector<Transaction> transactionList_;
        std::vector<Table> tableList_;
        // key: ConnectionのId, value: 送受信データ
        std::map<std::string, DataStream> dataStreams_;

        // コネクション作成要求がある場合にtrue
        bool isRequiredConnection_;
        // 上記bool用の条件変数
        std::condition_variable cond_;

        // データベースサービスのメインスレッド
        std::thread thread_;
        // サービスが開始していればtrue
        bool isStarted_;
        std::mutex mt_;

        // クライアントと子スレッドのセッションの状態
        std::array<SessionCondition, 50> conditions_;
        ThreadsMap threads_;

        // SessionConditionのConnectionのIdを設定する際に用いるミューテックス
        std::shared_mutex sharedMt_;

        // データベースを終了すべき場合にtrue
        std::atomic_bool toBeStoped_;
    };

    // Connection
    inline bool Connection::beginTransaction()
    {
        return refDb_.setTransaction(id_);
    }

    inline bool Connection::send(const std::vector<std::byte> &data)
    {
        // クライアントからの情報送信はDB内部の送受信用バッファに情報を入れる
        return refDb_.setData(id_, data);
    }

    inline bool Connection::receive(std::vector<std::byte> &out)
    {
        // クライアントはDB内部の送受信用バッファから情報を取り出すことで受信する
        return refDb_.getData(id_, out);
    }

    inline void Connection::request()
    {
        refDb_.toNotify(id_, true);
    }

    inline void Connection::wait()
    {
        refDb_.wait(id_);
    }

    inline bool Connection::isClosed()
    {
        return refDb_.isClosed(id_);
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