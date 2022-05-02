#ifndef DEADLOCK_EXAMPLE_UTILS_INCLUDED
#define DEADLOCK_EXAMPLE_UTILS_INCLUDED

#include "General.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
        size_t index = std::distance(s.crbegin(), rit);
        auto first = it;
        auto end = std::next(s.cbegin(), s.length() - index);
        if (std::distance(s.cbegin(), first) <= std::distance(s.cbegin(), end)) {
            std::for_each(first, end, [&oss](char c) { oss << c; });
        }
        return oss.str();
    }

    inline std::string removeExtension(const std::string &s)
    {
        auto rit = s.crbegin();
        while (*rit != '.' && rit < s.crend()) {
            ++rit;
        }
        size_t index = std::distance(s.crbegin(), rit);
        if (s.length() - index == 0) {
            return s;
        }
        auto first = s.cbegin();
        auto end = std::next(s.cbegin(), s.length() - index - 1);
        std::ostringstream oss{""};
        std::for_each(first, end, [&oss](char c) { oss << c; });
        return oss.str();
    }

    template <typename T>
    inline char *as_bytes(T &i)
    {
        void *addr = &i;
        return static_cast<char *>(addr);
    }

    // 引数のファイル名をiniファイルとみなして読み込んだマップを返す
    // エスケープシーケンスには対応していない
    // 日本語などはvalueにのみ設定可能
    // [section]
    // name="value"
    // name=value
    // name="value"
    // [section] ;comment
    // name="value"
    // ; comment
    // name="value" ;comment
    inline std::map<std::string, std::map<std::string, std::string>> readConfiguration(const std::filesystem::path configFile)
    {
        std::ifstream ifs{configFile};
        if (!ifs) {
            throw std::runtime_error{"configuration file: " + configFile.string() + " is not found."};
        }

        std::map<std::string, std::map<std::string, std::string>> result;
        std::string line;
        // このフラグはセクションパース: 0, nameパース: 1, valueパース: 2, commentパース: 3を設定する
        std::ostringstream section{""};
        std::ostringstream name{""};
        std::ostringstream value{""};
        int lineCount = 0;
        while (std::getline(ifs, line)) {
            int flg = -1;
            bool toNextLine = false;
            bool isSurroundedByDoubleQuotes = false;
            ++lineCount;
            for (const char c : line) {
                if (toNextLine) {
                    // 次の行に進むべきなのでコメントと空白文字以外はエラー
                    if (c == ';') {
                        // for文を抜けて次の行に進む
                        break;
                    }
                    else if (!std::isspace(c)) {
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " '" + std::string{c} + "' cannot appear this position."};
                    }
                }
                if (std::isspace(c)) {
                    switch (flg) {
                    case -1:
                        continue;
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain space."};
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain space."};
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (c == '[') {
                    switch (flg) {
                    case -1:
                        section.str("");
                        flg = 0;
                        break;
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain '['"};
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain '['"};
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (c == ']') {
                    switch (flg) {
                    case -1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " ']' cannot appear this position."};
                    case 0:
                        toNextLine = true;
                        break;
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain ']'"};
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (c == '=') {
                    switch (flg) {
                    case -1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " '=' cannot appear this position."};
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain '='"};
                    case 1:
                        flg = 2;
                        break;
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (c == '"') {
                    switch (flg) {
                    case -1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " '\"' cannot appear this position."};
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain '\"'"};
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain '\"'"};
                    case 2:
                        if (isSurroundedByDoubleQuotes) {
                            isSurroundedByDoubleQuotes = false;
                            toNextLine = true;
                        }
                        else {
                            isSurroundedByDoubleQuotes = true;
                        }
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (c == ';') {
                    switch (flg) {
                    case -1:
                        flg = 3;
                        break;
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain ';'"};
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain ';'"};
                    case 2:
                        if (isSurroundedByDoubleQuotes) {
                            value << c;
                        }
                        else {
                            flg = 3;
                        }
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (c == '_') {
                    switch (flg) {
                    case -1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " ';' cannot appear this position."};
                    case 0:
                        section << c;
                        break;
                    case 1:
                        name << c;
                        break;
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (std::isalnum(c)) {
                    switch (flg) {
                    case -1:
                        flg = 1;
                        name << c;
                        break;
                    case 0:
                        section << c;
                        break;
                    case 1:
                        name << c;
                        break;
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else if (std::ispunct(c)) {
                    switch (flg) {
                    case -1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " '" + std::string{c} + "' cannot appear this position."};
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain '" + std::string{c} + "'"};
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain '" + std::string{c} + "'"};
                    case 2:
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
                else {
                    switch (flg) {
                    case -1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " '" + std::string{c} + "' cannot appear this position."};
                    case 0:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " section cannot contain '" + std::string{c} + "'"};
                    case 1:
                        throw std::runtime_error{"parse error at line " + std::to_string(lineCount) + " name cannot contain '" + std::string{c} + "'"};
                    case 2:
                        // 以上の分岐の他にvalueに入れてはいけない文字があるならばここで処理する
                        value << c;
                        break;
                    case 3:
                        continue;
                    }
                }
            }
            if (flg == 0) {
                result.insert_or_assign(section.str(), std::map<std::string, std::string>{});
            }
            else if (flg == 2) {
                result.at(section.str()).insert_or_assign(name.str(), value.str());
                name.str("");
                value.str("");
            }
        }
        return result;
    }

    template <typename T>
    inline const T getValue(const std::map<std::string, std::map<std::string, std::string>> &config,
                            const std::string &section,
                            const std::string &key)
    {
        return static_cast<T>(config.at(section).at(key));
    }
    template <>
    inline const int getValue<int>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                   const std::string &section,
                                   const std::string &key)
    {
        return std::stoi(config.at(section).at(key));
    }
    template <>
    inline const long getValue<long>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                     const std::string &section,
                                     const std::string &key)
    {
        return std::stol(config.at(section).at(key));
    }
    template <>
    inline const long long getValue<long long>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                               const std::string &section,
                                               const std::string &key)
    {
        return std::stoll(config.at(section).at(key));
    }
    template <>
    inline const unsigned long getValue<unsigned long>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                                       const std::string &section,
                                                       const std::string &key)
    {
        return std::stoul(config.at(section).at(key));
    }
    template <>
    inline const unsigned long long getValue<unsigned long long>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                                                 const std::string &section,
                                                                 const std::string &key)
    {
        return std::stoull(config.at(section).at(key));
    }
    template <>
    inline const float getValue<float>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                       const std::string &section,
                                       const std::string &key)
    {
        return std::stof(config.at(section).at(key));
    }
    template <>
    inline const double getValue<double>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                         const std::string &section,
                                         const std::string &key)
    {
        return std::stod(config.at(section).at(key));
    }
    template <>
    inline const long double getValue<long double>(const std::map<std::string, std::map<std::string, std::string>> &config,
                                                   const std::string &section,
                                                   const std::string &key)
    {
        return std::stold(config.at(section).at(key));
    }

} // namespace PapierMache

#endif // DEADLOCK_EXAMPLE_UTILS_INCLUDED