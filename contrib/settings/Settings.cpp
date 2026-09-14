#include "Settings.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <map>
#include <pwd.h>
#include <sstream>

namespace istat_settings {
static const size_t limit = 262144;
static std::string trim(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
static bool utf8(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i++];
        if (c < 0x80) { if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') return false; continue; }
        unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!count || i + count > s.size()) return false;
        unsigned value = c & ((1u << (6 - count)) - 1);
        for (unsigned n = 0; n < count; ++n) {
            unsigned char tail = s[i++]; if ((tail & 0xc0) != 0x80) return false;
            value = (value << 6) | (tail & 0x3f);
        }
        if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800) ||
            (count == 3 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
static std::string quoted(const std::string &s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 0x20) {
            const char *hex = "0123456789abcdef";
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        } else out += c;
    }
    return out + '"';
}
std::string failure(const std::string &state) {
    return "{\"schema\":1,\"state\":" + quoted(state) + "}";
}
std::string configPath(const std::string &installation) {
    if (installation == "opt") return "/opt/istatserverlinux/etc/istatserver/istatserver.conf";
    if (installation == "local") return "/usr/local/etc/istatserver/istatserver.conf";
    return "";
}
std::string parse(const std::string &contents, bool reveal) {
    if (contents.size() > limit || !utf8(contents)) return failure("invalid-configuration");
    std::map<std::string, std::string> values;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        size_t split = line.find_first_of(" \t");
        if (split == std::string::npos) return failure("invalid-configuration");
        std::string key = line.substr(0, split), value = trim(line.substr(split));
        if (value.empty()) return failure("invalid-configuration");
        if (key == "server_code" || key == "network_port" || key == "network_addr")
            values.emplace(key, value); // Config::get uses the first occurrence.
    }
    std::string port = values.count("network_port") ? values["network_port"] : "5109";
    std::string address = values.count("network_addr") ? values["network_addr"] : "0.0.0.0";
    std::string secret = values.count("server_code") ? values["server_code"] : "00000";
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
        return failure("invalid-configuration");
    int number = std::stoi(port);
    unsigned char ip[16];
    if (!number || number > 65535 || (inet_pton(AF_INET, address.c_str(), ip) != 1 && inet_pton(AF_INET6, address.c_str(), ip) != 1) ||
        secret.size() > 4096 || secret.empty() || (secret.front() == '(' && secret.back() == ')'))
        return failure("invalid-configuration");
    // Match Socket.cpp, including its treatment of leading-zero codes as text.
    bool passcode = secret.size() == 5 && secret[0] >= '1' && secret[0] <= '9' && secret.find_first_not_of("0123456789") == std::string::npos;
    std::string result = "{\"schema\":1,\"state\":\"readable\",\"scope\":\"configuration-file\",\"port\":" + std::to_string(number) +
        ",\"address\":" + quoted(address) + ",\"bonjour\":\"not-observed\",\"auth_mode\":" + quoted(passcode ? "passcode" : "password") +
        ",\"credential_source\":" + quoted(values.count("server_code") ? "configured" : "server-default") +
        ",\"credential_present\":true";
    if (reveal) result += ",\"credential\":" + quoted(secret);
    return result + "}";
}

static std::string readFromRoot(int fd, const std::string &path, bool reveal, uid_t rootOwner, uid_t daemonOwner) {
    // Traverse with directory descriptors: reject symlinks and user-writable
    // ancestors rather than allowing a privileged read to escape the fixed path.
    if (fd < 0) return failure("unavailable");
    size_t start = 1;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        bool last = end == std::string::npos;
        std::string part = path.substr(start, last ? std::string::npos : end - start);
        int next = openat(fd, part.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (last ? 0 : O_DIRECTORY));
        int error = errno; close(fd); fd = next;
        if (fd < 0) return failure(error == ENOENT ? "missing" : "unsafe-or-inaccessible-path");
        struct stat info;
        bool daemonLeaf = last || path.substr(start) == "istatserver/istatserver.conf";
        if (fstat(fd, &info) || (info.st_uid != rootOwner && !(daemonLeaf && info.st_uid == daemonOwner)) || (info.st_mode & (S_IWGRP | S_IWOTH)) ||
            (last ? (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size > (off_t)limit) : !S_ISDIR(info.st_mode))) {
            close(fd); return failure("unsafe-or-inaccessible-path");
        }
        if (last) break;
        start = end + 1;
    }
    struct stat before, after;
    if (fstat(fd, &before)) { close(fd); return failure("unavailable"); }
    std::string data;
    char buffer[4096];
    ssize_t count;
    do {
        count = ::read(fd, buffer, sizeof(buffer));
        if (count > 0) data.append(buffer, count);
    } while ((count > 0 && data.size() <= limit) || (count < 0 && errno == EINTR));
    bool failed = count < 0 || fstat(fd, &after) != 0;
    close(fd);
    if (failed) return failure("unavailable");
#ifdef __APPLE__
    bool changed = before.st_mtimespec.tv_sec != after.st_mtimespec.tv_sec || before.st_mtimespec.tv_nsec != after.st_mtimespec.tv_nsec ||
                   before.st_ctimespec.tv_sec != after.st_ctimespec.tv_sec || before.st_ctimespec.tv_nsec != after.st_ctimespec.tv_nsec;
#else
    bool changed = before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                   before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec;
#endif
    if (changed || before.st_size != after.st_size) return failure("configuration-changed");
    return parse(data, reveal);
}
std::string read(const std::string &installation, bool reveal) {
    std::string path = configPath(installation);
    if (path.empty()) return failure("unsupported-installation");
    if (geteuid() != 0) return failure("authorization-required");
    // Our installers assign the last directory and config to the service user.
    // Ancestors must still be root-owned; never follow links, including at leaf.
    struct passwd entry, *user = nullptr;
    char storage[16384];
    uid_t daemon = 0;
    if (getpwnam_r("istat", &entry, storage, sizeof(storage), &user) == 0 && user) daemon = user->pw_uid;
    return readFromRoot(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC), path, reveal, 0, daemon);
}
#ifdef ISTAT_SETTINGS_TESTING
std::string readFixture(const std::string &root, const std::string &installation, bool reveal, unsigned owner) {
    std::string path = configPath(installation);
    if (path.empty()) return failure("unsupported-installation");
    return readFromRoot(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), path, reveal, owner, owner);
}
#endif
}
