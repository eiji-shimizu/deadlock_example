#ifndef DEADLOCK_EXAMPLE_DATABASE_INCLUDED
#define DEADLOCK_EXAMPLE_DATABASE_INCLUDED

#include "General.h"

#include "Common.h"
#include "Datafile.h"
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
            DEBUG_LOG << " ~Connection()";
        }

        Connection &operator=(const Connection &rhs)
        {
            if (this == &rhs) {
                return *this;
            }
            id_ = rhs.id_;
            pDb_ = rhs.pDb_;
            isInUse_ = rhs.isInUse_;
            return *this;
        }

        // 引数のデータを送信する
        bool send(const std::vector<std::byte> &data);
        // 引数にデータを受信する
        bool receive(std::vector<std::byte> &out);
        // 送信したデータの処理を依頼する
        bool request();
        // 処理結果を待つ
        int wait();
        // コネクションをクローズする
        bool close();
        // このコネクションがクローズしていればtrue
        bool isClosed();

        const std::string id() const { return id_; }

    private:
        Connection(const std::string id, Database &refDb)
            : id_{id},
              pDb_{&refDb},
              isInUse_{false}
        {
        }

        std::string id_;
        Database *pDb_;
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
              toBeStopped_{false}
        {
        }
        ~Database()
        {
            CATCH_ALL_EXCEPTIONS({
                DB_LOG << "~Database()";
                toBeStopped_.store(true);
                DB_LOG << "~Database(): child threads notify_all() BEFORE";
                for (int i = 0; i < conditions_.size(); ++i) {
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{std::get<1>(conditions_[i])};
                        std::get<3>(conditions_[i]) = true;
                    } // Scoped Lock end
                    std::get<2>(conditions_[i]).notify_all();
                }
                DB_LOG << "~Database(): child threads notify_all() AFTER";
                if (thread_.joinable()) {
                    DB_LOG << "~Database(): thread_.join() BEFORE";
                    thread_.join();
                }
                DB_LOG << "~Database(): thread_.join() AFTER";
            })
        }

        void start()
        {
            std::lock_guard<std::mutex> lock{mt_};
            if (isStarted_) {
                return;
            }
            // テーブルの情報を読み込む
            std::map<std::string, std::map<std::string, std::string>> tables{PapierMache::readConfiguration("./database/tables.ini")};
            for (const auto &e : tables) {
                datafiles_.emplace_back(e.first, e.second);
            }
            std::vector<std::byte> out;
            for (Datafile &f : datafiles_) {
                if (f.tableName() == "user") {
                    toBytesDataFromString("testtest bug iranai", out);
                    f.insert(5, out);
                    f.commit(5);
                }
            }

            thread_ = std::thread{[this] {
                try {
                    startService();
                    DB_LOG << "Database::start(): Database service is stop.";
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(LOG << "Database::start(): " << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(LOG << "Database::start(): "
                                             << "unexpected error or SEH exception.";)
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
            DEBUG_LOG << "connectionId: " << connectionId << " getData";
            DataStream ds;
            swap(connectionId, ds);
            ds.swap(out);
            return true;
        }

        bool setData(const std::string connectionId, const std::vector<std::byte> &data)
        {
            std::lock_guard<std::mutex> lock{mt_};
            DEBUG_LOG << "connectionId: " << connectionId << " setData";
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
                        DEBUG_LOG << connectionId << " ------------------------------wait 1.";
                        break;
                    }
                }
            }
            if (i == conditions_.size()) {
                DEBUG_LOG << "Session for connection id:" << connectionId << " is not found.";
                return -1;
            }
            DEBUG_LOG << connectionId << " ------------------------------wait 2.";
            std::unique_lock<std::mutex> lock{std::get<1>(conditions_.at(i))};
            if (std::get<3>(conditions_.at(i))) {
                DEBUG_LOG << connectionId << " ------------------------------wait 3.";
                std::get<2>(conditions_.at(i)).wait(lock, [&refB = std::get<3>(conditions_.at(i))] {
                    return refB == false;
                });
            }
            DEBUG_LOG << connectionId << " ------------------------------wait 4.";
            return 0;
        }

        bool close(const std::string connectionId)
        {
            { // Scoped Lock start
                std::lock_guard<std::mutex> lock{mt_};
                auto result = std::remove_if(connectionList_.begin(), connectionList_.end(),
                                             [connectionId](Connection c) { return c.id() == connectionId; });

                if (result == connectionList_.end()) {
                    LOG << "close1: " << connectionId;
                    return false;
                }
                connectionList_.erase(result, connectionList_.end());
                dataStreams_.erase(connectionId);
            } // Scoped Lock end

            // このコネクションを処理しているスレッドに通知する
            int i = 0;
            { // Scoped Lock start
                // 書き込みロック
                std::lock_guard<std::shared_mutex> wl(sharedMt_);
                for (; i < conditions_.size(); ++i) {
                    if (std::get<0>(conditions_.at(i)) == connectionId) {
                        std::get<0>(conditions_.at(i)) = "";
                        break;
                    }
                }
            } // Scoped Lock end
            if (i == conditions_.size()) {
                LOG << "close2: " << connectionId;
                return false;
            }
            { // Scoped Lock start
                std::lock_guard<std::mutex> lock{std::get<1>(conditions_.at(i))};
                std::get<3>(conditions_.at(i)) = true;
            } // Scoped Lock end
            std::get<2>(conditions_.at(i)).notify_one();
            LOG << "close3: " << connectionId;
            return true;
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
                DB_LOG << " ~Transaction() BEFORE";
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
                DB_LOG << " ~Transaction() AFTER";
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
            DEBUG_LOG << "connectionId: " << connectionId << " const swap BEFOER";
            dataStreams_.at(connectionId).swap(temp);
            DEBUG_LOG << "connectionId: " << connectionId << " const swap AFTER";
            return true;
        }

        bool swap(const std::string connectionId, std::vector<std::byte> &data)
        {
            DEBUG_LOG << "connectionId: " << connectionId << " swap BEFOER";
            dataStreams_.at(connectionId).swap(data);
            DEBUG_LOG << "connectionId: " << connectionId << " swap AFTER";
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
                    DB_LOG << connectionId << " notifyImpl: OK" << b;
                    return true;
                }
            }
            DB_LOG << connectionId << " notifyImpl: NG" << b;
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
                throw std::runtime_error{"Database::startChildThread(): " + std::string{"number of sessions is upper limits."}};
            }

            int i = 0;
            for (i = 0; i < conditions_.size(); ++i) {
                const SessionCondition &sc = conditions_.at(i);
                if (std::get<0>(sc) == connectionId) {
                    break;
                }
            }
            if (i == conditions_.size()) {
                throw std::runtime_error{"Database::startChildThread(): " + std::string{"cannot find connection. should not reach here."}};
            }

            std::thread t{[&condition = conditions_.at(i), this] {
                try {
                    std::string id;
                    { // 読み込みロック
                        std::shared_lock<std::shared_mutex> lock(sharedMt_);
                        id = std::get<0>(condition);
                    }
                    while (true) {
                        if (toBeStopped_.load() || isClosed(id)) {
                            DB_LOG << "Database::startChildThread(): child thread return.";
                            return;
                        }
                        { // Scoped Lock start
                            std::unique_lock<std::mutex> lock{std::get<1>(condition)};
                            if (!std::get<3>(condition)) {
                                DB_LOG << "Database::startChildThread(): wait for request from client.";
                                std::get<2>(condition).wait(lock, [&refB = std::get<3>(condition)] {
                                    return refB;
                                });
                            }
                        } // Scoped Lock end
                        if (toBeStopped_.load() || isClosed(id)) {
                            DB_LOG << "Database::startChildThread(): child thread return.";
                            return;
                        }
                        DB_LOG << "Database::startChildThread(): connection id: " << id << " is processing.";

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
                        data.push_back(static_cast<std::byte>(' '));
                        data.push_back(static_cast<std::byte>('O'));
                        data.push_back(static_cast<std::byte>('K'));
                        setData(id, std::cref(data));
                        toNotify(id);
                        DB_LOG << "Database::startChildThread(): notify to id: " << id;
                    }
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(LOG << "Database::startChildThread: " << e.what();)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(LOG << "Database::startChildThread: "
                                             << "unexpected error or SEH exception.";)
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
                ThreadsMap threadsMap_;
                while (true) {
                    if (toBeStopped_.load()) {
                        DB_LOG << "Database::startService(): toBeStopped_: " << toBeStopped_.load();
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
                    threadsMap_.addThread(std::move(t));
                    threadsMap_.cleanUp();
                }
            }
            catch (std::exception &e) {
                LOG << "Database::startService(): " << e.what();
            }
        }

        short transactionId_;
        short tableId_;
        std::vector<Datafile> datafiles_;
        std::vector<Connection> connectionList_;
        std::vector<Transaction> transactionList_;
        std::vector<Table> tableList_;
        // key: ConnectionのId, value: 送受信データ
        std::map<std::string, DataStream> dataStreams_;

        // クライアントと子スレッドのセッションの状態
        std::array<SessionCondition, 50> conditions_;
        // SessionConditionのConnectionのIdを設定する際に用いるミューテックス
        std::shared_mutex sharedMt_;

        // コネクション作成要求がある場合にtrue
        bool isRequiredConnection_;
        // 上記bool用の条件変数
        std::condition_variable cond_;

        // データベースを終了すべき場合にtrue
        std::atomic_bool toBeStopped_;

        // サービスが開始していればtrue
        bool isStarted_;
        std::mutex mt_;
        // データベースサービスのメインスレッド
        std::thread thread_;
    };

    // Connection
    inline bool Connection::send(const std::vector<std::byte> &data)
    {
        // クライアントからの情報送信はDB内部の送受信用バッファに情報を入れる
        return pDb_->setData(id_, data);
    }

    inline bool Connection::receive(std::vector<std::byte> &out)
    {
        // クライアントはDB内部の送受信用バッファから情報を取り出すことで受信する
        return pDb_->getData(id_, out);
    }

    inline bool Connection::request()
    {
        return pDb_->toNotify(id_, true);
    }

    inline int Connection::wait()
    {
        return pDb_->wait(id_);
    }

    inline bool Connection::close()
    {
        return pDb_->close(id_);
    }

    inline bool Connection::isClosed()
    {
        return pDb_->isClosed(id_);
    }

    // データベースドライバー
    class Driver {
    public:
        struct Result {
            const bool isSucceed;
            std::map<std::string, std::string> rows;
            const std::string message;

            Result(const bool b, std::map<std::string, std::string> m, const std::string s)
                : isSucceed{b}, rows{m}, message{s}
            {
            }
        };

        Driver(const Connection con)
            : con_{con}
        {
        }

        // 引数のクエリをコネクションを通じてデータベースに送る
        // ユーザーの新規作成:
        // PLEASE:NEWUSER user="userName",password="password"
        // このコネクションにおけるトランザクションを開始する:
        // PLEASE:TRANSACTION
        // テーブルへのselect ()内が照会する列
        // PLEASE:SELECT "tableName" (key1="value1",key2="value2"...)
        // テーブルへのinsert ()内が登録内容:
        // PLEASE:INSERT "tableName" (key1="value1",key2="value2"...)
        // テーブルへのupdate 前半の()内が更新内容 後半の()内が更新する列
        // PLEASE:UPDATE "tableName" (key1="value1",key2="value2"...) (key1="value1",key2="value2"...)
        // テーブルへのdelete ()内が削除する列
        // PLEASE:DELETE "tableName" (key1="value1",key2="value2"...)
        // テーブルへのselect for update ()内がロックする列
        // PLEASE:SELECT_FOR_UPDATE "tableName" (key1="value1",key2="value2"...)
        // トランザクションをコミットする
        // PLEASE:COMMIT
        // トランザクションをロールバックする
        // PLEASE:ROLLBACK
        Result sendQuery(std::string query)
        {
            std::string error = "";
            std::map<std::string, std::string> rows;
            try {
                std::vector<std::byte> data;
                for (const char c : query) {
                    data.push_back(static_cast<std::byte>(c));
                }
                // クエリ送信
                error = "send failed";
                if (!con_.send(data)) {
                    return Result{false, rows, error};
                }
                // データベースに送信したクエリの処理のリクエストを送る
                // リトライ含め3回
                error = "request failed";
                bool requestResult = con_.request();
                if (!requestResult) {
                    requestResult = con_.request();
                    if (!requestResult) {
                        requestResult = con_.request();
                    }
                }
                if (!requestResult) {
                    return Result{false, rows, error};
                }
                // データベースの処理を待つ
                error = "wait failed";
                int waitResult = con_.wait();
                if (waitResult == -1) {
                    // このエラー時のみリトライする
                    waitResult = con_.wait();
                }
                // エラーがあった場合
                if (waitResult != 0) {
                    return Result{false, rows, error};
                }
                // 結果受信
                error = "receive failed";
                if (!con_.receive(data)) {
                    return Result{false, rows, error};
                }
                error = "";
                std::ostringstream oss{""};
                for (const std::byte b : data) {
                    oss << static_cast<char>(b);
                }
                // TODO: 受信データをパースする
                return Result{true, rows, oss.str()};
            }
            catch (std::exception &e) {
                if (error == "") {
                    throw;
                }
                return Result{false, rows, error + " : " + e.what()};
            }
        }

    private:
        Connection con_;
    };

} // namespace PapierMache::DbStuff

#endif // DEADLOCK_EXAMPLE_DATABASE_INCLUDED