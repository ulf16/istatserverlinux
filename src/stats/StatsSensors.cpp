/*
 *  Copyright 2016 Bjango Pty Ltd. All rights reserved.
 *  Copyright 2010 William Tisäter. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    1.  Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *    2.  Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 *    3.  The name of the copyright holder may not be used to endorse or promote
 *        products derived from this software without specific prior written
 *        permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL WILLIAM TISÄTER BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include "StatsSensors.h"

/* Standard C/C++ headers needed here */
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <map>

/* POSIX / system headers */
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_LIBSENSORS
  /* libsensors API */
  #include <sensors/sensors.h>
  #include <sensors/error.h>
#endif

using namespace std;

// Ensure Linux-specific headers for helpers using open/read/close/errno/O_CLOEXEC
#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
// ---- Linux sysfs helpers (shared) ----
static bool read_sysfs_ll(const std::string &path, long long &out) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    long long v = 0; int rc = fscanf(f, "%lld", &v);
    fclose(f); if (rc != 1) return false;
    out = v; return true;
}
static bool read_sysfs_string(const std::string &path, std::string &out) {
    FILE *f = fopen(path.c_str(), "r"); if (!f) return false;
    char buf[256]; size_t n = fread(buf,1,sizeof(buf)-1,f); fclose(f);
    if (!n) {
    return false;
	}
	buf[n] = '\0';
    while (n && (buf[n-1]=='\n'||buf[n-1]=='\r'||buf[n-1]==' '||buf[n-1]=='\t')) buf[--n]='\0';
    out.assign(buf); return true;
}
static std::string pretty_thermal_label(const std::string &type) {
    // cpuN[-...] -> "CPU N" or just "CPU" if no index
    if (type.rfind("cpu", 0) == 0) {
        size_t i = 3; std::string num;
        while (i < type.size() && std::isdigit((unsigned char)type[i])) { num.push_back(type[i++]); }
        return num.empty() ? std::string("CPU") : std::string("CPU ") + num;
    }
    // gpuN/maliN -> "GPU N" (or just "GPU")
    if (type.rfind("gpu", 0) == 0 || type.rfind("mali", 0) == 0) {
        size_t i = (type.rfind("gpu", 0) == 0) ? 3 : 4; std::string num;
        while (i < type.size() && std::isdigit((unsigned char)type[i])) { num.push_back(type[i++]); }
        return num.empty() ? std::string("GPU") : std::string("GPU ") + num;
    }
    if (type.rfind("ddr", 0) == 0 || type.rfind("mem", 0) == 0) return "Memory";
    if (type == "soc-thermal" || type == "soc_thermal") return "SoC";
    return type;
}
static bool read_longlong(const std::string &path, long long &out) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[istatserver][RAPL] open failed: %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        fprintf(stderr, "[istatserver][RAPL] read failed: %s: %s\n", path.c_str(), (n < 0) ? strerror(errno) : "short read");
        close(fd);
        return false;
    }
    buf[n] = '\0';
    close(fd);
    // Trim trailing newlines/spaces
    char *end = buf + n;
    while (end > buf && (*(end-1) == '\n' || *(end-1) == '\r' || *(end-1) == ' ' || *(end-1) == '\t')) { --end; }
    *end = '\0';
    errno = 0;
    char *ep = nullptr;
    long long v = strtoll(buf, &ep, 10);
    if (errno != 0 || ep == buf) {
        fprintf(stderr, "[istatserver][RAPL] parse failed: %s: '%s'\n", path.c_str(), buf);
        return false;
    }
    out = v;
    return true;
}

static bool read_string(const std::string &path, std::string &out) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return false;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    if (!n) return false;
    buf[n] = '\0';
    while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    out.assign(buf);
    return true;
}
#endif

#ifdef HAVE_LIBSENSORS
#if SENSORS_API_VERSION >= 0x0400 /* libsensor 4 */

