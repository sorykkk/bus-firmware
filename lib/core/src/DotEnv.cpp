#include "DotEnv.h"
#include "TextUtil.h"

bool DotEnv::parse(const std::string &text) {
    vars_.clear();
    errors_ = 0;

    txt::forEachLine(text, [&](const std::string &raw) {
        std::string line = txt::trimmed(raw);
        if(line.empty() || line[0] == '#')
            return;
        if(line.compare(0, 7, "export ") == 0)
            line = txt::trimmed(line.substr(7));

        size_t eq = line.find('=');
        std::string key = eq == std::string::npos ? "" : txt::trimmed(line.substr(0, eq));
        if(key.empty() || key.find_first_of(" \t") != std::string::npos) {
            errors_++;
            return;
        }
        vars_[key] = txt::parseValue(line.substr(eq + 1), "#");
    });
    return errors_ == 0;
}

std::string DotEnv::get(const std::string &key, const std::string &def) const {
    auto it = vars_.find(key);
    return it == vars_.end() ? def : it->second;
}
