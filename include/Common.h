#ifndef DEADLOCK_EXAMPLE_COMMON_INCLUDED
#define DEADLOCK_EXAMPLE_COMMON_INCLUDED

#include "General.h"

#include <filesystem>
#include <map>
#include <ostream>
#include <string>

#define CATCH_ALL_EXCEPTIONS(statements) \
    try {                                \
        statements                       \
    }                                    \
    catch (...) {                        \
    }

#define LOG logger.stream().out()
#define DEBUG_LOG logger.stream().debug()
#define DB_LOG logger.stream("DB").out()
#define WEB_LOG logger.stream("WEB").out()
#define FILE_INFO \
    std::string { " [file: " + std::filesystem::path{__FILE__}.filename().string() + ", function: " + std::string{__FUNCTION__} + ", line: " + std::to_string(__LINE__) + "]" }

namespace PapierMache {
    template <typename outT>
    class Logger;

    namespace DbStuff {
        class Database;
    }
}

extern PapierMache::Logger<std::ostream> logger;

extern const std::map<std::string, std::map<std::string, std::string>> webConfiguration;

// extern PapierMache::DbStuff::Database database;

#endif // DEADLOCK_EXAMPLE_COMMON_INCLUDED