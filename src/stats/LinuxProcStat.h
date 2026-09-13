#ifndef ISTAT_LINUX_PROC_STAT_H
#define ISTAT_LINUX_PROC_STAT_H

#include <limits>
#include <sstream>
#include <string>

namespace istat {

struct LinuxProcStat {
    unsigned long long userTicks;
    unsigned long long systemTicks;
    unsigned long long residentPages;
    long threads;
};

inline bool parseUnsignedProcField(const std::string &text, unsigned long long &value)
{
    if (text.empty()) return false;
    unsigned long long parsed = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        unsigned digit = text[i] - '0';
        if (parsed > (std::numeric_limits<unsigned long long>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

inline bool parseLinuxProcStat(const std::string &text, LinuxProcStat &result)
{
    // Field 2 is parenthesized comm, which may itself contain spaces, ')' or '\n'.
    const size_t open = text.find('('), close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open ||
        open < 2 || text[open - 1] != ' ' || close + 2 >= text.size() || text[close + 1] != ' ') return false;
    unsigned long long pid;
    if (!parseUnsignedProcField(text.substr(0, open - 1), pid) || pid == 0) return false;
    std::istringstream fields(text.substr(close + 2));
    std::string token;
    LinuxProcStat parsed = {};
    for (unsigned field = 3; field <= 24; ++field) {
        if (!(fields >> token)) return false;
        if (field == 3 && token.size() != 1) return false;
        if (field == 14 && !parseUnsignedProcField(token, parsed.userTicks)) return false;
        if (field == 15 && !parseUnsignedProcField(token, parsed.systemTicks)) return false;
        if (field == 20) {
            unsigned long long count;
            if (!parseUnsignedProcField(token, count) || count > (unsigned long long)std::numeric_limits<long>::max()) return false;
            parsed.threads = (long)count;
        }
        if (field == 24 && !parseUnsignedProcField(token, parsed.residentPages)) return false;
    }
    result = parsed;
    return true;
}

inline bool residentBytes(const LinuxProcStat &stat, unsigned long long pageSize, unsigned long long &bytes)
{
    if (!pageSize || stat.residentPages > std::numeric_limits<unsigned long long>::max() / pageSize) return false;
    bytes = stat.residentPages * pageSize;
    return true;
}

} // namespace istat
#endif
