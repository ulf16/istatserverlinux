#ifndef ISTAT_FILESYSTEM_FILTERS_H
#define ISTAT_FILESYSTEM_FILTERS_H

#include <cstring>

namespace istat {
inline bool isIgnoredFilesystem(const char *type)
{
    static const char *const ignored[] = {
        "rpc_pipefs", "rootfs", "configfs", "hugetlbfs", "nfsd", "mqueue",
        "selinuxfs", "linprocfs", "binfmt_misc", "tmpfs", "devpts", "devtmpfs",
        "pstore", "prl_fs", "cgroup", "iso9660", "securityfs", "fusectl",
        "proc", "procfs", "debugfs", "fuse.gvfsd-fuse", "sysfs", "devfs",
        "autofs", "fd", "lofs", "sharefs", "objfs", "mntfs", "dev",
        "cgroup2", "bpf", "tracefs", "efivarfs", "nsfs"
    };
    if (!type) return false;
    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i)
        if (std::strcmp(type, ignored[i]) == 0) return true;
    return false;
}
} // namespace istat
#endif
