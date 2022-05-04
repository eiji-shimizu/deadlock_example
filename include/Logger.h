#ifndef DEADLOCK_EXAMPLE_LOGGER_INCLUDED
#define DEADLOCK_EXAMPLE_LOGGER_INCLUDED

#include "General.h"

#include "Common.h"

#include <mutex>
#include <sstream>

namespace PapierMache {

    template <typename outT>
    class Logger {
    public:
        // template <typnename outT>
        class LogStream {
        public:
            LogStream(Logger<outT> &refLogger)
                : refLogger_{refLogger},
                  isNullMode_{false}
            {
            }
            LogStream(Logger<outT> &refLogger, bool isNullMode)
                : refLogger_{refLogger},
                  isNullMode_{isNullMode}
            {
            }
            ~LogStream()
            {
                CATCH_ALL_EXCEPTIONS(
                    if (buffer_.str().length() > 0) {
                        refLogger_.out(buffer_);
                    })
            }
            std::ostream &out()
            {
                if (isNullMode_) {
                    return garbage_;
                }
                return buffer_;
            }

            // コピー禁止
            LogStream(const LogStream &) = delete;
            LogStream &operator=(const LogStream &) = delete;

            // ムーブ演算
            LogStream(LogStream &&rhs)
                : buffer_{std::move(rhs.buffer_)},
                  refLogger_{rhs.refLogger_},
                  isNullMode_{rhs.isNullMode_}
            {
            }

            LogStream &operator=(LogStream &&rhs)
            {
                if (this == &rhs) {
                    return *this;
                }
                buffer_ = std::move(rhs.buffer_);
                return *this;
            }

        private:
            std::ostringstream buffer_;
            Logger &refLogger_;
            bool isNullMode_;
            std::ostringstream garbage_;
        };

        Logger(outT &out)
            : refOut_{out}
        {
        }
        ~Logger()
        {
            // noop
        }

        LogStream stream(const std::string &prefix = "")
        {
#ifdef WEB_LOG_MASK
            if (prefix == "web" || prefix == "WEB") {
                return std::move(LogStream{*this, true});
            }
#endif // WEB_LOG_MASK
#ifdef DB_LOG_MASK
            if (prefix == "db" || prefix == "DB") {
                return std::move(LogStream{*this, true});
            }
#endif // DB_LOG_MASK
            LogStream ls{*this};
            if (prefix.length() > 0) {
                ls.out() << prefix << ": ";
            }
            return std::move(ls);
        }

        Logger &out(std::ostringstream &log)
        {
            std::lock_guard<std::mutex> lock{mt_};
            refOut_ << log.str() << std::endl;
            return *this;
        }

        // コピー禁止
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        // ムーブ禁止
        Logger(Logger &&) = delete;
        Logger &operator=(Logger &&) = delete;

    private:
        // 実際の出力先 参照なのでライフタイムの管理はしない
        outT &refOut_;
        // バッファから実際に出力する際に使うミューテックス
        std::mutex mt_;
    };

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_LOGGER_INCLUDED