void StatsSensors::update_libsensors(long long sampleID)
{
    int a = 0, b, c, num = 0;
    const sensors_chip_name *chip;
    const sensors_feature *features;
    const sensors_subfeature *subfeatures;

    while ((chip = sensors_get_detected_chips(NULL, &a))) {
        b = 0;
        while ((features = sensors_get_features(chip, &b))) {
            c = 0;
            while ((subfeatures = sensors_get_all_subfeatures(chip, features, &c))) {
                // Only process readable inputs we care about
                if (!(subfeatures->flags & SENSORS_MODE_R)) continue;

                if (subfeatures->type == SENSORS_SUBFEATURE_FAN_INPUT  ||
                    subfeatures->type == SENSORS_SUBFEATURE_CURR_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_POWER_INPUT||
                    subfeatures->type == SENSORS_SUBFEATURE_IN_INPUT   ||
                    subfeatures->type == SENSORS_SUBFEATURE_VID        ||
                    subfeatures->type == SENSORS_SUBFEATURE_TEMP_INPUT) {

                    char *label = sensors_get_label(chip, features); // must free()
                    if (!label) continue;

                    std::stringstream key;
                    key << label << "_" << sensorType(subfeatures->type);

                    if (createSensor(key.str()) == 1) {
                        for (std::vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur) {
                            if ((*cur).key == key.str()) {
                                (*cur).method = 1;                // libsensors
                                (*cur).chip   = chip->addr;
                                (*cur).sensor = features->number;
                                (*cur).label  = std::string(label);
                                (*cur).kind   = sensorType(subfeatures->type);
                                break;
                            }
                        }
                    }

                    double value = 0.0;
                    if (sensors_get_value(chip, subfeatures->number, &value) == 0) {
                        processSensor(key.str(), sampleID, value);
                    }
                    free(label);
                    ++num;
                }
            }
        }
    }
}

int StatsSensors::sensorType(int type)
{
    switch (type) {
        case SENSORS_SUBFEATURE_TEMP_INPUT:  return 1; // temperature
        case SENSORS_SUBFEATURE_FAN_INPUT:   return 2; // fan
        case SENSORS_SUBFEATURE_IN_INPUT:
        case SENSORS_SUBFEATURE_VID:         return 3; // voltage
        case SENSORS_SUBFEATURE_CURR_INPUT:  return 4; // current
        case SENSORS_SUBFEATURE_POWER_INPUT: return 5; // power
        default:                             return 0; // unknown/other
    }
}
#elif SENSORS_API_VERSION < 0x0400 /* libsensor 3 and earlier */

void StatsSensors::update_libsensors(long long sampleID)
{
	int a, b, c, num;
	const sensors_chip_name * chip;
	const sensors_feature_data * features;

	a = num = 0;

	while ((chip = sensors_get_detected_chips(&a)))
	{
		b = c = 0;

		while ((features = sensors_get_all_features(*chip, &b, &c)))
		{
			if ((!memcmp(features->name, "fan", 3) && features->name[4]=='\0') || (!memcmp(features->name, "temp", 3) && features->name[5]=='\0')){
				stringstream key;
				key << num;

				if(createSensor(key.str()) == 1)
				{
					for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
					{
						if((*cur).key == key.str())
						{
							(*cur).method = 1;
							(*cur).chip = chip->addr;
							(*cur).sensor = features->number;
							if(!memcmp(features->name, "fan", 3) && features->name[4]=='\0')
								(*cur).kind = 2;
							else
								(*cur).kind = 0;

							char *label;
							sensors_get_label(*chip, (*cur).sensor, &label);
							(*cur).label = string(label);
						}
					}
				}	

				double value;
				sensors_get_feature(*chip, features->number, &value);

				processSensor(key.str(), sampleID, value);
				
				num++;
			}
		}
	}
}
#endif
#endif

#ifdef HAVE_LIBSENSORS
void StatsSensors::init_libsensors()
{
#if SENSORS_API_VERSION >= 0x0400 /* libsensor 4 */
		sensors_init(NULL);
		libsensors_ready = true;
#else
	FILE *fp;

	if ((fp = fopen("/etc/sensors.conf", "r")) == NULL)
		return;
	
	if (sensors_init(fp) != 0) {
		libsensors_ready = false;
		fclose(fp);
	}
	libsensors_ready = true;
#endif
}
#endif

void StatsSensors::init_dev_cpu()
{
#if defined(HAVE_SYSCTLBYNAME)
	int x;
	for(x=0;x<32;x++)
	{
		stringstream key;
		key << "dev.cpu." << x << ".temperature";

		size_t len;
        long buf;
        len = sizeof(buf);

        if (sysctlbyname(key.str().c_str(), &buf, &len, NULL, 0) >= 0)
        {
        	createSensor(key.str());
			for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).key == key.str())
				{
					stringstream label;
					label << "CPU " << x;
					(*cur).label = label.str();
					(*cur).method = 2;
					(*cur).kind = 0;
				}
			}
        }
	}
