#pragma once
#include <map>
#include <string>

/// Minimal .ini reader (used for env/config.ini).
///   [section]           section and key names are case-insensitive
///   key = value         ; or # starts a comment (inline comments need a space before them)
///   key = "quoted ; value"
/// Keys before the first [section] live in section "".
class IniFile {
public:
    using Section = std::map<std::string, std::string>;

    /// Returns false if some lines were malformed (they are skipped, the rest is still loaded).
    bool parse(const std::string &text);

    bool has(const std::string &section, const std::string &key) const;
    std::string get(const std::string &section, const std::string &key, const std::string &def = "") const;
    long   getInt(const std::string &section, const std::string &key, long def) const;
    double getFloat(const std::string &section, const std::string &key, double def) const;
    bool   getBool(const std::string &section, const std::string &key, bool def) const;

    const std::map<std::string, Section> &sections() const { return data_; }
    int errorCount() const { return errors_; }

private:
    const std::string *find(const std::string &section, const std::string &key) const;

    std::map<std::string, Section> data_;
    int errors_ = 0;
};
