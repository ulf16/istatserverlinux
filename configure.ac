#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([istatserver],[3.02],[http://github.com/bjango/istatserverlinux/issues])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_MAINTAINER_MODE
AC_CANONICAL_HOST
AC_PROG_RANLIB
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs
AC_PROG_CXX([c++ g++ clang++ CC cxx cc++])
AC_PROG_CC([cc gcc clang])
dnl Require pkg-config (pkgconf on Debian/Ubuntu provides pkg-config)
AC_PATH_PROG([PKG_CONFIG], [pkg-config])
AS_IF([test -z "$PKG_CONFIG"], [
  AC_MSG_ERROR([pkg-config not found. Please install it (Debian/Ubuntu: sudo apt-get install -y pkgconf)])
])
#AC_PROG_CXX
#AC_PROG_CC
AC_PROG_INSTALL
 m4_ifdef([AC_PROG_AR],[AC_PROG_AR],[m4_ifdef([AM_PROG_AR],[AM_PROG_AR],[])])

LIBS="$LIBS -lpthread"

dnl ---- Probe -Wall for C and C++ (don’t clobber user-provided flags) ----
AC_MSG_CHECKING([whether C compiler accepts -Wall])
AS_VAR_COPY(_SAVE_CFLAGS, CFLAGS)
CFLAGS="$CFLAGS -Wall"
AC_LANG_PUSH([C])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])],
  [AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no]); CFLAGS="$_SAVE_CFLAGS"])
AC_LANG_POP([C])

AC_MSG_CHECKING([whether C++ compiler accepts -Wall])
AS_VAR_COPY(_SAVE_CXXFLAGS, CXXFLAGS)
CXXFLAGS="$CXXFLAGS -Wall"
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])],
  [AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no]); CXXFLAGS="$_SAVE_CXXFLAGS"])
AC_LANG_POP([C++])

AC_MSG_CHECKING([if the linker supports -rdynamic])
oldldflags="$LDFLAGS"
LDFLAGS="$LDFLAGS -rdynamic"
AC_LANG_PUSH([C])
AC_LINK_IFELSE([AC_LANG_PROGRAM([],[])],
  [AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no]); LDFLAGS="$oldldflags"])
AC_LANG_POP([C])

dnl ============ libxml2 (prefer pkg-config, fallback to xml2-config) ============
have_libxml2=no

PKG_CHECK_MODULES([LIBXML2], [libxml-2.0], [
  CPPFLAGS="$CPPFLAGS $LIBXML2_CFLAGS"
  LIBS="$LIBS $LIBXML2_LIBS"
  have_libxml2=yes
], []])

