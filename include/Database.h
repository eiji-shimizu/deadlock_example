#ifndef DEADLOCK_EXAMPLE_DATABASE_INCLUDED
#define DEADLOCK_EXAMPLE_DATABASE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"
#include "ThreadsMap.h"
#include "UUID.h"
#include "Utils.h"

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
            // DB_LOG << " ~Connection()";
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

        // 引数のデータを送信する
        bool send(const std::vector<std::byte> &data);
        // 引数にデータを受信する
        bool receive(std::vector<std::byte> &out);
        // 送信したデータの処理を依頼する
        bool request();
        // 処理結果を待つ
        int wait();
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
                DB_LOG << " ~Database()";
                LOG << "~Database()";
                toBeStoped_.store(true);
                for (int i = 0; i < conditions_.size(); ++i) {
                    DB_LOG << "notify_all() BEFORE";
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{std::get<1>(conditions_[i])};
                        std::get<3>(conditions_[i]) = true;
                    } // Scoped Lock end
                    std::get<2>(conditions_[i]).notify_all();
                    DB_LOG << "notify_all() AFTER";
                }
                if (thread_.joinable()) {
                    LOG << " thread_.join() BEFORE";
                    DB_LOG << " thread_.join() BEFORE";
                    thread_.join();
                }

                LOG << " thread_.join() AFTER";
                DB_LOG << " thread_.join() AFTER";
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
                    threads_.setFinishedFlagAll();
                    LOG << "Database service is stop.";
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(DB_LOG << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(DB_LOG << "unexpected error or SEH exception.";)
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
            DB_LOG << "Database::getConnection()";
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
        bool getData(const std::string connectionId, std::vector<std::byte> &out)
        {
            std::lock_guard<std::mutex> lock{mt_};
            DB_LOG << "connectionId: " << connectionId << " getData";
            DataStream ds;
            swap(connectionId, ds);
            ds.swap(out);
            return true;
        }

        bool setData(const std::string connectionId, const std::vector<std::byte> &data)
        {
            std::lock_guard<std::mutex> lock{mt_};
            DB_LOG << "connectionId: " << connectionId << " setData";
            return swap(connectionId, data);
        }

        // 引数のコネクションIDに対応するコネクションに処理完了を通知する
        // 第2引数がtrueの場合はコネクション側からの処理依頼になる
        // 戻り値: 通知が設定されればtrue
        bool toNotify(const std::string connectionId, const bool b = false)
        {
            return notifyImpl(connectionId, b);
        }

        // 引数のコネクションIDに対応する処理完了を待つ関数
        // クライアント側(コネクション)から呼び出される
        // 戻り値
        // 0: 成功
        // -1: まだ対応する処理のためのセッションが開始していない
        // -2: その他のエラー
        int wait(const std::string connectionId)
        {
            int i = 0;
            { // 読み込みロック
                std::shared_lock<std::shared_mutex> lock(sharedMt_);
                for (i = 0; i < conditions_.size(); ++i) {
                    const SessionCondition &sc = conditions_.at(i);
                    if (std::get<0>(sc) == connectionId) {
                        DB_LOG << "------------------------------wait 1.";
                        LOG << "wait " << connectionId;
                        break;
                    }
                }
            }
            if (i == conditions_.size()) {
                LOG << "-------------------------bugbug";
                LOG << "Session for connection id:" << connectionId << " is not found.";
                return -1;
            }
            DB_LOG << "------------------------------wait 2.";
            std::unique_lock<std::mutex> lock{std::get<1>(conditions_.at(i))};
            if (std::get<3>(conditions_.at(i))) {
                DB_LOG << "------------------------------wait 3.";
                std::get<2>(conditions_.at(i)).wait(lock, [&refB = std::get<3>(conditions_.at(i))] {
                    return refB == false;
                });
            }
            DB_LOG << "------------------------------wait 4.";
            return 0;
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
                DB_LOG << " ~Transaction()";
                for (const auto &e : toCommit_) {
                    DB_LOG << e.first;
                    std::ostringstream oss{""};
                    for (const auto &v : e.second) {
                        for (const std::byte b : v) {
                            oss << static_cast<char>(b);
                        }
                    }
                    DB_LOG << oss.str();
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
                DB_LOG << " ~Table()";
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
            DB_LOG << "connectionId: " << connectionId << " const swap BEFOER";
            dataStreams_.at(connectionId).swap(temp);
            DB_LOG << "connectionId: " << connectionId << " const swap AFTER";
            return true;
        }

        bool swap(const std::string connectionId, std::vector<std::byte> &data)
        {
            DB_LOG << "connectionId: " << connectionId << " swap BEFOER";
            dataStreams_.at(connectionId).swap(data);
            DB_LOG << "connectionId: " << connectionId << " swap AFTER";
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
        bool notifyImpl(const std::string connectionId, const bool b)
        {
            // 読み込みロック
            std::shared_lock<std::shared_mutex> shLock(sharedMt_);
            for (int i = 0; i < conditions_.size(); ++i) {
                const SessionCondition &sc = conditions_.at(i);
                if (std::get<0>(sc) == connectionId) {
                    // ここからは書き込み操作
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{std::get<1>(conditions_.at(i))};
                        std::get<3>(conditions_.at(i)) = b;
                    } // Scoped Lock end
                    std::get<2>(conditions_.at(i)).notify_one();
                    LOG << connectionId << " notifyImpl: OK" << b;
                    return true;
                }
            }
            LOG << connectionId << " notifyImpl: NG" << b;
            return false;
        }

        bool addTransaction(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            Transaction t{transactionId_++, connectionId};
            transactionList_.push_back(t);
            return true;
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

        bool isTransactionExists(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            for (const Transaction &t : transactionList_) {
                if (t.connectionId() == connectionId) {
                    return true;
                }
            }
            return false;
        }

        void toBytesDataFromString(const std::string &s, std::vector<std::byte> &out)
        {
            for (const char c : s) {
                out.push_back(static_cast<std::byte>(c));
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
                    DB_LOG << "------------------------------wait 1.";
                    break;
                }
            }
            if (i == conditions_.size()) {
                throw std::runtime_error{"Database::startChildThread() : " + std::string{"should not reach here."}};
            }

            DB_LOG << "------------------------------11.";
            std::thread t{[&condition = conditions_.at(i), this] {
                try {
                    DB_LOG << "------------------------------12.";
                    std::string id;
                    { // 読み込みロック
                        std::shared_lock<std::shared_mutex> lock(sharedMt_);
                        id = std::get<0>(condition);
                    }
                    while (true) {
                        // if (threads_.shouldBeStopped(std::this_thread::get_id())) {
                        //     // TODO: 終了処理
                        //     LOG << "child thread return.";
                        //     return;
                        // }
                        if (toBeStoped_.load()) {
                            LOG << "child thread return.";
                            return;
                        }
                        { // Scoped Lock start
                            std::unique_lock<std::mutex> lock{std::get<1>(condition)};
                            DB_LOG << "------------------------------13.";
                            if (!std::get<3>(condition)) {
                                DB_LOG << "------------------------------14.";
                                std::get<2>(condition).wait(lock, [&refB = std::get<3>(condition)] {
                                    return refB;
                                });
                            }
                        } // Scoped Lock end
                        if (toBeStoped_.load()) {
                            LOG << "child thread return.";
                            return;
                        }
                        LOG << "connection id: " << id << " is processing.";

                        // TODO: 要求を処理
                        std::vector<std::byte> data;
                        std::vector<std::byte> response;
                        getData(id, data);
                        // 最初の要求はトランザクションの開始であること
                        // 続く要求はこのコネクションのユーザーに許可された処理であること
                        // トランザクションがない状態で他の要求が来た場合はエラー
                        // またトランザクションがある状態でトランザクションの要求が来た場合もエラーとする
                        std::ostringstream oss{""};
                        int i = 0;
                        for (i = 0; i < data.size() && i < 7; ++i) {
                            oss << static_cast<char>(data[i]);
                        }
                        if (toLower(oss.str()) != "please:") {
                            toBytesDataFromString("parse error.", response);
                            setData(id, std::cref(response));
                            toNotify(id);
                            continue;
                        }
                        oss.str("");
                        for (; i < data.size() && i < 7 + 11; ++i) {
                            oss << static_cast<char>(data[i]);
                        }
                        if (isTransactionExists(id) && toLower(oss.str()) == "transaction") {
                            toBytesDataFromString("transaction is already exists.", response);
                            setData(id, std::cref(response));
                            toNotify(id);
                            continue;
                        }
                        if (!isTransactionExists(id) && toLower(oss.str()) != "transaction") {
                            toBytesDataFromString("cannot find transaction.", response);
                            setData(id, std::cref(response));
                            toNotify(id);
                            continue;
                        }
                        if (!isTransactionExists(id) && toLower(oss.str()) == "transaction") {
                            if (addTransaction(id)) {
                                toBytesDataFromString("transaction start is succeed.", response);
                                setData(id, std::cref(response));
                                toNotify(id);
                            }
                            else {
                                toBytesDataFromString("transaction start is failed.", response);
                                setData(id, std::cref(response));
                                toNotify(id);
                            }
                            continue;
                        }

                        addTransactionTarget(id, "tableName", data);
                        LOG << "LOOP END: " << id;
                        data.push_back(static_cast<std::byte>(' '));
                        data.push_back(static_cast<std::byte>('O'));
                        data.push_back(static_cast<std::byte>('K'));
                        setData(id, std::cref(data));
                        toNotify(id);
                    }
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(DB_LOG << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(DB_LOG << "unexpected error or SEH exception.";)
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
                        DB_LOG << "toBeStoped_: " << toBeStoped_.load();
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
                DB_LOG << e.what();
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

    inline bool Connection::request()
    {
        return refDb_.toNotify(id_, true);
    }

    inline int Connection::wait()
    {
        return refDb_.wait(id_);
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