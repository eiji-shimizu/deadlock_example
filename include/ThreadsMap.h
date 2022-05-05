#ifndef DEADLOCK_EXAMPLE_THREADS_MAP_INCLUDED
#define DEADLOCK_EXAMPLE_THREADS_MAP_INCLUDED

#include "General.h"

#include "Common.h"
#include "Logger.h"

#include <map>
#include <mutex>
#include <vector>

namespace PapierMache {

    class ThreadsMap {
    public:
        ThreadsMap(int cleanUpPoint)
            : cleanUpPoint_{cleanUpPoint}
        {
        }
        ThreadsMap()
            : cleanUpPoint_{10}
        {
        }
        ~ThreadsMap(){
            CATCH_ALL_EXCEPTIONS({
                LOG << "~ThreadsMap(): BEFORE";
                LOG << "threads_.size(): " << threads_.size();
                for (auto &e : threads_) {
                    if (e.second.joinable()) {
                        LOG << "e.second.join() BEFORE";
                        e.second.join();
                        LOG << "e.second.join() AFTER";
                    }
                }
                LOG << "~ThreadsMap(): AFTER";
            })}

        // コピー禁止
        ThreadsMap(const ThreadsMap &) = delete;
        ThreadsMap &operator=(const ThreadsMap &) = delete;
        // ムーブ禁止
        ThreadsMap(ThreadsMap &&) = delete;
        ThreadsMap &operator=(ThreadsMap &&) = delete;

        size_t size() const
        {
            // この関数はデバッグ用なのでロック無し
            return threads_.size();
        }

        bool addThread(std::thread &&t)
        {
            std::lock_guard<std::mutex> lock{mt_};
            std::thread::id threadId = t.get_id();
            threads_.insert(std::make_pair(t.get_id(), std::move(t)));
            finishedFlag_.try_emplace(threadId, false);
            return true;
        }

        void setFinishedFlag(const std::thread::id id)
        {
            std::lock_guard<std::mutex> lock{mt_};
            finishedFlag_.insert_or_assign(id, true);
        }

        void cleanUp()
        {
            std::lock_guard<std::mutex> lock{mt_};
            if (threads_.size() > cleanUpPoint_) {
                std::vector<std::thread::id> vec;
                for (const auto &p : finishedFlag_) {
                    if (p.second) {
                        vec.push_back(p.first);
                        // finishedFlgがtrueのスレッドのみjoin
                        DEBUG_LOG << "thread id : " << p.first << " join for cleanup.";
                        threads_.at(p.first).join();
                    }
                }
                for (const auto id : vec) {
                    threads_.erase(id);
                    finishedFlag_.erase(id);
                }
            }
        }

    private:
        // この数よりスレッド数が多い場合にはクリーンアップが有効になる
        const int cleanUpPoint_;
        std::map<std::thread::id, std::thread> threads_;
        std::map<std::thread::id, bool> finishedFlag_;
        std::mutex mt_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_THREADS_MAP_INCLUDED