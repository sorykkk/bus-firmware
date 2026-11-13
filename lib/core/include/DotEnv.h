#pragma once
#include <map>
#include <string>

/// Minimal .env reader (used for the per-device secrets file).
///   KEY=value            keys are case-sensitive
///   export KEY="value"   optional `export`, double quotes support escapes, single quotes are literal
///   # comment            inline comments need a space before the #
/// Unlike desktop dotenv libraries it never touches the process environment.
class DotEnv {
public:
    /// Returns false if some lines were malformed (they are skipped, the rest is still loaded).
    bool parse(const std::string &text);

    bool has(const std::string &key) const { return vars_.count(key) != 0; }
    std::string get(const std::string &key, const std::string &def = "") const;

    const std::map<std::string, std::string> &all() const { return vars_; }
    int errorCount() const { return errors_; }

private:
    std::map<std::string, std::string> vars_;
    int errors_ = 0;
};