#endif
}

void StatsSensors::init_acpi_thermal()
{
#if defined(HAVE_SYSCTLBYNAME)
	int x;
	for(x=0;x<32;x++)
	{
		stringstream key;
		key << "hw.acpi.thermal.tz" << x << ".temperature";

		size_t len;
        long buf;
        len = sizeof(buf);

        if (sysctlbyname(key.str().c_str(), &buf, &len, NULL, 0) >= 0)
        {
        	createSensor(key.str());
			for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).key == key.str())
				{
					stringstream label;
					label << "Thermal Zone " << x;
					(*cur).label = label.str();
					(*cur).method = 4;
					(*cur).kind = 0;
				}
			}
        }
	}
#endif
}

void StatsSensors::init_acpi_freq()
{
#if defined(HAVE_SYSCTLBYNAME)
	int x;
	for(x=0;x<32;x++)
	{
		stringstream key;
		key << "dev.cpu." << x << ".freq";

		size_t len;
        long buf;
        len = sizeof(buf);

        if (sysctlbyname(key.str().c_str(), &buf, &len, NULL, 0) >= 0)
        {
        	createSensor(key.str());
			for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).key == key.str())
				{
					stringstream label;
					label << "CPU " << x << " Frequency";
					(*cur).label = label.str();
					(*cur).method = 5;
					(*cur).kind = 8;
				}
			}
        }
	}
#endif
}

void StatsSensors::init_qnap()
{
	int  temp;

	char *systempfile=(char*)"/proc/tsinfo/systemp";
	FILE *systempfp;
	if ((systempfp=fopen(systempfile, "r"))==NULL) {
		return;
	}
	if (fscanf(systempfp, "%d", &temp)!=1) {
		fclose(systempfp);
		return;
	}
	fclose(systempfp);

	createSensor("qnap");
	
	for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
	{
		(*cur).label = "Temperature";
		(*cur).method = 3;
		(*cur).kind = 0;
	}
}

void StatsSensors::update_qnap(long long sampleID)
{
	char *systempfile=(char*)"/proc/tsinfo/systemp";
	FILE *systempfp;
	int systemp;
	if ((systempfp=fopen(systempfile, "r"))==NULL)
		return;
	else  {
		fseek(systempfp, 0l, 0);
		if (fscanf(systempfp, "%d", &systemp) == 1){
			processSensor("qnap", sampleID, systemp);
		}
		fclose(systempfp);
	}
}

void StatsSensors::update_dev_cpu(long long sampleID)
{
#if defined(HAVE_SYSCTLBYNAME)
	if(_items.size() == 0)
		return;
	
	for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
	{
		if((*cur).method != 2)
			continue;

		size_t len;
        int buf;
        len = sizeof(buf);

        if (sysctlbyname((*cur).key.c_str(), &buf, &len, NULL, 0) >= 0)
        {
        	double value = (buf - 2732) / 10.0f;
   			processSensor((*cur).key, sampleID, value);
        }
	}
#endif
}

void StatsSensors::update_acpi_thermal(long long sampleID)
{
#if defined(HAVE_SYSCTLBYNAME)
	if(_items.size() == 0)
		return;
	
	for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
	{
		if((*cur).method != 4)
			continue;

		size_t len;
        int buf;
        len = sizeof(buf);

        if (sysctlbyname((*cur).key.c_str(), &buf, &len, NULL, 0) >= 0)
        {
        	double value = (buf - 2732) / 10.0f;
   			processSensor((*cur).key, sampleID, value);
        }
	}
#endif
}