AS_IF([test "x$have_libxml2" != xyes], [
  AC_PATH_PROG([XML2_CONFIG], [xml2-config], [])
  AS_IF([test -z "$XML2_CONFIG"], [
    AC_MSG_ERROR([libxml2 not found (install pkg-config libxml-2.0 or xml2-config)])
  ])
  CPPFLAGS="$CPPFLAGS $($XML2_CONFIG --cflags)"
  LIBS="$LIBS $($XML2_CONFIG --libs)"
  AC_CHECK_HEADER([libxml/parser.h], [],
    [AC_MSG_ERROR([xml2-config found, but libxml2 headers are missing])]
  )
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM([[#include <libxml/parser.h>]], [[xmlInitParser(); xmlCleanupParser();]])],
    [have_libxml2=yes],
    [AC_MSG_ERROR([libxml2 libraries present, but link test failed])]
  )
])

AS_IF([test "x$have_libxml2" = xyes], [
  AC_DEFINE([HAVE_LIBXML2],[1],[Define if libxml2 is available])
], [
  AC_MSG_ERROR([libxml2 is required])
])

AC_ARG_WITH([no-sqlite],
  AS_HELP_STRING([--with-no-sqlite],[disable sqlite history storage]),
  [use_sqlite=no],
  [use_sqlite=auto])

dnl sqlite3 (unless --with-no-sqlite)
AS_IF([test "x$use_sqlite" != xno], [
  PKG_CHECK_MODULES([SQLITE3], [sqlite3],
    [CPPFLAGS="$CPPFLAGS $SQLITE3_CFLAGS"
     LIBS="$LIBS $SQLITE3_LIBS"
     AC_DEFINE([USE_SQLITE],1,[Use sqlite for storing history])
     use_sqlite=yes],
    [AC_MSG_ERROR([sqlite3 not found; use --with-no-sqlite to disable (not recommended)])]
  )
])

dnl --- Optional install of systemd unit (default: off to keep old behavior) ---
AC_ARG_ENABLE([systemd-unit],
  AS_HELP_STRING([--enable-systemd-unit],[install the systemd unit (default: no)]),
  [], [enable_systemd_unit=no])

dnl Where to install the unit if enabled. Allow override, else try pkg-config, else fallback.
AC_ARG_WITH([systemdsystemunitdir],
  AS_HELP_STRING([--with-systemdsystemunitdir=DIR],[directory for systemd unit files]),
  [systemdsystemunitdir="$withval"],
  [systemdsystemunitdir=])

AS_IF([test -z "$systemdsystemunitdir"], [
  AS_IF([test -n "$PKG_CONFIG" && $PKG_CONFIG --exists systemd], [
    systemdsystemunitdir=`$PKG_CONFIG --variable=systemdsystemunitdir systemd 2>/dev/null`
  ], [
    systemdsystemunitdir="/lib/systemd/system"
  ])
])

AC_SUBST([systemdsystemunitdir])
AM_CONDITIONAL([INSTALL_SYSTEMD_UNIT],
  [test "x$enable_systemd_unit" = "xyes" -a -n "$systemdsystemunitdir"])

# Add non-standard directories to the include path
AC_ARG_WITH([libraries],
  [AS_HELP_STRING([--with-libraries=PATH], [additional place to look for libraries])],
  [LDFLAGS="$LDFLAGS -L$withval"],
  []
)

AC_ARG_WITH([includes],
  [AS_HELP_STRING([--with-includes=PATH], [additional place to look for header files])],
  [CPPFLAGS="$CPPFLAGS -I$withval"],
  []
)
# Checks for libraries.
# AC_CHECK_LIB([dl], [dlopen],,[
# 	AC_CHECK_FUNC([dlopen],,[
# 		AC_MSG_ERROR([cannot find a suitable dlopen() implementation])
# 	])
# ])

CPPFLAGS="$CPPFLAGS -I/usr/local/include"
LDFLAGS="$LDFLAGS -L/usr/local/lib"

have_procfs=none

case "${host_os}" in
	linux|linux-*)
		have_procfs=procfs
		;;
esac


dnl Fall back to procfs, if available

use_cpu=$have_procfs
use_net=$have_procfs
use_uptime=$have_procfs
use_mem=$have_procfs
use_load=$have_procfs
use_activity=$have_procfs
use_battery=$have_procfs
use_processes=$have_procfs
use_disk_uuid=$have_procfs
use_disk=none
use_tls=none
use_bonjour=none

AC_CHECK_LIB([nsl],[inet_ntoa])
AC_CHECK_LIB([socket],[socket])

dnl ============ OpenSSL ============
use_tls=none
have_openssl=no

AS_IF([test -n "$PKG_CONFIG"], [
  PKG_CHECK_MODULES([OPENSSL], [openssl], [
    CPPFLAGS="$CPPFLAGS $OPENSSL_CFLAGS"
    LIBS="$LIBS $OPENSSL_LIBS"
    AC_DEFINE([HAVE_OPENSSL],1,[Define if openssl is available])
    have_openssl=yes
    use_tls=yes
  ], [])])

dnl Fallback link checks if pkg-config didn’t succeed
AS_IF([test "x$have_openssl" != xyes], [
  AC_CHECK_HEADERS([openssl/x509.h openssl/pem.h openssl/ssl.h openssl/evp.h openssl/err.h], [], [])
  dnl OpenSSL >= 1.1: OPENSSL_init_ssl exists; OpenSSL 1.0.x: SSL_library_init exists
  AC_CHECK_LIB([ssl],[OPENSSL_init_ssl], [
    AC_CHECK_LIB([crypto],[X509_new], [
      AC_DEFINE([HAVE_OPENSSL],1,[Define if openssl is available])
      LIBS="$LIBS -lssl -lcrypto"
      have_openssl=yes
      use_tls=yes
    ])
  ], [
    AC_CHECK_LIB([ssl],[SSL_library_init], [
      AC_CHECK_LIB([crypto],[X509_new], [
        AC_DEFINE([HAVE_OPENSSL],1,[Define if openssl is available])
        LIBS="$LIBS -lssl -lcrypto"
        have_openssl=yes
        use_tls=yes
      ])
    ])
  ])
])

AS_IF([test "x$use_tls" = xnone], [
  AC_MSG_ERROR([OpenSSL not found. Install openssl dev headers or provide pkg-config openssl.])
])

AC_ARG_WITH(no-avahi,
  [  --with-no-avahi   disable avahi support],
  	use_bonjour=no,
  [
	AC_CHECK_HEADERS([avahi-common/thread-watch.h], [
		AC_CHECK_LIB([avahi-client], [avahi_client_new], [
			AC_CHECK_LIB([avahi-common],[avahi_threaded_poll_new], [
				AC_DEFINE_UNQUOTED([HAVE_LIBAVAHI_CLIENT], 1, [Define if avahi is available])
				LIBS="$LIBS -lavahi-client -lavahi-common"
				use_bonjour=yes
			])
		])
	], [])
])

AC_CHECK_HEADERS([zlib.h], [
	AC_DEFINE_UNQUOTED([HAVE_LIBZLIB], 1, [Define if zlib is available])
	LIBS="$LIBS -lz"
], [])

dnl Specific tests for probe types

AC_CHECK_LIB([kstat],[kstat_open],[
AC_DEFINE_UNQUOTED([HAVE_LIBKSTAT],1,[Define if libkstat is available])
have_kstat=yes
LIBS="$LIBS -lkstat"
use_cpu=kstat
use_net=kstat
use_uptime=kstat
use_mem=kstat
use_processes=psinfo
use_activity=kstat
],[have_kstat=no])

if test x"$have_kstat" = x"no" ; then
	AC_CHECK_LIB([kvm],[kvm_open],[
	AC_DEFINE_UNQUOTED([HAVE_LIBKVM],1,[Define if libkvm is available])
	LIBS="$LIBS -lkvm"
	use_mem=kvm
	use_processes=kvm
	])
fi

case "${host_os}" in
	*hpux11*)
		use_cpu=hpux
		use_mem=hpux
		use_net=hpux
		use_load=hpux
		use_processes=hpux
		use_activity=hpux
		use_uptime=hpux
		CPPFLAGS="$CPPFLAGS -D_PSTAT64"
		;;
	*openbsd*)
		use_cpu=sysctl
		use_mem=sysctl
		use_net=getifaddrs
		use_processes=kvm
		use_activity=hw_diskstats_open
		AC_DEFINE_UNQUOTED([PROCESSES_KVM_OPENBSD],1,[Define for openbsd])
		;;
	*netbsd*)
		use_cpu=sysctlbyname
		use_mem=sysctl
		use_net=getifaddrs
		use_processes=kvm
		use_activity=hw_diskstats_net
		AC_DEFINE_UNQUOTED([PROCESSES_KVM_NETBSD],1,[Define for netbsd])
		AC_DEFINE_UNQUOTED([GETMNTINFO_STATVFS],1,[Define if getmntinfo uses statvfs])
		;;
	*dragonfly*)
		use_cpu=sysctlbyname
		use_mem=sysctlbyname
		use_net=getifaddrs
		use_processes=kvm
		AC_DEFINE_UNQUOTED([PROCESSES_KVM_DRAGONFLY],1,[Define for dragonfly])
		;;
	*solaris*)
		AC_DEFINE_UNQUOTED([IPS_IOCTL],1,[Define to use ioctl for ip addresses on solaris])
		;;
	*aix*)
		use_processes=aix
		;;
