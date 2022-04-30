#ifndef DEADLOCK_EXAMPLE_COMMON_INCLUDED
#define DEADLOCK_EXAMPLE_COMMON_INCLUDED

#define _UNICODE
#define UNICODE

#include <map>
#include <ostream>
#include <string>

#define CATCH_ALL_EXCEPTIONS(statements) \
    try {                                \
        statements                       \
    }                                    \
    catch (...) {                        \
    }

namespace PapierMache {
    template <typename outT>
    class Logger;
}

extern PapierMache::Logger<std::ostream> logger;

extern std::map<std::string, std::map<std::string, std::string>> webConfiguration;

#endif // DEADLOCK_EXAMPLE_COMMON_INCLUDED