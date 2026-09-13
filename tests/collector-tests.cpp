#include "stats/LinuxProcStat.h"
#include "stats/FilesystemFilters.h"
#include "stats/SmartMetadata.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>

static void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << std::endl; std::exit(1); }
}

static std::string record(const std::string &name, const std::string &rss = "72815",
                          const std::string &user = "16888", const std::string &system = "391",
                          const std::string &threads = "4")
{
    return "95689 (" + name + ") S 95435 95435 95435 0 -1 4194624 678914 919241 15 251 " + user + " " + system + " 8883 482 20 0 " + threads + " 0 268071713 522231808 " + rss + " 18446744073709551615\n";
}

int main(int argc, char **argv)
{
    char healthPath[] = "/tmp/istat-health-test.XXXXXX";
    int healthFD = mkstemp(healthPath);
    require(healthFD >= 0, "Create isolated health fixture");
    close(healthFD);
    {
        std::ofstream xml(healthPath);
        xml << "<health version='1'><volume bsd='disk3s1s1' devices='disk0' state='passed' checked='" << time(NULL) << "' detail='SSD &amp; model'/><volume bsd='sda2' state='passed' checked='1'/></health>";
    }
    istat::SmartMetadata metadata;
    metadata.refresh(healthPath);
    require(metadata.lookup("disk3s1s1").devices == "disk0" && metadata.lookup("disk3s1s1").state == "passed", "Resolve exact cached volume identity");
    require(metadata.lookup("disk3s1s1").detail == "SSD & model", "Decode XML safely");
    require(metadata.lookup("sda2").state == "stale", "Old health must not remain passed");
    require(metadata.lookup("absent").state.empty(), "Missing health is not passed");
    {
        std::ofstream xml(healthPath);
        xml << "<!DOCTYPE health [<!ENTITY x 'passed'>]><health version='1'><volume bsd='sda2' state='&x;'/></health>";
    }
    istat::SmartMetadata unsafe;
    unsafe.refresh(healthPath);
    require(unsafe.lookup("sda2").state.empty(), "Reject DTD-bearing health cache");
    unlink(healthPath);
    if (argc == 2) {
        std::ifstream file(argv[1]);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        istat::LinuxProcStat stat;
        require(file.is_open() && !file.bad() && istat::parseLinuxProcStat(text, stat), "Could not parse live proc stat");
        std::cout << "rss_pages=" << stat.residentPages << " user_ticks=" << stat.userTicks << " system_ticks=" << stat.systemTicks << " threads=" << stat.threads << std::endl;
        return 0;
    }
    const char *names[] = {"simple", "[celeryd: celer", "name ) with (", "name\nwith space", ")", ""};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        istat::LinuxProcStat stat;
        require(istat::parseLinuxProcStat(record(names[i]), stat), "Parse names with whitespace and parentheses");
        require(stat.userTicks == 16888 && stat.systemTicks == 391 && stat.threads == 4 && stat.residentPages == 72815, "Read fields 14, 15, 20 and 24 without shifting");
        unsigned long long bytes;
        require(istat::residentBytes(stat, 4096, bytes) && bytes == 298250240, "RSS is pages, not virtual size");
        require(istat::residentBytes(stat, 16384, bytes) && bytes == 1193000960, "Respect runtime page size");
    }
    istat::LinuxProcStat stat = {};
    require(!istat::parseLinuxProcStat("1 (truncated) S 1", stat), "Reject incomplete reads");
    require(!istat::parseLinuxProcStat("1 no delimiters S 1", stat), "Reject malformed comm");
    require(!istat::parseLinuxProcStat(record("bad", "-1"), stat), "Reject negative RSS instead of wrapping");
    require(!istat::parseLinuxProcStat(record("bad", "123garbage"), stat), "Reject partial numeric fields");
    require(!istat::parseLinuxProcStat(record("bad", "18446744073709551616"), stat), "Reject integer overflow");
    require(!istat::parseLinuxProcStat(record("bad", "10", "bad"), stat), "Reject malformed CPU ticks");
    require(!istat::parseLinuxProcStat(record("bad", "10", "20", "-1"), stat), "Reject negative CPU ticks");
    require(!istat::parseLinuxProcStat(record("bad", "10", "20", "30", "-1"), stat), "Reject negative thread count");
    require(!istat::parseLinuxProcStat(record("bad", "10", "20", "30", "18446744073709551615"), stat), "Reject overflowing thread count");
    require(istat::parseLinuxProcStat(record("large", "10", "4294967296", "4294967297"), stat) && stat.systemTicks == 4294967297ULL, "Retain 64-bit CPU tick counters");
    require(istat::parseLinuxProcStat(record("zero", "0"), stat) && stat.residentPages == 0, "Kernel threads may have zero RSS");
    unsigned long long bytes;
    stat.residentPages = std::numeric_limits<unsigned long long>::max();
    require(!istat::residentBytes(stat, 4096, bytes) && !istat::residentBytes(stat, 0, bytes), "Reject byte overflow and zero page size");
    const char *ignored[] = {"cgroup2", "bpf", "tracefs", "efivarfs", "nsfs", "proc", "sysfs", "tmpfs", "debugfs"};
    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i)
        require(istat::isIgnoredFilesystem(ignored[i]), "Exclude pseudo-filesystems");
    const char *real[] = {"ext4", "xfs", "btrfs", "apfs", "hfs", "nfs", "nfs4", "cifs", "vfat", "zfs", "overlay", ""};
    for (size_t i = 0; i < sizeof(real) / sizeof(real[0]); ++i)
        require(!istat::isIgnoredFilesystem(real[i]), "Retain real/local/network filesystem types");
    std::cout << "Collector parser and filesystem filter tests passed" << std::endl;
    return 0;
}