esac

AC_CHECK_LIB([sensors],[sensors_init],[
	AC_DEFINE_UNQUOTED([HAVE_LIBSENSORS],1,[Define if libsensors is available])
	LIBS="$LIBS -lsensors"
])

AC_CHECK_LIB([devstat], [devstat_getdevs],[
AC_DEFINE_UNQUOTED([HAVE_DEVSTAT],1,[Define if devstat is available])
use_activity=devstat
LIBS="$LIBS -ldevstat"
],[])

AC_CHECK_LIB([devstat], [getdevs],[
AC_DEFINE_UNQUOTED([HAVE_DEVSTAT_ALT],1,[Define if devstat is available])
use_activity=devstat
LIBS="$LIBS -ldevstat"
],[])

AC_CHECK_LIB([perfstat],[perfstat_cpu_total],[
AC_DEFINE_UNQUOTED([HAVE_LIBPERFSTAT],1,[Define if libperfstat is available])
have_perfstat=yes
LIBS="$LIBS -lperfstat"
use_cpu=perfstat
use_net=perfstat
use_uptime=perfstat
use_mem=perfstat
use_load=perfstat
use_activity=perfstat
],[have_perfstat=no])

AC_CHECK_FUNC([getloadavg],[
use_load=getloadavg
])

AC_CHECK_HEADERS_ONCE([sys/time.h])

