#ifndef DEADLOCK_EXAMPLE_CONNECTIONS_INCLUDED
#define DEADLOCK_EXAMPLE_CONNECTIONS_INCLUDED

#include "General.h"

#include "Database.h"

#include <mutex>
#include <vector>

namespace PapierMache {

    class Connections {
    public:
        void add(const PapierMache::DbStuff::Connection c)
        {
            std::lock_guard<std::mutex> lock{mt_};
            v_.push_back(c);
        }

        void terminateAll()
        {
            std::lock_guard<std::mutex> lock{mt_};
            LOG << "v_.size(): " << v_.size();
            for (auto c : v_) {
                c.terminate();
                LOG << "c.terminate()";
            }
            v_.clear();
            LOG << "v_.clear()";
        }

    private:
        std::vector<PapierMache::DbStuff::Connection> v_;
        std::mutex mt_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_CONNECTIONS_INCLUDED