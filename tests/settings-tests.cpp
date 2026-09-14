#include "../contrib/settings/Settings.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <cstdlib>
using namespace istat_settings;
int main() {
    std::string config = "# header\nnetwork_port 5110\nnetwork_addr ::1\nserver_code 83706\nserver_code ignored-secret\nunknown key\n";
    std::string publicResult = parse(config, false);
    assert(publicResult.find("83706") == std::string::npos);
    assert(publicResult.find("ignored-secret") == std::string::npos);
    assert(publicResult.find("\"credential\":") == std::string::npos);
    assert(publicResult.find("\"port\":5110") != std::string::npos);
    assert(publicResult.find("\"auth_mode\":\"passcode\"") != std::string::npos);
    assert(parse(config, true).find("\"credential\":\"83706\"") != std::string::npos);
    assert(parse("server_code 01234", false).find("\"password\"") != std::string::npos);
    assert(parse("", false).find("server-default") != std::string::npos);
    assert(parse("server_code text with spaces # ignored", true).find("text with spaces\"") != std::string::npos);
    assert(parse("server_code a\\\"b", true).find("a\\\\\\\"b") != std::string::npos);
    assert(parse("server_code \xc3\xbc", true).find("\xc3\xbc") != std::string::npos);
    for (const std::string bad : {"network_port 0", "network_port 65536", "network_port secret", "network_addr nope", "server_code (secret)", "server_code", "server_code "})
        assert(parse(bad, true) == failure("invalid-configuration"));
    assert(parse(std::string(262145, 'a'), true) == failure("invalid-configuration"));
    assert(parse(std::string("server_code \xc0\x80"), true) == failure("invalid-configuration"));
    assert(parse(std::string("server_code a\0b", 15), true) == failure("invalid-configuration"));
    assert(configPath("local") == "/usr/local/etc/istatserver/istatserver.conf");
    for (const std::string path : {"/etc/passwd", "../local", "classic", "", "opt/etc"})
        assert(read(path, true) == failure("unsupported-installation"));
    if (geteuid() != 0) assert(read("opt", true) == failure("authorization-required"));
#ifdef ISTAT_SETTINGS_TESTING
    char temp[] = "/tmp/istat-settings-tests.XXXXXX";
    assert(mkdtemp(temp));
    std::string root = temp, current = root;
    for (auto segment : {"opt", "istatserverlinux", "etc", "istatserver"}) {
        current += "/" + std::string(segment); assert(mkdir(current.c_str(), 0700) == 0);
    }
    std::string file = current + "/istatserver.conf", db = current + "/istatserver.db";
    { std::ofstream f(file); f << config; }
    assert(chmod(file.c_str(), 0600) == 0);
    { std::ofstream f(db); f << "history-must-not-be-touched"; }
    struct stat before, after;
    assert(stat(db.c_str(), &before) == 0);
    auto load = [&]{ return readFixture(root, "opt", false, getuid()); };
    assert(load() == publicResult);
    assert(readFixture(root, "opt", false, getuid() + 1) == failure("unsafe-or-inaccessible-path"));
    assert(chmod(file.c_str(), 0666) == 0);
    assert(load() == failure("unsafe-or-inaccessible-path"));
    assert(chmod(file.c_str(), 0600) == 0);
    assert(link(file.c_str(), (file + ".link").c_str()) == 0);
    assert(load() == failure("unsafe-or-inaccessible-path"));
    assert(unlink((file + ".link").c_str()) == 0);
    assert(unlink(file.c_str()) == 0);
    assert(symlink(db.c_str(), file.c_str()) == 0);
    assert(load() == failure("unsafe-or-inaccessible-path"));
    assert(unlink(file.c_str()) == 0);
    assert(mkfifo(file.c_str(), 0600) == 0);
    assert(load() == failure("unsafe-or-inaccessible-path"));
    assert(unlink(file.c_str()) == 0);
    assert(load() == failure("missing"));
    assert(rename(current.c_str(), (current + ".saved").c_str()) == 0);
    assert(symlink((current + ".saved").c_str(), current.c_str()) == 0);
    assert(load() == failure("unsafe-or-inaccessible-path"));
    assert(unlink(current.c_str()) == 0);
    assert(rename((current + ".saved").c_str(), current.c_str()) == 0);
    assert(stat(db.c_str(), &after) == 0);
    assert(before.st_ino == after.st_ino && before.st_mtime == after.st_mtime && before.st_size == after.st_size && before.st_mode == after.st_mode);
    assert(unlink(db.c_str()) == 0);
    while (current != root) { assert(rmdir(current.c_str()) == 0); current = current.substr(0, current.rfind('/')); }
    assert(rmdir(root.c_str()) == 0);
#endif
    std::cout << "Settings parsing, redaction and target-boundary tests passed\n";
}