AC_CHECK_FUNC([clock_gettime], [
  AC_MSG_CHECKING([if CLOCK_UPTIME is available])
  AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE([[
      #include <time.h>
      #ifdef HAVE_SYS_TIME_H
      # include <sys/time.h>
      #endif
      static clockid_t testuptime = CLOCK_UPTIME;
      int main(void) { return (int)testuptime; }
    ]])],
    [have_clock_uptime=yes; use_uptime=gettime],
    [have_clock_uptime=no]
  )
  AC_MSG_RESULT([$have_clock_uptime])
])

dnl Use sysctl for cpu and net if procfs isn't available
AC_CHECK_FUNC([sysctl],[
AC_DEFINE_UNQUOTED([HAVE_SYSCTL],1,[Define if sysctl is available])
test x"$use_cpu" = x"none" && use_cpu=sysctlbyname
test x"$use_net" = x"none" && use_net=sysctl
have_sysctl=yes
],[have_sysctl=no])

AC_CHECK_FUNC([sysctlbyname],[
AC_DEFINE_UNQUOTED([HAVE_SYSCTLBYNAME],1,[Define if sysctlbyname is available])

if test x"$use_battery" = x"none" ; then
	case "${host_os}" in
		freebsd*)
		use_battery=sysctl
		;;
	esac
fi
],)

dnl Try to use sysctl on freebsd/netbsd if there's no CLOCK_UPTIME
if test x"$use_uptime" = x"none" ; then
	case "$host_os" in
		freebsd*|*netbsd*)
			if test x"$have_sysctl" = x"yes" ; then
				use_uptime=sysctl
			fi
			;;
	esac
fi

AC_CHECK_FUNC([statvfs],[
use_disk=statvfs
],[
	AC_CHECK_FUNC([statfs],[
		use_disk=statfs
	])
])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h mntent.h netdb.h stdlib.h string.h paths.h sys/socket.h sys/statfs.h sys/statvfs.h sys/mnttab.h sys/loadavg.h kstat.h errno.h sys/sysinfo.h sys/processor.h sys/swap.h kvm.h alloca.h sys/resource.h netinet/in.h sys/sysctl.h sys/vmmeter.h sys/param.h sys/user.h sys/sched.h sys/dkstat.h sys/ioctl.h sensors/sensors.h libperfstat.h devstat.h ifaddrs.h dirent.h inet/common.h sys/sockio.h dev/acpica/acpiio.h sys/stat.h procfs.h sys/disk.h uvm/uvm_extern.h sys/time.h sys/procfs.h procinfo.h])

