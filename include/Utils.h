#ifndef DEADLOCK_EXAMPLE_UTILS_INCLUDED
#define DEADLOCK_EXAMPLE_UTILS_INCLUDED

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>

namespace PapierMache {

    // 引数の文字列から空白を除去した文字列を返す
    inline std::string removeSpace(const std::string &s)
    {
        // 全角空白などに対応していない非常に簡易的な実装
        std::ostringstream oss{""};
        for (const char c : s) {
            if (!std::isspace(c)) {
                oss << c;
            }
        }
        return oss.str();
    }

    inline std::string trim(const std::string &s, const char target)
    {
        std::ostringstream oss{""};
        auto it = s.cbegin();
        auto rit = s.crbegin();
        while (*it == target) {
            ++it;
        }
        while (*rit == target) {
            ++rit;
        }
        size_t index0 = std::distance(s.cbegin(), it);
        size_t index = std::distance(s.crbegin(), rit);
        auto first = it;
        auto end = std::next(s.cbegin(), s.length() - index);
        if (std::distance(s.cbegin(), first) <= std::distance(s.cbegin(), end)) {
            std::for_each(first, end, [&oss](char c) { oss << c; });
        }
        return oss.str();
    }

}
#endif // DEADLOCK_EXAMPLE_UTILS_INCLUDED