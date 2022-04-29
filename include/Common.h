#ifndef DEADLOCK_EXAMPLE_COMMON_INCLUDED
#define DEADLOCK_EXAMPLE_COMMON_INCLUDED

#define _UNICODE
#define UNICODE

#include <ostream>

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

#endif // DEADLOCK_EXAMPLE_COMMON_INCLUDED