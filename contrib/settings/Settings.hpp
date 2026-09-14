#pragma once
#include <string>

namespace istat_settings {
// Fixed installation IDs, never caller-supplied privileged paths.
std::string configPath(const std::string &installation);
std::string parse(const std::string &contents, bool reveal);
std::string read(const std::string &installation, bool reveal);
std::string failure(const std::string &state);
#ifdef ISTAT_SETTINGS_TESTING
std::string readFixture(const std::string &root, const std::string &installation, bool reveal, unsigned owner);
#endif
}