void StatsSensors::update_acpi_freq(long long sampleID)
{
#if defined(HAVE_SYSCTLBYNAME)
	if(_items.size() == 0)
		return;
	
	for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
	{
		if((*cur).method != 5)
			continue;

		size_t len;
        int buf;
        len = sizeof(buf);

        if (sysctlbyname((*cur).key.c_str(), &buf, &len, NULL, 0) >= 0)
        {
   			processSensor((*cur).key, sampleID, (double)buf);
        }
	}
#endif
}
void StatsSensors::init_sysfs_thermal() {
#if defined(__linux__)
    // First pass: count zones per type
    std::map<std::string,int> type_counts;
    DIR *d1 = opendir("/sys/class/thermal");
    if (d1) {
        struct dirent *e;
        while ((e = readdir(d1))) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
            std::string base = std::string("/sys/class/thermal/") + e->d_name;
            std::string type;
            if (!read_sysfs_string(base + "/type", type)) continue;
            type_counts[type]++;
        }
        closedir(d1);
    }

    // Second pass: create sensors with stable keys and neat labels
    DIR *dir = opendir("/sys/class/thermal"); if (!dir) return;
    std::map<std::string,int> type_seen; // running index per type
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;
        std::string base = std::string("/sys/class/thermal/") + ent->d_name;
        std::string type; long long raw;
        if (!read_sysfs_string(base + "/type", type)) continue;
        if (!read_sysfs_ll(base + "/temp", raw)) continue;

        int zone = atoi(ent->d_name + 12); // keep key stable on zone id
        std::string key = "thermal:" + type + ":" + std::to_string(zone);

        // Build neat label: pretty base + optional index when multiple of same type exist
        std::string base_label = pretty_thermal_label(type);
        int &seen = type_seen[type];
        int total = type_counts[type];
        std::string label = base_label;
        bool ends_with_digit = !base_label.empty() && std::isdigit((unsigned char)base_label.back());
        if (total > 1 && !ends_with_digit) {
            label += " " + std::to_string(seen);
        }
        seen++;

        if (createSensor(key) == 1) {
            for (auto &s : _items) if (s.key == key) {
                s.method = 10; // Linux sysfs thermal
                s.kind   = 0;  // temperature
                s.label  = label;
                break;
            }
        }
    }
    closedir(dir);
#endif
}
void StatsSensors::update_sysfs_thermal(long long sampleID) {
#if defined(__linux__)
    DIR *dir = opendir("/sys/class/thermal"); if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        if (strncmp(ent->d_name, "thermal_zone", 12) != 0) continue;
        std::string base = std::string("/sys/class/thermal/")+ent->d_name;
        std::string type; long long raw = 0;
        if (!read_sysfs_string(base + "/type", type)) continue;
        if (!read_sysfs_ll(base + "/temp", raw)) continue;
        int zone = atoi(ent->d_name + 12);
        std::string key = "thermal:" + type + ":" + std::to_string(zone);
        processSensor(key, sampleID, (double)raw / 1000.0);
    }
    closedir(dir);
#endif
}