# hp-ux headers
AC_CHECK_HEADERS([sys/pstat.h sys/dk.h sys/dlpi.h sys/dlpi_ext.h sys/mib.h sys/stropts.h])
AC_CHECK_MEMBER(struct pst_diskinfo.psd_dkbytewrite,
	[AC_DEFINE([HAVE_HPUX_DISK_RW],[1],[pst_diskinfo.psd_dkbytewrite])],
	[],
	[#include <sys/pstat.h>])

AC_CHECK_HEADERS([machine/apmvar.h], [
	use_battery=apm
], [], [])

AC_CHECK_HEADERS([dev/acpica/acpiio.h], [
	use_battery=acpi
], [], [])

# Mute warning in freebsd 6.0
AC_CHECK_HEADERS([sys/mount.h], [], [], [#include <sys/param.h>])

# Mute warning in openbsd 7.0
AC_CHECK_HEADERS([sys/proc.h], [], [], [#include <sys/param.h>])

# check net/if.h seperately, as it requires other headers on atleast FreeBSD
AC_CHECK_HEADERS([net/if.h net/if_mib.h net/if_var.h net/if_types.h],[],[],
[[
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_NET_IF_H
# include <net/if.h>
#endif
]])

# Checks for getifaddrs.
AC_CHECK_FUNCS([getifaddrs],[
	AC_DEFINE_UNQUOTED([HAVE_GETIFADDRS],[1],[Define if getifaddrs() is available.])]
,[])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_GETMNTENT
AC_CHECK_FUNCS([gethostbyname getmntent getmntinfo inet_ntoa memset mkdir select socket strerror statvfs setmntent])

# Try to figure out which type of mntent structure we're dealing with
AC_CHECK_MEMBER([struct mnttab.mnt_special],[AC_DEFINE_UNQUOTED([USE_STRUCT_MNTTAB],[1],[define to use 'struct mnttab'])],,[
#include <stdio.h>
#include <sys/mnttab.h>
])
AC_CHECK_MEMBER([struct mntent.mnt_fsname],[AC_DEFINE_UNQUOTED([USE_STRUCT_MNTENT],[1],[define to use 'struct mntent'])],,[
#include <stdio.h>
#include <mntent.h>
])
AC_CHECK_MEMBER([struct statvfs.f_frsize],[AC_DEFINE_UNQUOTED([HAVE_STATVFS_FRSIZE],[1],[define if statvfs has the f_frsize member])],,[
#include <sys/types.h>
#include <sys/statvfs.h>
])

AC_MSG_CHECKING([for GCC/Clang to enable hardening flags])
AS_IF([$CXX --version >/dev/null 2>&1], [
  CXXFLAGS="$CXXFLAGS -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-plt -fPIC"
  CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-plt -fPIC"
  LDFLAGS="$LDFLAGS -Wl,-z,relro -Wl,-z,now"
])
AC_MSG_RESULT([enabled])

AC_MSG_CHECKING([for default location of config directory])
test x"$prefix" = x"NONE" && prefix="$ac_default_prefix"
configpath=`eval echo "$sysconfdir"/istatserver/`
AC_MSG_RESULT([$configpath])
AC_DEFINE_UNQUOTED([CONFIG_PATH],["${configpath}"],[default location of the certificate files])

AC_MSG_CHECKING([for default location of configuration file])
test x"$prefix" = x"NONE" && prefix="$ac_default_prefix"
configpath=`eval echo "$sysconfdir"/istatserver/istatserver.conf`
AC_MSG_RESULT([$configpath])

AC_MSG_CHECKING([CPU usage probe type])
case $use_cpu in
	hpux)
		AC_DEFINE_UNQUOTED([USE_CPU_HPUX],1,[Use pstat_getdynamic to probe CPU usage])
		;;
	procfs)
		AC_DEFINE_UNQUOTED([USE_CPU_PROCFS],1,[Use procfs to probe CPU usage])
		;;
	kstat)
		AC_DEFINE_UNQUOTED([USE_CPU_KSTAT],1,[Use kstat(3) to probe CPU usage])
		;;
	perfstat)
		AC_DEFINE_UNQUOTED([USE_CPU_PERFSTAT],1,[Use AIX perfstat to probe CPU usage])
		;;
	sysctlbyname)
		AC_DEFINE_UNQUOTED([USE_CPU_SYSCTL_BYNAME],1,[Use sysctlbyname to probe CPU usage])
		;;
	sysctl)
		AC_DEFINE_UNQUOTED([USE_CPU_SYSCTL],1,[Use sysctl to probe CPU usage])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_CPU_NONE],1,[No CPU usage])
		;;
