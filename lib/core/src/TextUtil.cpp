#include "TextUtil.h"
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace txt {

static const char *kWs = " \t\r\n\f\v";

std::string trimmed(const std::string &s) {
    size_t b = s.find_first_not_of(kWs);
    if(b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(kWs);
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for(auto &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string parseValue(const std::string &raw, const char *commentChars) {
    std::string v = trimmed(raw);
    if(v.empty())
        return v;

    const char q = v[0];
    if(q == '"' || q == '\'') {
        std::string out;
        for(size_t i = 1; i < v.size(); i++) {
            char c = v[i];
            if(c == q)
                return out;                         // anything after the closing quote is ignored
            if(q == '"' && c == '\\' && i + 1 < v.size()) {
                char n = v[++i];
                switch(n) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    default:  out += n;    break;   // \\  \"  and unknown escapes
                }
                continue;
            }
            out += c;
        }
        return out;                                 // unterminated quote: take the rest
    }

    for(size_t i = 0; i < v.size(); i++) {
        if(std::strchr(commentChars, v[i]) && (i == 0 || v[i - 1] == ' ' || v[i - 1] == '\t'))
            return trimmed(v.substr(0, i));
    }
    return v;
}

bool toLong(const std::string &s, long &out) {
    std::string t = trimmed(s);
    if(t.empty())
        return false;
    int base = (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) ? 16 : 10;
    char *end = nullptr;
    errno = 0;
    long v = std::strtol(t.c_str(), &end, base);
    if(errno || *end != '\0')
        return false;
    out = v;
    return true;
}

bool toDouble(const std::string &s, double &out) {
    std::string t = trimmed(s);
    if(t.empty())
        return false;
    char *end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if(*end != '\0')
        return false;
    out = v;
    return true;
}

bool toBool(const std::string &s, bool &out) {
    std::string t = lower(trimmed(s));
    if(t == "1" || t == "true" || t == "yes" || t == "on")  { out = true;  return true; }
    if(t == "0" || t == "false" || t == "no" || t == "off") { out = false; return true; }
    return false;
}

} // namespace txt