void StatsSensors::init_sysfs_cpufreq() {
#if defined(__linux__)
    bool any = false;
    // Prefer policies
    for (int p = 0; p < 128; ++p) {
        char test[256]; snprintf(test,sizeof(test),"/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq",p);
        if (access(test, R_OK) != 0) continue;

        // Try to map CPUs in this policy
        std::string rel; read_sysfs_string(
            (std::string("/sys/devices/system/cpu/cpufreq/policy")+std::to_string(p)+"/related_cpus"), rel);
        std::vector<int> cpus;
        if (!rel.empty()) {
            const char *s = rel.c_str();
            while (*s) {
                while (*s == ' ' || *s == '\n' || *s == '\t') {
 				   ++s;
				}
				if (!*s) {
    				break;
				}
                int a = strtol(s,(char**)&s,10), b=a;
                if (*s=='-') { ++s; b = strtol(s,(char**)&s,10); }
                for (int i=a;i<=b;i++) cpus.push_back(i);
            }
        }
        if (cpus.empty()) {
            std::string key = "cpufreq:policy"+std::to_string(p);
            if (createSensor(key)==1) for (auto &s:_items) if (s.key==key) {
                s.method=11; s.kind=8; s.label="CPU policy "+std::to_string(p)+" Frequency";
            }
            any = true; continue;
        }
        for (int cpu : cpus) {
            std::string key = "cpu" + std::to_string(cpu) + "_freq";
            if (createSensor(key)==1) for (auto &s:_items) if (s.key==key) {
                s.method=11; s.kind=8; s.label="CPU "+std::to_string(cpu)+" Frequency";
            }
            any = true;
        }
    }
    // Fallback per-CPU directories
    if (!any) {
        DIR *dir = opendir("/sys/devices/system/cpu"); if (!dir) return;
        struct dirent *e;
        while ((e = readdir(dir))) {
            if (strncmp(e->d_name,"cpu",3)!=0 || !isdigit((unsigned char)e->d_name[3])) continue;
            int cpu = atoi(e->d_name+3);
            std::string path = std::string("/sys/devices/system/cpu/")+e->d_name+"/cpufreq/scaling_cur_freq";
            if (access(path.c_str(), R_OK) != 0) continue;
            std::string key = "cpu" + std::to_string(cpu) + "_freq";
            if (createSensor(key)==1) for (auto &s:_items) if (s.key==key) {
                s.method=11; s.kind=8; s.label="CPU "+std::to_string(cpu)+" Frequency";
            }
        }
        closedir(dir);
    }
#endif
}
void StatsSensors::update_sysfs_cpufreq(long long sampleID) {
#if defined(__linux__)
    // per-CPU updates
    for (int cpu=0; cpu<256; ++cpu) {
        std::string key = "cpu" + std::to_string(cpu) + "_freq";
        bool have=false; for (auto &s:_items) if (s.key==key){have=true;break;}
        if (!have) continue;
        char p1[256]; snprintf(p1,sizeof(p1),"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",cpu);
        long long khz=0;
        if (!read_sysfs_ll(p1, khz)) {
            char p2[256]; snprintf(p2,sizeof(p2),"/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq",cpu);
            if (!read_sysfs_ll(p2, khz)) continue;
        }
        double mhz = khz / 1000.0;
        processSensor(key, sampleID, mhz);
    }
    // policy updates
    for (int p=0; p<128; ++p) {
        std::string key = "cpufreq:policy"+std::to_string(p);
        bool have=false; for (auto &s:_items) if (s.key==key){have=true;break;}
        if (!have) continue;
        char path[256]; snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq",p);
        long long khz=0; if (!read_sysfs_ll(path,khz)) continue;
        processSensor(key, sampleID, khz/1000.0);
    }
#endif
}
static bool find_gpu_devfreq_cur(std::string &out) {
    long long tmp;
    if (read_sysfs_ll("/sys/class/devfreq/ffe40000.gpu/cur_freq", tmp)) { out = "/sys/class/devfreq/ffe40000.gpu/cur_freq"; return true; }
    DIR *d = opendir("/sys/class/devfreq"); if (!d) return false;
    struct dirent *de; 
    while ((de=readdir(d))) {
        if (de->d_name[0]=='.') continue;
        std::string cand = std::string("/sys/class/devfreq/")+de->d_name+"/cur_freq";
        if (read_sysfs_ll(cand, tmp)) { out=cand; closedir(d); return true; }
    }
    closedir(d); return false;
}
void StatsSensors::init_sysfs_devfreq_gpu() {
#if defined(__linux__)
    std::string path; long long hz;
    if (!find_gpu_devfreq_cur(path)) return;
    if (!read_sysfs_ll(path, hz)) return;
    std::string key = "gpu_freq";
    if (createSensor(key)==1) for (auto &s:_items) if (s.key==key) { s.method=12; s.kind=8; s.label="GPU Frequency"; }
#endif
}
void StatsSensors::update_sysfs_devfreq_gpu(long long sampleID) {
#if defined(__linux__)
    std::string path; if (!find_gpu_devfreq_cur(path)) return;
    long long hz=0; if (!read_sysfs_ll(path, hz)) return;
    processSensor("gpu_freq", sampleID, (double)hz / 1.0e6);
#endif
}

void StatsSensors::init_rapl() {
    const char *base = "/sys/class/powercap";
    DIR *d = opendir(base);
    if (!d) { perror("opendir /sys/class/powercap"); return; }
    struct dirent *de;
    while ((de = readdir(d))) {
        // fprintf(stderr, "[istatserver][RAPL] readdir: %s\n", de->d_name);
        if (strncmp(de->d_name, "intel-rapl:", 11) != 0) continue;

        std::string dir = std::string(base) + "/" + de->d_name;
        struct stat st{};
        if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        long long __probe_uj = 0;
        if (!read_longlong(dir + "/energy_uj", __probe_uj)) continue;
        // fprintf(stderr, "[istatserver][RAPL] probe OK: %s -> %lld\n", (dir + "/energy_uj").c_str(), (long long)__probe_uj);

        RaplDomain dom;
        dom.path = dir;
        if (!read_string(dir + "/name", dom.name)) dom.name = de->d_name;

        long long wrap = 0;
        if (read_longlong(dir + "/max_energy_range_uj", wrap)) dom.wrap_uj = wrap;

        dom.key = "rapl:" + dom.name;  // Stable sensor key

        if (createSensor(dom.key) == 1) {
            for (auto &s : _items) {
                if (s.key == dom.key) {
                    s.method = 13;               // New method id for RAPL
                    s.kind   = 5;               // Power (matches mapping)
                    s.label  = "CPU " + dom.name + " Power";
                }
            }
        }
        // fprintf(stderr, "[istatserver][RAPL] found: path=%s name=%s key=%s wrap=%lld\n",
        //         dir.c_str(), dom.name.c_str(), dom.key.c_str(), (long long)dom.wrap_uj);
        // Seed an initial value so the client lists the sensor right away
        processSensor(dom.key, 0 /*sampleID ignored here*/, 0.0);
        rapl_.push_back(dom);
    }
    // Optionally, keep a minimal summary log at the end:
    // fprintf(stderr, "[istatserver][RAPL] initialized %zu domains\n", rapl_.size());
    closedir(d);
}