esac
AC_MSG_RESULT($use_cpu)

AC_MSG_CHECKING([load average probe type])
case $use_load in
    hpux)
        AC_DEFINE_UNQUOTED([USE_LOAD_HPUX],1,[Use pstat_getdynamic to probe load average])
        ;;
    procfs)
        AC_DEFINE_UNQUOTED([USE_LOAD_PROCFS],1,[Use procfs to probe load average])
        ;;
	getloadavg)
		AC_DEFINE_UNQUOTED([USE_LOAD_GETLOADAVG],1,[Use getloadavg(3) to probe load average])
		;;
	perfstat)
		AC_DEFINE_UNQUOTED([USE_LOAD_PERFSTAT],1,[Use perfstat (AIX) to probe load average])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_LOAD_NONE],1,[No load average])
		;;
esac
AC_MSG_RESULT($use_load)

AC_MSG_CHECKING([memory usage probe type])
case $use_mem in
	hpux)
		AC_DEFINE_UNQUOTED([USE_MEM_HPUX],1,[Use pstat_getdynamic to probe memory usage])
		;;
	procfs)
		AC_DEFINE_UNQUOTED([USE_MEM_PROCFS],1,[Use procfs to probe memory usage])
		;;
	kstat)
		AC_DEFINE_UNQUOTED([USE_MEM_KSTAT],1,[Use kstat(3) to probe memory usage])
		;;
	perfstat)
		AC_DEFINE_UNQUOTED([USE_MEM_PERFSTAT],1,[Use perfstat (AIX) to probe memory usage])
		;;
	kvm)
		AC_DEFINE_UNQUOTED([USE_MEM_KVM],1,[Use kvm(3) to probe memory usage])
		;;
	sysctl)
		AC_DEFINE_UNQUOTED([USE_MEM_SYSCTL],1,[Use sysctl to probe memory usage])
		;;
	sysctlbyname)
		AC_DEFINE_UNQUOTED([USE_MEM_SYSCTLBYNAME],1,[Use sysctl to probe memory usage])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_MEM_NONE],1,[No memory usage])
		;;
esac
AC_MSG_RESULT($use_mem)

AC_MSG_CHECKING([uptime probe type])
case $use_uptime in
	hpux)
		AC_DEFINE_UNQUOTED([USE_UPTIME_HPUX],1,[Use pstat_getdynamic to probe uptime])
		;;
	procfs)
		AC_DEFINE_UNQUOTED([USE_UPTIME_PROCFS],1,[Use procfs to probe uptime])
		;;
	kstat)
		AC_DEFINE_UNQUOTED([USE_UPTIME_KSTAT],1,[Use kstat(3) to probe uptime])
		;;
	perfstat)
		AC_DEFINE_UNQUOTED([USE_UPTIME_PERFSTAT],1,[Use perfstat(3) to probe uptime])
		;;
	gettime)
		AC_DEFINE_UNQUOTED([USE_UPTIME_GETTIME],1,[Use clock_gettime(2) to probe load uptime])
		;;
	sysctl)
		AC_DEFINE_UNQUOTED([USE_UPTIME_SYSCTL],1,[Use sysctl(2) to probe uptime])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_UPTIME_NONE],1,[No uptime])
		;;
esac
AC_MSG_RESULT($use_uptime)

AC_MSG_CHECKING([network activity probe type])
case $use_net in
	hpux)
		AC_DEFINE_UNQUOTED([USE_NET_HPUX],1,[Use pstat_getdynamic to probe network activity])
		;;
	procfs)
		AC_DEFINE_UNQUOTED([USE_NET_PROCFS],1,[Use procfs to probe network activity])
		;;
	kstat)
		AC_DEFINE_UNQUOTED([USE_NET_KSTAT],1,[Use kstat(3) to probe network activity])
		;;
	perfstat)
		AC_DEFINE_UNQUOTED([USE_NET_PERFSTAT],1,[Use perfstat(3) to probe network activity])
		;;
	sysctl)
		AC_DEFINE_UNQUOTED([USE_NET_SYSCTL],1,[Use sysctl to probe network activity])
		;;
	getifaddrs)
		AC_DEFINE_UNQUOTED([USE_NET_GETIFADDRS],1,[Use getifaddrs to probe network activity])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_NET_NONE],1,[No network activity])
		;;
