#ifndef DEADLOCK_EXAMPLE_DATABASE_INCLUDED
#define DEADLOCK_EXAMPLE_DATABASE_INCLUDED

#include "General.h"

#include "BCryptHash.h"
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

    // valueTの最大値 - 1までのidを生成して返すジェネレータ
    template <typename valueT>
    class IdGenerator {
    public:
        IdGenerator(valueT initialValue, bool reuseMode = false)
            : initialValue_{initialValue},
              v_{initialValue},
              reuseMode_{reuseMode} {}

        // コピー禁止
        IdGenerator(const IdGenerator &) = delete;
        IdGenerator &operator=(const IdGenerator &) = delete;
        // ムーブ禁止
        IdGenerator(IdGenerator &&) = delete;
        IdGenerator &operator=(IdGenerator &&) = delete;

        bool isLimits(const valueT v) const
        {
#pragma push_macro("max")
#undef max
            if (std::numeric_limits<valueT>::max() == v) {
#pragma pop_macro("max")
                return true;
            }
            return false;
        }

        valueT getId()
        {
            std::lock_guard<std::mutex> lock{mt_};
            if (!reuseMode_) {
                if (isLimits(v_)) {
                    throw std::runtime_error{"all id value is used."};
                }
                return v_++;
            }
            if (isLimits(v_)) {
                v_ = initialValue_;
            }
            for (; !isLimits(v_); ++v_) {
                if (used_.find(v_) == used_.end()) {
                    // これは現在利用中ではない値なのでOK
                    valueT save = v_;
                    used_.insert(std::make_pair(v_++, ' '));
                    return save;
                }
            }
            throw std::runtime_error{"all id value is used."};
        }

        void release(valueT id)
        {
            std::lock_guard<std::mutex> lock{mt_};
            used_.erase(id);
        }

    private:
        valueT initialValue_;
        valueT v_;
        // 使用した値を再利用する場合はtrue
        const bool reuseMode_;
        std::map<valueT, char> used_;
        std::mutex mt_;
    };

    class Database;

    class Connection {
        friend Database;

    public:
        ~Connection()
        {
            DEBUG_LOG << " ~Connection()" << FILE_INFO;
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
        // コネクションが開始したトランザクションを停止する
        bool terminate();
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

    class DatabaseException : public std::runtime_error {
    public:
        DatabaseException(const char *message)
            : std::runtime_error(message)
        {
        }

        DatabaseException(const std::string message)
            : std::runtime_error(message)
        {
        }
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
            : tIdGenerator_{0, true},
              isRequiredConnection_{false},
              isStarted_{false},
              toBeStopped_{false}
        {
        }
        ~Database()
        {
            CATCH_ALL_EXCEPTIONS({
                DB_LOG << "~Database()" << FILE_INFO;
                toBeStopped_.store(true);
                DB_LOG << "child threads notify_all() BEFORE" << FILE_INFO;
                for (int i = 0; i < conditions_.size(); ++i) {
                    { // Scoped Lock start
                        std::lock_guard<std::mutex> lock{std::get<1>(conditions_[i])};
                        std::get<3>(conditions_[i]) = true;
                    } // Scoped Lock end
                    std::get<2>(conditions_[i]).notify_all();
                }
                DB_LOG << "child threads notify_all() AFTER" << FILE_INFO;
                if (thread_.joinable()) {
                    DB_LOG << "thread_.join() BEFORE" << FILE_INFO;
                    thread_.join();
                }
                DB_LOG << "thread_.join() AFTER" << FILE_INFO;
                // 全てのコネクションをクローズする
                std::vector<std::string> connectionIds;
                for (Connection &c : connectionList_) {
                    connectionIds.push_back(c.id());
                }
                for (const std::string &cId : connectionIds) {
                    close(cId);
                }
                DB_LOG << "all connection closed." << FILE_INFO;
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
                    Connection con = createConnection();
                    Transaction t{tIdGenerator_.getId(), con.id()};
                    transactionList_.push_back(t);
                    auto users = f.select(t.id(), std::vector<std::byte>{});
                    // ユーザーテーブルがゼロ件であれば次のユーザーを追加する
                    if (users.size() == 0) {
                        std::string hash;
                        toBCryptHash("adminpass", hash);
                        toBytesDataFromString("USER_NAME=\"admin\"," + std::string("PASSWORD=") + hash + "," + "DATETIME=\"30827:12:31:23:59:59:999\"", out);
                        f.insert(t.id(), out);
                        hash = "";
                        out.clear();
                        toBCryptHash("user1pass", hash);
                        toBytesDataFromString("USER_NAME=\"user1\"," + std::string("PASSWORD=") + hash + "," + "DATETIME=\"30827:12:31:23:59:59:999\"", out);
                        f.insert(t.id(), out);
                        hash = "";
                    }
                    f.commit(t.id());

                    out.clear();
                    Transaction t1{tIdGenerator_.getId(), con.id()};
                    transactionList_.push_back(t1);
                    users = f.select(t1.id(), std::vector<std::byte>{});
                    DB_LOG << "users.size(): " << users.size();
                    for (const auto &e : users) {
                        User u{};
                        std::ostringstream oss{""};
                        for (const std::byte b : e.at("user_name")) {
                            if (static_cast<unsigned char>(b) != 0) {
                                oss << static_cast<char>(b);
                            }
                        }
                        u.setUserName(oss.str());
                        oss.str("");
                        for (const std::byte b : e.at("password")) {
                            oss << static_cast<char>(b);
                        }
                        u.setPassword(oss.str());
                        users_.push_back(u);
                    }
                    f.commit(t1.id());

                    auto result = std::remove_if(connectionList_.begin(), connectionList_.end(),
                                                 [connectionId = con.id()](Connection c) { return c.id() == connectionId; });
                    if (result == connectionList_.end()) {
                        throw std::runtime_error{"cannot find connection. should not reach here." + FILE_INFO};
                    }
                    connectionList_.erase(result, connectionList_.end());
                    dataStreams_.erase(con.id());
                    transactionList_.clear();
                }
            }

            thread_ = std::thread{[this] {
                try {
                    startService();
                    DB_LOG << "Database service is stop." << FILE_INFO;
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(LOG << e.what() << FILE_INFO;)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(LOG << "unexpected error or SEH exception." << FILE_INFO;)
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
            DB_LOG << "Database::getConnection()" << FILE_INFO;
            if (isRequiredConnection_) {
                throw std::runtime_error("concurrency problems occurs." + FILE_INFO);
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
            throw std::runtime_error("cannot find connection." + FILE_INFO);
        }

        // コネクションとのインターフェース関数 ここから
        bool getData(const std::string connectionId, std::vector<std::byte> &out)
        {
            std::lock_guard<std::mutex> lock{mt_};
            DEBUG_LOG << "connectionId: " << connectionId << " getData" << FILE_INFO;
            DataStream ds;
            swap(connectionId, ds);
            ds.swap(out);
            return true;
        }

        bool setData(const std::string connectionId, const std::vector<std::byte> &data)
        {
            std::lock_guard<std::mutex> lock{mt_};
            DEBUG_LOG << "connectionId: " << connectionId << " setData" << FILE_INFO;
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
                        DEBUG_LOG << connectionId << " ------------------------------wait 1." << FILE_INFO;
                        break;
                    }
                }
            }
            if (i == conditions_.size()) {
                DEBUG_LOG << "Session for connection id:" << connectionId << " is not found." << FILE_INFO;
                return -1;
            }
            DEBUG_LOG << connectionId << " ------------------------------wait 2." << FILE_INFO;
            std::unique_lock<std::mutex> lock{std::get<1>(conditions_.at(i))};
            if (std::get<3>(conditions_.at(i))) {
                DEBUG_LOG << connectionId << " ------------------------------wait 3." << FILE_INFO;
                std::get<2>(conditions_.at(i)).wait(lock, [&refB = std::get<3>(conditions_.at(i))] {
                    return refB == false;
                });
            }
            DEBUG_LOG << connectionId << " ------------------------------wait 4." << FILE_INFO;
            return 0;
        }

        bool terminate(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            return terminateImpl(connectionId);
        }

        bool close(const std::string connectionId)
        {
            { // Scoped Lock start
                std::lock_guard<std::mutex> lock{mt_};
                terminateImpl(connectionId);
                auto result = std::remove_if(connectionList_.begin(), connectionList_.end(),
                                             [connectionId](Connection c) { return c.id() == connectionId; });

                if (result == connectionList_.end()) {
                    return false;
                }
                connectionList_.erase(result, connectionList_.end());
                dataStreams_.erase(connectionId);
                connectedUsers_.erase(connectionId);
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
                return false;
            }
            { // Scoped Lock start
                std::lock_guard<std::mutex> lock{std::get<1>(conditions_.at(i))};
                std::get<3>(conditions_.at(i)) = true;
            } // Scoped Lock end
            std::get<2>(conditions_.at(i)).notify_one();
            LOG << "connection id: " << connectionId << " is closed." << FILE_INFO;
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
                DB_LOG << " ~Transaction()" << FILE_INFO;
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
            short id_;
            std::string connectionId_;
        };

        class User {
        public:
            const std::string userName() const { return userName_; }
            const std::string password() const { return password_; }
            void setUserName(const std::string userName) { userName_ = userName; }
            void setPassword(const std::string password) { password_ = password; }

        private:
            std::string userName_;
            std::string password_;
        };

        class Result {
        public:
            Result(const char flag,
                   const std::string tableName,
                   const std::string tableInfo,
                   const std::vector<std::map<std::string, std::vector<std::byte>>> data)
                : flag_{flag},
                  tableName_{tableName},
                  tableInfo_{tableInfo},
                  data_{data}
            {
            }

            Result(const char flag,
                   const std::string tableName,
                   const std::string tableInfo,
                   const std::string message)
                : flag_{flag},
                  tableName_{tableName},
                  tableInfo_{tableInfo}
            {
                std::vector<std::byte> v;
                for (const char c : message) {
                    v.push_back(static_cast<std::byte>(c));
                }
                std::map<std::string, std::vector<std::byte>> m;
                m.insert(std::make_pair("message", v));
                data_.push_back(m);
            }

            std::vector<std::byte> toBytes() const
            {
                std::vector<std::byte> bytes;
                bytes.push_back(static_cast<std::byte>(flag_));
                bytes.push_back(static_cast<std::byte>(' '));
                for (const char c : tableName_) {
                    bytes.push_back(static_cast<std::byte>(c));
                }
                bytes.push_back(static_cast<std::byte>(' '));
                for (const char c : tableInfo_) {
                    bytes.push_back(static_cast<std::byte>(c));
                }
                bytes.push_back(static_cast<std::byte>(' '));
                for (const auto &e : data_) {
                    for (const auto &p : e) {
                        for (const char c : p.first) {
                            bytes.push_back(static_cast<std::byte>(c));
                        }
                        bytes.push_back(static_cast<std::byte>('='));
                        for (const std::byte b : p.second) {
                            bytes.push_back(b);
                        }
                    }
                }
                return bytes;
            }

        private:
            // 成功であれば0
            char flag_;
            std::string tableName_;
            // 列名=データサイズ,列名=データサイズ,列名=データサイズ...
            std::string tableInfo_;
            std::vector<std::map<std::string, std::vector<std::byte>>> data_;
        };

        bool swap(const std::string connectionId, const std::vector<std::byte> &data)
        {
            DataStream temp{data};
            DEBUG_LOG << "connectionId: " << connectionId << " const swap BEFOER" << FILE_INFO;
            dataStreams_.at(connectionId).swap(temp);
            DEBUG_LOG << "connectionId: " << connectionId << " const swap AFTER" << FILE_INFO;
            return true;
        }

        bool swap(const std::string connectionId, std::vector<std::byte> &data)
        {
            DEBUG_LOG << "connectionId: " << connectionId << " swap BEFOER" << FILE_INFO;
            dataStreams_.at(connectionId).swap(data);
            DEBUG_LOG << "connectionId: " << connectionId << " swap AFTER" << FILE_INFO;
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
                    DB_LOG << connectionId << " notifyImpl: OK" << b << FILE_INFO;
                    return true;
                }
            }
            DB_LOG << connectionId << " notifyImpl: NG" << b << FILE_INFO;
            return false;
        }

        bool terminateImpl(const std::string connectionId)
        {
            TRANSACTION_ID id = -1;
            for (const Transaction &t : transactionList_) {
                if (t.connectionId() == connectionId) {
                    id = t.id();
                }
            }
            for (int i = 0; i < datafiles_.size(); ++i) {
                if (id != -1) {
                    datafiles_[i].setToTerminate(id);
                }
            }
            auto it = std::remove_if(transactionList_.begin(), transactionList_.end(),
                                     [tId = id](Transaction &t) { return t.id() == tId; });
            transactionList_.erase(it, transactionList_.end());
            tIdGenerator_.release(id);
            return true;
        }

        bool addTransaction(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            Transaction t{tIdGenerator_.getId(), connectionId};
            transactionList_.push_back(t);
            return true;
        }

        TRANSACTION_ID getTransactionId(const std::string connectionId)
        {
            std::lock_guard<std::mutex> lock{mt_};
            for (const Transaction &t : transactionList_) {
                if (t.connectionId() == connectionId) {
                    return t.id();
                }
            }
            throw std::runtime_error{"transaction is not found. connection id: " + connectionId + FILE_INFO};
        }

        Datafile &getDatafile(const std::string tableName)
        {
            std::lock_guard<std::mutex> lock{mt_};
            auto it = std::find_if(datafiles_.begin(), datafiles_.end(),
                                   [tableName](const Datafile &ref) { return ref.tableName() == tableName; });
            return *it;
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

        void commitTransaction(const TRANSACTION_ID id)
        {
            for (int i = 0; i < datafiles_.size(); ++i) {
                std::lock_guard<std::mutex> lock{mt_};
                datafiles_[i].commit(id);
            }
            std::lock_guard<std::mutex> lock{mt_};
            auto it = std::remove_if(transactionList_.begin(), transactionList_.end(),
                                     [tId = id](Transaction &t) { return t.id() == tId; });
            transactionList_.erase(it, transactionList_.end());
            tIdGenerator_.release(id);
        }

        void rollbackTransaction(const TRANSACTION_ID id)
        {
            for (int i = 0; i < datafiles_.size(); ++i) {
                std::lock_guard<std::mutex> lock{mt_};
                datafiles_[i].rollback(id);
            }
            std::lock_guard<std::mutex> lock{mt_};
            auto it = std::remove_if(transactionList_.begin(), transactionList_.end(),
                                     [tId = id](Transaction &t) { return t.id() == tId; });
            transactionList_.erase(it, transactionList_.end());
            tIdGenerator_.release(id);
        }

        void toBytesDataFromString(const std::string &s, std::vector<std::byte> &out)
        {
            for (const char c : s) {
                out.push_back(static_cast<std::byte>(c));
            }
        }

        // 引数の先頭から'('以外が出現するまでにある'('を削除する
        // 引数の末尾から')'以外が出現するまでにある')'を削除する
        void trimParentheses(std::vector<std::byte> &bytes)
        {
            if (bytes.size() == 0) {
                return;
            }

            auto it = bytes.begin();
            while (static_cast<char>(*it) == '(' || static_cast<char>(*it) == ' ') {
                ++it;
            }
            bytes.erase(bytes.begin(), it);

            auto rit = bytes.rbegin();
            while (static_cast<char>(*rit) == ')' || static_cast<char>(*rit) == ' ') {
                ++rit;
            }
            bytes.erase(rit.base(), bytes.end());
        }

        void separate(const std::vector<std::byte> &data,
                      size_t &start,
                      std::vector<std::byte> &v,
                      std::vector<std::byte> &where)
        {
            // 直前がエスケープシーケンスであった場合にtrue
            bool isESMode = false;
            // ""の内部にいる場合にtrue
            bool isInnerDq = false;
            bool isCandidate = false;
            int i = start;
            for (; i < data.size(); ++i) {
                if (static_cast<char>(data[i]) == '\\') {
                    isCandidate = false;
                    if (isESMode) {
                        isESMode = false;
                    }
                    else {
                        isESMode = true;
                    }
                }
                else if (static_cast<char>(data[i]) == '"') {
                    isCandidate = false;
                    if (isESMode) {
                        // noop
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
                else if (static_cast<char>(data[i]) == ')') {
                    if (isCandidate) {
                        isCandidate = false;
                    }
                    else {
                        if (!isInnerDq) {
                            isCandidate = true;
                        }
                    }
                    isESMode = false;
                }
                else if (static_cast<char>(data[i]) == '(') {
                    if (isCandidate) {
                        break;
                    }
                    isCandidate = false;
                    isESMode = false;
                }
                else if (static_cast<char>(data[i]) != ' ') {
                    isCandidate = false;
                    isESMode = false;
                }
                v.push_back(data[i]);
            }
            // i = i + 1;
            for (; i < data.size(); ++i) {
                where.push_back(data[i]);
            }
            start = i;
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
                throw std::runtime_error{"number of sessions is upper limits." + FILE_INFO};
            }

            int i = 0;
            for (i = 0; i < conditions_.size(); ++i) {
                const SessionCondition &sc = conditions_.at(i);
                if (std::get<0>(sc) == connectionId) {
                    break;
                }
            }
            if (i == conditions_.size()) {
                throw std::runtime_error{"cannot find connection. should not reach here." + FILE_INFO};
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
                            DB_LOG << "child thread return." << FILE_INFO;
                            return;
                        }
                        { // Scoped Lock start
                            std::unique_lock<std::mutex> lock{std::get<1>(condition)};
                            if (!std::get<3>(condition)) {
                                DB_LOG << "wait for request from client." << FILE_INFO;
                                std::get<2>(condition).wait(lock, [&refB = std::get<3>(condition)] {
                                    return refB;
                                });
                            }
                        } // Scoped Lock end
                        if (toBeStopped_.load() || isClosed(id)) {
                            DB_LOG << "child thread return." << FILE_INFO;
                            return;
                        }

                        std::vector<std::byte> data;
                        std::vector<std::byte> response;
                        try {
                            DB_LOG << "connection id: " << id << " is processing." << FILE_INFO;
                            getData(id, data);

                            // 最初の要求はユーザーのセットであること
                            bool setUserOperation = false;
                            { // Scoped Lock start
                                std::lock_guard<std::mutex> lock{mt_};
                                if (connectedUsers_.find(id) == connectedUsers_.end()) {
                                    setUserOperation = true;
                                }
                            } // Scoped Lock end
                            if (setUserOperation) {
                                // ユーザーの設定
                                std::ostringstream oss{""};
                                size_t i = 0;
                                for (i = 0; i < data.size() && i < 7; ++i) {
                                    oss << static_cast<char>(data[i]);
                                }
                                if (toLower(oss.str()) != "please:") {
                                    Result r{-1, "", "", "parse error."};
                                    response = r.toBytes();
                                    setData(id, std::cref(response));
                                    toNotify(id);
                                    continue;
                                }
                                oss.str("");
                                for (; i < data.size() && i < 7 + 4; ++i) {
                                    oss << static_cast<char>(data[i]);
                                }
                                if (toLower(oss.str()) != "user") {
                                    Result r{-1, "", "", "operation PLEASE:USER is not done."};
                                    response = r.toBytes();
                                    setData(id, std::cref(response));
                                    toNotify(id);
                                    continue;
                                }
                                oss.str("");
                                std::string userName;
                                std::string password;
                                // iを空白文字が終わるまで進める
                                for (; i < data.size(); ++i) {
                                    if (static_cast<char>(data[i]) != ' ') {
                                        break;
                                    }
                                }
                                for (; i < data.size(); ++i) {
                                    if (static_cast<char>(data[i]) != ' ') {
                                        oss << static_cast<char>(data[i]);
                                    }
                                    else {
                                        break;
                                    }
                                }
                                userName = oss.str();
                                oss.str("");
                                // iを空白文字が終わるまで進める
                                for (; i < data.size(); ++i) {
                                    if (static_cast<char>(data[i]) != ' ') {
                                        break;
                                    }
                                }
                                for (; i < data.size(); ++i) {
                                    if (static_cast<char>(data[i]) != ' ') {
                                        oss << static_cast<char>(data[i]);
                                    }
                                    else {
                                        break;
                                    }
                                }
                                password = oss.str();
                                bool isSuccess = false;
                                for (const User &u : users_) {
                                    DB_LOG << "u.userName(): " << u.userName();
                                    if (u.userName() == userName) {
                                        std::string hash;
                                        toBCryptHash(password, hash);
                                        if (u.password() == hash) {
                                            { // Scoped Lock start
                                                std::lock_guard<std::mutex> lock{mt_};
                                                connectedUsers_.insert(std::make_pair(id, userName));
                                            } // Scoped Lock end
                                            DB_LOG << "User: " << userName << " authentication is success." << FILE_INFO;
                                            Result r{1, "", "", "User authentication is success."};
                                            response = r.toBytes();
                                            setData(id, std::cref(response));
                                            toNotify(id);
                                            isSuccess = true;
                                            break;
                                        }
                                    }
                                }
                                if (!isSuccess) {
                                    Result r{-1, "", "", "User authentication is failed."};
                                    response = r.toBytes();
                                    setData(id, std::cref(response));
                                    toNotify(id);
                                    continue;
                                }
                            }
                            // ユーザーが既に設定されていれば以下で処理継続
                            std::string userName;
                            { // Scoped Lock start
                                std::lock_guard<std::mutex> lock{mt_};
                                userName = connectedUsers_.at(id);
                            } // Scoped Lock end

                            // ユーザーがセットされてから最初の要求はトランザクションの開始であること
                            // 続く要求はこのコネクションのユーザーに許可された処理であること
                            // トランザクションがない状態で他の要求が来た場合はエラー
                            // またトランザクションがある状態でトランザクションの要求が来た場合もエラーとする
                            std::ostringstream oss{""};
                            size_t i = 0;
                            for (i = 0; i < data.size() && i < 7; ++i) {
                                oss << static_cast<char>(data[i]);
                            }
                            if (toLower(oss.str()) != "please:") {
                                Result r{-1, "", "", "parse error."};
                                response = r.toBytes();
                                setData(id, std::cref(response));
                                toNotify(id);
                                continue;
                            }
                            oss.str("");
                            for (; i < data.size() && i < 7 + 11; ++i) {
                                oss << static_cast<char>(data[i]);
                            }
                            if (isTransactionExists(id) && toLower(oss.str()) == "transaction") {
                                Result r{-1, "", "", "transaction is already exists."};
                                response = r.toBytes();
                                setData(id, std::cref(response));
                                toNotify(id);
                                continue;
                            }
                            if (!isTransactionExists(id) && toLower(oss.str()) != "transaction") {
                                Result r{-1, "", "", "cannot find transaction."};
                                response = r.toBytes();
                                setData(id, std::cref(response));
                                toNotify(id);
                                continue;
                            }
                            if (!isTransactionExists(id) && toLower(oss.str()) == "transaction") {
                                if (addTransaction(id)) {
                                    Result r{1, "", "", "transaction start is succeed."};
                                    response = r.toBytes();
                                    setData(id, std::cref(response));
                                    toNotify(id);
                                }
                                else {
                                    Result r{-1, "", "", "transaction start is failed."};
                                    response = r.toBytes();
                                    setData(id, std::cref(response));
                                    toNotify(id);
                                }
                                continue;
                            }
                            // ここに到達した場合はトランザクションは存在しているので実際の要求を処理する
                            // 操作名を取り出す
                            oss.str("");
                            i = 7;
                            // iを空白文字が終わるまで進める
                            for (; i < data.size(); ++i) {
                                if (static_cast<char>(data[i]) != ' ') {
                                    break;
                                }
                            }
                            for (; i < data.size(); ++i) {
                                if (static_cast<char>(data[i]) != ' ') {
                                    oss << static_cast<char>(data[i]);
                                }
                                else {
                                    break;
                                }
                            }
                            const std::string operationName = toLower(trim(oss.str(), '"'));

                            // テーブル名を取り出す
                            // iを空白文字が終わるまで進める
                            oss.str("");
                            for (; i < data.size(); ++i) {
                                if (static_cast<char>(data[i]) != ' ') {
                                    break;
                                }
                            }
                            for (; i < data.size(); ++i) {
                                if (static_cast<char>(data[i]) != ' ') {
                                    oss << static_cast<char>(data[i]);
                                }
                                else {
                                    break;
                                }
                            }
                            const std::string tableName = toLower(oss.str());
                            oss.str("");
                            // iを空白文字が終わるまで進める
                            for (; i < data.size(); ++i) {
                                if (static_cast<char>(data[i]) != ' ') {
                                    break;
                                }
                            }
                            if (operationName == "select") {
                                if (!getDatafile(tableName).isPermitted(operationName, userName)) {
                                    throw DatabaseException{"operation: " + operationName + " to " + tableName + " is not permitted. user: " + userName};
                                }
                                std::vector<std::byte> where;
                                for (; i < data.size(); ++i) {
                                    where.push_back(data[i]);
                                }
                                trimParentheses(where);
                                auto result = getDatafile(tableName).select(getTransactionId(id), where);
                                std::string tableInfo = getDatafile(tableName).tableInfo();
                                Result r{0, tableName, tableInfo, result};
                                response = r.toBytes();
                            }
                            else if (operationName == "insert") {
                                if (!getDatafile(tableName).isPermitted(operationName, userName)) {
                                    throw DatabaseException{"operation: " + operationName + " to " + tableName + " is not permitted. user: " + userName};
                                }
                                std::vector<std::byte> v;
                                for (; i < data.size(); ++i) {
                                    v.push_back(data[i]);
                                }
                                trimParentheses(v);
                                bool result = getDatafile(tableName).insert(getTransactionId(id), v);
                                if (!result) {
                                    rollbackTransaction(getTransactionId(id));
                                    throw DatabaseException{"transaction is terminated."};
                                }
                                std::string tableInfo = getDatafile(tableName).tableInfo();
                                Result r{1, tableName, tableInfo, "insert success."};
                                response = r.toBytes();
                            }
                            else if (operationName == "update") {
                                if (!getDatafile(tableName).isPermitted(operationName, userName)) {
                                    throw DatabaseException{"operation: " + operationName + " to " + tableName + " is not permitted. user: " + userName};
                                }
                                std::vector<std::byte> v;
                                std::vector<std::byte> where;
                                separate(data, i, v, where);
                                trimParentheses(v);
                                trimParentheses(where);
                                bool result = getDatafile(tableName).update(getTransactionId(id), v, where);
                                if (!result) {
                                    rollbackTransaction(getTransactionId(id));
                                    throw DatabaseException{"transaction is terminated."};
                                }
                                std::string tableInfo = getDatafile(tableName).tableInfo();
                                Result r{1, tableName, tableInfo, "update success."};
                                response = r.toBytes();
                            }
                            else if (operationName == "delete") {
                                if (!getDatafile(tableName).isPermitted(operationName, userName)) {
                                    throw DatabaseException{"operation: " + operationName + " to " + tableName + " is not permitted. user: " + userName};
                                }
                                std::vector<std::byte> where;
                                for (; i < data.size(); ++i) {
                                    where.push_back(data[i]);
                                }
                                trimParentheses(where);
                                bool result = getDatafile(tableName).update(getTransactionId(id), where);
                                if (!result) {
                                    rollbackTransaction(getTransactionId(id));
                                    throw DatabaseException{"transaction is terminated."};
                                }
                                std::string tableInfo = getDatafile(tableName).tableInfo();
                                Result r{1, tableName, tableInfo, "delete success."};
                                response = r.toBytes();
                            }
                            else if (operationName == "commit") {
                                commitTransaction(getTransactionId(id));
                                Result r{1, tableName, "", "commit success."};
                                response = r.toBytes();
                            }
                            else if (operationName == "rollback") {
                                rollbackTransaction(getTransactionId(id));
                                Result r{1, tableName, "", "rollback success."};
                                response = r.toBytes();
                            }
                            else if (operationName == "user") {
                                throw DatabaseException{"operation PLEASE:USER is already done."};
                            }
                            else {
                                throw DatabaseException{"unknown operation name: " + operationName};
                            }
                        }
                        catch (DatafileException &e) {
                            // TODO: エラー内容を送信する
                            DB_LOG << e.what() << FILE_INFO;
                            Result r{-1, "", "", e.what()};
                            setData(id, std::cref(response));
                            toNotify(id);
                            continue;
                        }
                        catch (DatabaseException &e) {
                            // TODO: エラー内容を送信する
                            DB_LOG << e.what() << FILE_INFO;
                            Result r{-1, "", "", e.what()};
                            setData(id, std::cref(response));
                            toNotify(id);
                            continue;
                        }

                        setData(id, std::cref(response));
                        toNotify(id);
                        DB_LOG << "notify to id: " << id << FILE_INFO;
                    }
                }
                catch (std::exception &e) {
                    CATCH_ALL_EXCEPTIONS(LOG << e.what() << FILE_INFO;)
                }
                catch (...) {
                    CATCH_ALL_EXCEPTIONS(LOG << "unexpected error or SEH exception." << FILE_INFO;)
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
                        DB_LOG << "toBeStopped_: " << toBeStopped_.load() << FILE_INFO;
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
                LOG << e.what() << FILE_INFO;
            }
        }

        IdGenerator<TRANSACTION_ID> tIdGenerator_;
        std::vector<Datafile> datafiles_;
        std::vector<Connection> connectionList_;
        std::vector<Transaction> transactionList_;
        std::vector<User> users_;
        // key: ConnectionのId, value: ユーザー名
        std::map<std::string, std::string> connectedUsers_;
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

    inline bool Connection::terminate()
    {
        return pDb_->terminate(id_);
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
            bool isSucceed;
            std::vector<std::map<std::string, std::string>> rows;
            std::string message;

            Result(const bool b, std::vector<std::map<std::string, std::string>> v, const std::string s)
                : isSucceed{b}, rows{v}, message{s}
            {
            }
        };

        Driver(const Connection con)
            : con_{con}
        {
        }

        // 引数のクエリをコネクションを通じてデータベースに送る
        // ユーザーの設定
        // PLEASE:USER userName password
        // このコネクションにおけるトランザクションを開始する:
        // PLEASE:TRANSACTION
        // テーブルへのselect ()内が照会する列
        // PLEASE:SELECT tableName (key1="value1",key2="value2"...)
        // テーブルへのinsert ()内が登録内容:
        // PLEASE:INSERT tableName (key1="value1",key2="value2"...)
        // テーブルへのupdate 前半の()内が更新内容 後半の()内が更新する列
        // PLEASE:UPDATE tableName (key1="value1",key2="value2"...) (key1="value1",key2="value2"...)
        // テーブルへのdelete ()内が削除する列
        // PLEASE:DELETE tableName (key1="value1",key2="value2"...)
        // トランザクションをコミットする
        // PLEASE:COMMIT
        // トランザクションをロールバックする
        // PLEASE:ROLLBACK
        Result sendQuery(std::string query)
        {
            std::string error = "";
            std::vector<std::map<std::string, std::string>> rows;
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
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    requestResult = con_.request();
                    if (!requestResult) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

                bool b = false;
                char flag = -1;
                std::ostringstream oss{""};
                auto it = data.begin();
                for (; it != data.end(); ++it) {
                    // 結果コード
                    if (static_cast<char>(*it) == ' ') {
                        // 次でこの空白は無関係のためインクリメントしてからbreak
                        ++it;
                        break;
                    }
                    if (static_cast<char>(*it) >= 0) {
                        b = true;
                        flag = static_cast<char>(*it);
                    }
                    else {
                        error = "operation failed";
                        flag = static_cast<char>(*it);
                    }
                }
                std::string tableName;
                for (; it != data.end(); ++it) {
                    // テーブル名
                    if (static_cast<char>(*it) == ' ') {
                        // 次でこの空白は無関係のためインクリメントしてからbreak
                        ++it;
                        break;
                    }
                    oss << static_cast<char>(*it);
                }
                tableName = oss.str();
                oss.str("");
                std::map<std::string, int> tableInfo;
                bool sizeMode = false;
                std::vector<std::byte> v;
                for (; it != data.end(); ++it) {
                    // 列情報
                    if (static_cast<char>(*it) == ' ') {
                        // 次でこの空白は無関係のためインクリメントしてからbreak
                        ++it;
                        break;
                    }
                    if (static_cast<char>(*it) == '=') {
                        sizeMode = true;
                        continue;
                    }
                    if (static_cast<char>(*it) == ',') {
                        if (v.size() > 0) {
                            std::string sizeStr;
                            sizeStr.resize(v.size());
                            for (int i = 0; i < sizeStr.length(); ++i) {
                                sizeStr[i] = static_cast<char>(v[i]);
                            }
                            int size = std::stoi(sizeStr);
                            v.clear();
                            tableInfo.insert(std::make_pair(oss.str(), size));
                            oss.str("");
                        }
                        sizeMode = false;
                        continue;
                    }
                    if (sizeMode) {
                        v.push_back(*it);
                    }
                    else {
                        oss << static_cast<char>(*it);
                    }
                }

                if (flag == 0) {
                    if (v.size() > 0) {
                        std::string sizeStr;
                        sizeStr.resize(v.size());
                        for (int i = 0; i < sizeStr.length(); ++i) {
                            sizeStr[i] = static_cast<char>(v[i]);
                        }
                        int size = std::stoi(sizeStr);
                        v.clear();
                        tableInfo.insert(std::make_pair(oss.str(), size));
                        oss.str("");
                    }

                    bool valueMode = false;
                    int sizeCount = 0;
                    int colCount = 0;
                    std::string colName;
                    std::map<std::string, std::string> row;
                    for (; it != data.end(); ++it) {
                        if (valueMode && sizeCount == 0) {
                            row.insert(std::make_pair(colName, oss.str()));
                            oss.str("");
                            valueMode = false;
                            if (++colCount == tableInfo.size()) {
                                rows.push_back(row);
                                row.clear();
                                colCount = 0;
                                sizeCount = 0;
                            }
                        }
                        if (!valueMode && static_cast<char>(*it) == '=') {
                            colName = oss.str();
                            oss.str("");
                            valueMode = true;
                            sizeCount = tableInfo.at(colName);
                        }
                        else if (valueMode && sizeCount > 0) {
                            oss << static_cast<char>(*it);
                            --sizeCount;
                        }
                        else {
                            oss << static_cast<char>(*it);
                        }
                    }
                    if (valueMode && oss.str().length() > 0) {
                        row.insert(std::make_pair(colName, oss.str()));
                        oss.str("");
                        valueMode = false;
                        if (++colCount == tableInfo.size()) {
                            rows.push_back(row);
                            row.clear();
                            colCount = 0;
                            sizeCount = 0;
                        }
                    }
                }
                else {
                    std::ostringstream msg{""};
                    bool valueMode = false;
                    for (; it != data.end(); ++it) {
                        if (static_cast<char>(*it) == '=') {
                            valueMode = true;
                            continue;
                        }
                        if (valueMode) {
                            msg << static_cast<char>(*it);
                        }
                    }
                    if (msg.str().length() > 0) {
                        error += " " + msg.str();
                    }
                }
                return Result{b, rows, error};
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