void StatsSensors::update_rapl(long long sampleID) {
    if (rapl_.empty()) return;

    double now = get_current_time();  // you already use this elsewhere

    for (auto &dom : rapl_) {
        long long uj = 0;
        if (!read_longlong(dom.path + "/energy_uj", uj)) continue;

        if (dom.last_uj >= 0) {
            long long delta_uj = uj - dom.last_uj;
            if (delta_uj < 0 && dom.wrap_uj > 0) delta_uj += dom.wrap_uj;  // handle wrap
            double dt = now - dom.last_time;
            if (dt > 0.0 && delta_uj >= 0) {
                // µJ / s = µW → W
                double watts = (double)delta_uj / 1e6 / dt;
                // fprintf(stderr, "[istatserver][RAPL] %s: %.2f W (dU=%lld µJ, dt=%.3f s)\n",
                //         dom.key.c_str(), watts, (long long)delta_uj, dt);
                processSensor(dom.key, sampleID, watts);
            }
        }
        dom.last_uj = uj;
        dom.last_time = now;
    }
}

/*
Methods

1  = libsensors
2  = dev.cpu.(x).temperature (BSD)
3  = qnap
4  = hw.acpi.thermal.tz(x).temperature (BSD)
5  = dev.cpu.(x).freq (BSD)
10 = Linux sysfs thermal (/sys/class/thermal)
11 = Linux sysfs CPU freq (/sys/devices/system/cpu/.../cpufreq/scaling_cur_freq)
12 = Linux sysfs GPU freq (/sys/class/devfreq/ * /cur_freq)
13 = Linux Intel RAPL power (/sys/class/powercap/intel-rapl: * /energy_uj)

*/

void StatsSensors::init()
{
	_init();

#ifdef HAVE_LIBSENSORS
	init_libsensors();
#endif

	init_dev_cpu();
	init_qnap();
	init_acpi_thermal();
	init_acpi_freq();
	#if defined(__linux__)
		init_sysfs_thermal();
		init_sysfs_cpufreq();
		init_sysfs_devfreq_gpu();
		init_rapl();
	#endif
}

void StatsSensors::update(long long sampleID)
{
	#ifdef HAVE_LIBSENSORS
	if(libsensors_ready == true)
		update_libsensors(sampleID);
	#endif

	update_qnap(sampleID);
	update_dev_cpu(sampleID);
	update_acpi_thermal(sampleID);
	update_acpi_freq(sampleID);
	#if defined(__linux__)
		update_sysfs_thermal(sampleID);
		update_sysfs_cpufreq(sampleID);
		update_sysfs_devfreq_gpu(sampleID);
		update_rapl(sampleID);
	#endif

	#ifdef USE_SQLITE
	if(historyEnabled == true)
	{
		if(_items.size() > 0)
		{
			for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
			{
				if((*cur).recordedValueChanged)
				{
					string sql = "UPDATE sensor_limits SET low = ?, high = ? where uuid = ?";

					DatabaseItem dbItem = _database.databaseItem(sql);
					sqlite3_bind_double(dbItem._statement, 1, (*cur).lowestValue);
					sqlite3_bind_double(dbItem._statement, 2, (*cur).highestValue);
					sqlite3_bind_text(dbItem._statement, 3, (*cur).key.c_str(), -1, SQLITE_STATIC);
					databaseQueue.push_back(dbItem);
				}
			}
		}
	}
	#endif
}