esac
AC_MSG_RESULT($use_net)

AC_MSG_CHECKING([disk space probe type])
case $use_disk in
	statfs|statvfs)
		AC_DEFINE_UNQUOTED([USE_DISK_STATFS],1,[Use statfs/statvfs(2) to probe disk space])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_DISK_NONE],1,[No disk space])
		;;
esac
AC_MSG_RESULT($use_disk)

AC_MSG_CHECKING([disk activity probe type])
case $use_activity in
	hpux)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_HPUX],1,[Use pstat_getdynamic to probe disk activity])
		;;
	procfs)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_PROCFS],1,[Use procfs to probe disk activity])
		;;
	devstat)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_DEVSTAT],1,[Use devstat to probe disk activity])
		;;
	kstat)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_KSTAT],1,[Use kstat to probe disk activity])
		;;
	hw_diskstats_open)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_HWDS_OPEN],1,[Use hw.diskstats to probe disk activity])
		;;
	hw_diskstats_net)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_HWDS_NET],1,[Use hw.diskstats to probe disk activity])
		;;
	perfstat)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_PERFSTAT],1,[Use AIX perfstat to probe disk activity])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_ACTIVITY_NONE],1,[No disk activity])
		;;
esac
AC_MSG_RESULT($use_activity)

AC_MSG_CHECKING([battery probe type])
case $use_battery in
	procfs)
		AC_DEFINE_UNQUOTED([USE_BATTERY_PROCFS],1,[Use procfs to probe battery])
		;;
	sysctl)
		AC_DEFINE_UNQUOTED([USE_BATTERY_SYSCTL],1,[Use sysctl to probe battery])
		;;
	acpi)
		AC_DEFINE_UNQUOTED([USE_BATTERY_ACPI],1,[Use acpi to probe battery])
		;;
	apm)
		AC_DEFINE_UNQUOTED([USE_BATTERY_APM],1,[Use apm to probe battery])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_BATTERY_NONE],1,[No battery])
		;;
esac
AC_MSG_RESULT($use_battery)

AC_MSG_CHECKING([processes probe type])
case $use_processes in
	hpux)
		AC_DEFINE_UNQUOTED([USE_PROCESSES_HPUX],1,[Use pstat_getdynamic to probe processes])
		;;
	procfs)
		AC_DEFINE_UNQUOTED([USE_PROCESSES_PROCFS],1,[Use procfs to probe processes])
		;;
	kvm)
		AC_DEFINE_UNQUOTED([USE_PROCESSES_KVM],1,[Use kvm to probe processes])
		;;
	psinfo)
		AC_DEFINE_UNQUOTED([USE_PROCESSES_PSINFO],1,[Use proc/psinfo to probe processes])
		;;
	aix)
		AC_DEFINE_UNQUOTED([USE_PROCESSES_AIX],1,[Use getprocs64/psinfo to probe processes])
		;;
	none)
		AC_DEFINE_UNQUOTED([USE_PROCESSES_NONE],1,[No processes])
		;;		
esac
AC_MSG_RESULT($use_processes)

case $use_disk_uuid in
	procfs)
		AC_DEFINE_UNQUOTED([USE_DISKUUID_PROCFS],1,[Use procfs to probe disk uuids])
		;;
esac

AC_MSG_CHECKING([checking bonjour/zeroconf support])
AC_MSG_RESULT($use_bonjour)

AC_MSG_CHECKING([checking tls support])
AC_MSG_RESULT($use_tls)

AC_MSG_CHECKING([checking sqlite support])
AC_MSG_RESULT($use_sqlite)

AC_CONFIG_FILES([
Makefile
src/Makefile
resource/Makefile
conf/Makefile
])
AC_OUTPUT
