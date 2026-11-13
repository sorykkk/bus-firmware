#include "IniFile.h"
#include "TextUtil.h"

bool IniFile::parse(const std::string &text) {
    data_.clear();
    errors_ = 0;
    std::string section;

    txt::forEachLine(text, [&](const std::string &raw) {
        std::string line = txt::trimmed(raw);
        if(line.empty() || line[0] == ';' || line[0] == '#')
            return;

        if(line[0] == '[') {
            size_t end = line.find(']');
            if(end == std::string::npos) {
                errors_++;
                return;
            }
            section = txt::lower(txt::trimmed(line.substr(1, end - 1)));
            data_[section];
            return;
        }

        size_t eq = line.find('=');
        std::string key = eq == std::string::npos ? "" : txt::lower(txt::trimmed(line.substr(0, eq)));
        if(key.empty()) {
            errors_++;
            return;
        }
        data_[section][key] = txt::parseValue(line.substr(eq + 1), ";#");
    });
    return errors_ == 0;
}

const std::string *IniFile::find(const std::string &section, const std::string &key) const {
    auto s = data_.find(txt::lower(section));
    if(s == data_.end())
        return nullptr;
    auto k = s->second.find(txt::lower(key));
    return k == s->second.end() ? nullptr : &k->second;
}

bool IniFile::has(const std::string &section, const std::string &key) const {
    return find(section, key) != nullptr;
}

std::string IniFile::get(const std::string &section, const std::string &key, const std::string &def) const {
    const std::string *v = find(section, key);
    return v ? *v : def;
}

long IniFile::getInt(const std::string &section, const std::string &key, long def) const {
    const std::string *v = find(section, key);
    long out;
    return (v && txt::toLong(*v, out)) ? out : def;
}

double IniFile::getFloat(const std::string &section, const std::string &key, double def) const {
    const std::string *v = find(section, key);
    double out;
    return (v && txt::toDouble(*v, out)) ? out : def;
}

bool IniFile::getBool(const std::string &section, const std::string &key, bool def) const {
    const std::string *v = find(section, key);
    bool out;
    return (v && txt::toBool(*v, out)) ? out : def;
}