int StatsSensors::createSensor(string key)
{
	if(_items.size() > 0)
	{
		for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
		{
				sensor_info sensor = *cur;
				if(sensor.key == key){
					return 0;
				}
		}
	}

	sensor_info item;
	item.key = key;
	item.lowestValue = -1;
	item.highestValue = 0;

	#ifdef USE_SQLITE
	if(historyEnabled == true)
	{
		string sql = "select * from sensor_limits where uuid = ?";
		DatabaseItem query = _database.databaseItem(sql);
		sqlite3_bind_text(query._statement, 1, key.c_str(), -1, SQLITE_STATIC);

		bool hasRow = false;
		while(query.next())
		{
			hasRow = true;
			item.lowestValue = query.doubleForColumn("low");
			item.highestValue = query.doubleForColumn("high");
		}

		if(!hasRow){
			string sql = "insert into sensor_limits (uuid) values(?)";
			DatabaseItem dbItem = _database.databaseItem(sql);
			sqlite3_bind_text(dbItem._statement, 1, key.c_str(), -1, SQLITE_STATIC);
			dbItem.executeUpdate();
		}

		int x;
		for(x=1;x<8;x++)
		{
			string table = databasePrefix + tableAtIndex(x);
			double sampleID = 0;
			if(samples[x].size() > 0)
				sampleID = samples[x][0].sampleID;

			string sql = "select * from " + table + " where sample >= @sample AND uuid = ? order by sample asc limit 602";
			DatabaseItem query = _database.databaseItem(sql);
			sqlite3_bind_double(query._statement, 1, sampleID - 602);
			sqlite3_bind_text(query._statement, 2, key.c_str(), -1, SQLITE_STATIC);

			while(query.next())
			{
				sensor_data sample;
				sample.value = query.doubleForColumn("value");
				sample.sampleID = (long long)query.doubleForColumn("sample");
				sample.time = query.doubleForColumn("time");
				item.samples[x].push_front(sample);
			}
		}
	}
#endif

	_items.insert(_items.begin(), item);	
	return 1;
}

void StatsSensors::processSensor(string key, long long sampleID, double value)
{
	if(ready == 0)
		return;

	for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
	{					
		if((*cur).key == key){
			sensor_data data;
			data.value = value;
			data.sampleID = sampleIndex[0].sampleID;
			data.time = sampleIndex[0].time;

			(*cur).samples[0].push_front(data);
			if ((*cur).samples[0].size() > HISTORY_SIZE)
				(*cur).samples[0].pop_back();

			bool changed = false;
   			(*cur).recordedValueChanged = false;
    
    		if(value > (*cur).highestValue){
        		(*cur).highestValue = value;
        		changed = true;
    		}
    
   			if(value < (*cur).lowestValue || (*cur).lowestValue == -1){
      			(*cur).lowestValue = value;
      			changed = true;
    		}
    
    		(*cur).recordedValueChanged = changed;
		}
	}
}

void StatsSensors::_init()
{	
	initShared();
	ready = 0;

	#ifdef USE_SQLITE
	if(historyEnabled == true)
	{
		databaseType = 1;
		databasePrefix = "sensors_";

		int x;
		for(x=1;x<8;x++)
		{
			string table = databasePrefix + tableAtIndex(x) + "_id";
			if(!_database.tableExists(table))
			{
				string sql = "create table " + table + " (sample double PRIMARY KEY NOT NULL, time double NOT NULL DEFAULT 0, empty integer NOT NULL DEFAULT 0)";		
				DatabaseItem dbItem = _database.databaseItem(sql);
				dbItem.executeUpdate();
			}

			table = databasePrefix + tableAtIndex(x);
			if(!_database.tableExists(table))
			{
				string sql = "create table " + table + " (sample double NOT NULL, time double NOT NULL DEFAULT 0, uuid varchar(255) NOT NULL, value double NOT NULL DEFAULT 0)";		
				DatabaseItem dbItem = _database.databaseItem(sql);
				dbItem.executeUpdate();
			}
		}

		string table = "sensor_limits";
		if(!_database.tableExists(table))
		{
			string sql = "create table sensor_limits (uuid varchar(255) NOT NULL, low double NOT NULL DEFAULT 0, high double NOT NULL DEFAULT 0)";		
			DatabaseItem dbItem = _database.databaseItem(sql);
			dbItem.executeUpdate();
		}
		loadPreviousSamples();
		fillGaps();
	}
	#endif
}

#ifdef USE_SQLITE
void StatsSensors::loadPreviousSamples()
{
	loadPreviousSamplesAtIndex(1);
	loadPreviousSamplesAtIndex(2);
	loadPreviousSamplesAtIndex(3);
	loadPreviousSamplesAtIndex(4);
	loadPreviousSamplesAtIndex(5);
	loadPreviousSamplesAtIndex(6);
	loadPreviousSamplesAtIndex(7);
}

