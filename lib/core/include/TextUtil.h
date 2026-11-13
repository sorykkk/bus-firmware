#pragma once
#include <string>

/// Small string helpers shared by the .ini and .env parsers. No Arduino dependency.
namespace txt {

std::string trimmed(const std::string &s);
std::string lower(std::string s);

/// Parses the right-hand side of `key = value`:
///  - "double quoted" values support \n \t \\ \" escapes,
///  - 'single quoted' values are taken literally,
///  - unquoted values end at an inline comment (any char of `commentChars`
///    that is at the start or preceded by whitespace) and are trimmed.
std::string parseValue(const std::string &raw, const char *commentChars);

bool toLong(const std::string &s, long &out);
bool toDouble(const std::string &s, double &out);
bool toBool(const std::string &s, bool &out);

/// Calls fn(line) for every line of `text` (handles \n and \r\n).
template <typename Fn>
void forEachLine(const std::string &text, Fn fn) {
    size_t pos = 0;
    while(pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if(nl == std::string::npos)
            nl = text.size();
        fn(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
}

} // namespace txt