void StatsSensors::loadPreviousSamplesAtIndex(int index)
{
	
	string table = databasePrefix + tableAtIndex(index) + "_id";
	double sampleID = sampleIdForTable(table);

	string sql = "select * from " + table + " where sample >= @sample order by sample asc limit 602";
	DatabaseItem query = _database.databaseItem(sql);
	sqlite3_bind_double(query._statement, 1, sampleID - 602);

	while(query.next())
	{
		sample_data sample;
		sample.sampleID = (long long)query.doubleForColumn("sample");
		sample.time = query.doubleForColumn("time");
		samples[index].insert(samples[index].begin(), sample);
	}
	if(samples[index].size() > 0)
	{
		sampleIndex[index].sampleID = samples[index][0].sampleID;
		sampleIndex[index].time = samples[index][0].time;
		sampleIndex[index].nextTime = sampleIndex[index].time + sampleIndex[index].interval;
	}
}

void StatsSensors::updateHistory()
{
	int x;
	for (x = 1; x < 8; x++)
	{
		if(sampleIndex[0].time >= sampleIndex[x].nextTime)
		{
			double now = get_current_time();
			double earlistTime = now - (HISTORY_SIZE * sampleIndex[x].interval);
			while(sampleIndex[x].nextTime < now)
			{
				sampleIndex[x].sampleID = sampleIndex[x].sampleID + 1;
				sampleIndex[x].time = sampleIndex[x].nextTime;
				sampleIndex[x].nextTime = sampleIndex[x].nextTime + sampleIndex[x].interval;

				if(sampleIndex[x].time < earlistTime)
					continue;

				if(_items.size() > 0)
				{
					for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
					{
						sensor_data sample = historyItemAtIndex(x, (*cur));

						(*cur).samples[x].push_front(sample);	
						if ((*cur).samples[x].size() > HISTORY_SIZE) (*cur).samples[x].pop_back();

						if(sample.empty)
							continue;

						string table = databasePrefix + tableAtIndex(x);
						string sql = "insert into " + table + " (sample, time, value, uuid) values(?, ?, ?, ?)";

						DatabaseItem dbItem = _database.databaseItem(sql);
						sqlite3_bind_double(dbItem._statement, 1, (double)sample.sampleID);
						sqlite3_bind_double(dbItem._statement, 2, sample.time);
						sqlite3_bind_double(dbItem._statement, 3, sample.value);
						sqlite3_bind_text(dbItem._statement, 4, (*cur).key.c_str(), -1, SQLITE_STATIC);
						databaseQueue.push_back(dbItem);
						//dbItem.executeUpdate();
					}
				}

				string table = databasePrefix + tableAtIndex(x) + "_id";
				string sql = "insert into " + table + " (empty, sample, time) values(?, ?, ?)";

				DatabaseItem dbItem = _database.databaseItem(sql);
				sqlite3_bind_int(dbItem._statement, 1, 0);
				sqlite3_bind_double(dbItem._statement, 2, (double)sampleIndex[x].sampleID);
				sqlite3_bind_double(dbItem._statement, 3, sampleIndex[x].time);
				databaseQueue.push_back(dbItem);
			}
		}
	}
}

sensor_data StatsSensors::historyItemAtIndex(int index, sensor_info item)
{
	sensor_data sample;
	double value = 0;

	std::deque<sensor_data> from = item.samples[sampleIndex[index].historyIndex];
	double minimumTime = sampleIndex[index].time - sampleIndex[index].interval;
	double maximumTime = sampleIndex[index].time;
	if(sampleIndex[index].historyIndex == 0)
		maximumTime += 0.99;

	int count = 0;
	if(from.size() > 0)
	{
		for (deque<sensor_data>::iterator cursample = from.begin(); cursample != from.end(); ++cursample)
		{
			if ((*cursample).time > maximumTime){
				continue;
			}

			if ((*cursample).time < minimumTime)
				break;

			value += (*cursample).value;
			count++;
		}
		if (count > 0 && value > 0)
		{
			value /= count;
		}
	}

	sample.value = value;
	sample.sampleID = sampleIndex[index].sampleID;
	sample.time = sampleIndex[index].time;
	sample.empty = false;
	if(count == 0)
		sample.empty = true;

	return sample;
}
#endif
