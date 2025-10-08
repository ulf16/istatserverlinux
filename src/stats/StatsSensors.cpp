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
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>

using namespace std;

#ifdef HAVE_LIBSENSORS
#if SENSORS_API_VERSION >= 0x0400 /* libsensor 4 */

#include <sensors/error.h>

const char* sensors_feature_type_name(int feature_type) {
    switch (feature_type) {
        case SENSORS_FEATURE_IN: return "in";
        case SENSORS_FEATURE_FAN: return "fan";
        case SENSORS_FEATURE_TEMP: return "temp";
        case SENSORS_FEATURE_POWER: return "power";
        case SENSORS_FEATURE_ENERGY: return "energy";
        case SENSORS_FEATURE_CURR: return "curr";
        case SENSORS_FEATURE_HUMIDITY: return "humidity";
        default: return "unknown";
    }
}

const char* sensors_subfeature_type_name(int subfeature_type) {
    switch (subfeature_type) {
        case SENSORS_SUBFEATURE_IN_INPUT: return "in_input";
        case SENSORS_SUBFEATURE_FAN_INPUT: return "fan_input";
        case SENSORS_SUBFEATURE_TEMP_INPUT: return "temp_input";
        case SENSORS_SUBFEATURE_POWER_INPUT: return "power_input";
        // Add other cases as necessary
        default: return "unknown";
    }
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

/*
string createKey(const sensors_chip_name *chip, const sensors_subfeature *subfeature) {
    char chipName[128];
    sensors_snprintf_chip_name(chipName, sizeof(chipName), chip);
    stringstream keyStream;
    keyStream << chipName << "_" << subfeature->type;
    return keyStream.str();
}
*/

/*void StatsSensors::update_libsensors(long long sampleID)
{
	int a, b, c, num;
	const sensors_chip_name * chip;
	const sensors_feature * features;
	const sensors_subfeature * subfeatures;

	a = num = 0;

	while ((chip = sensors_get_detected_chips(NULL, &a)))
	{
		b = 0;
		while ((features = sensors_get_features(chip, &b)))
		{
			c = 0;
			while ((subfeatures = sensors_get_all_subfeatures(chip, features, &c)))
			{
				if (subfeatures->type == SENSORS_SUBFEATURE_FAN_INPUT ||
					subfeatures->type == SENSORS_SUBFEATURE_CURR_INPUT ||
					subfeatures->type == SENSORS_SUBFEATURE_POWER_INPUT ||
					subfeatures->type == SENSORS_SUBFEATURE_IN_INPUT ||
					subfeatures->type == SENSORS_SUBFEATURE_VID ||
					subfeatures->type == SENSORS_SUBFEATURE_TEMP_INPUT)
				{

  //if (chip->bus == SENSORS_CHIP_NAME_BUS_ISA)
  //  printf ("%s-isa-%04x", chip->prefix, chip->addr);
  //else
   // printf ("%s\n", chip->prefix);
    //fflush(stdout);


					char *label = sensors_get_label(chip, features);
					stringstream key;
					key << label << "_" << sensorType(subfeatures->type);

					if(createSensor(key.str()) == 1)
					{
						for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
						{
							if((*cur).key == key.str())
							{
								(*cur).method = 1;
								(*cur).chip = chip->addr;
								(*cur).sensor = features->number;
								(*cur).label = string(label);
								(*cur).kind = sensorType(subfeatures->type);					
							}
						}
					}

					double value;
  					sensors_get_value(chip, subfeatures->number, &value);

					processSensor(key.str(), sampleID, value);

					free(label);

					num++;
				}
			}
		}
	}
}
*/

void StatsSensors::update_libsensors(long long sampleID)
{
    int a, b, c, num;
    const sensors_chip_name *chip;
    const sensors_feature *features;
    const sensors_subfeature *subfeatures;

    a = num = 0;
    while ((chip = sensors_get_detected_chips(NULL, &a)))
    {
        // Print chip information
        //char chip_name[128];
        //sensors_snprintf_chip_name(chip_name, sizeof(chip_name), chip);
        //printf("Chip: %s\n", chip_name);

        b = 0;
        while ((features = sensors_get_features(chip, &b)))
        {
            // Print feature information
            // printf("  Feature #%d (%s): %s\n", features->number, sensors_get_label(chip, features), sensors_feature_type_name(features->type));

            c = 0;
            while ((subfeatures = sensors_get_all_subfeatures(chip, features, &c)))
            {
                // Print subfeature information
                // printf("    Subfeature (%s): %s\n", sensors_subfeature_type_name(subfeatures->type), subfeatures->name);

                if (subfeatures->type == SENSORS_SUBFEATURE_FAN_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_CURR_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_POWER_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_IN_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_VID ||
                    subfeatures->type == SENSORS_SUBFEATURE_TEMP_INPUT)
                {
                    char *label = sensors_get_label(chip, features);
                    stringstream key;
                    key << label << "_" << sensorType(subfeatures->type);

                    if(createSensor(key.str()) == 1)
                    {
                        for (vector<sensor_info>::iterator cur = _items.begin(); cur != _items.end(); ++cur)
                        {
                            if((*cur).key == key.str())
                            {
                                (*cur).method = 1;
                                (*cur).chip = chip->addr;
                                (*cur).sensor = features->number;
                                (*cur).label = string(label);
                                (*cur).kind = sensorType(subfeatures->type);                            
                            }
                        }
                    }
                    double value;
                    sensors_get_value(chip, subfeatures->number, &value);
                    processSensor(key.str(), sampleID, value);
                    free(label);
                    num++;
                }
            }
        }
    }
}


/*
void updateSensorData(const sensors_chip_name *chip, const sensors_subfeature *subfeature) {
    double sensorValue;
    int result = sensors_get_value(chip, subfeature->number, &sensorValue);
    if (result == 0) { // Successfully read the sensor value
        string sensorKey = createKey(chip, subfeature);
        long long sampleID = getCurrentSampleID(); // This function needs to be defined or modified as per your application
        processSensor(sensorKey, sampleID, sensorValue);
    } else {
        cerr << "Failed to read sensor value: " << sensors_strerror(result) << endl;
    }
}
*/
/*
void StatsSensors::update_libsensors(long long sampleID) {
    int a = 0, b, c;
    const sensors_chip_name *chip;
    const sensors_feature *features;
    const sensors_subfeature *subfeatures;

    while ((chip = sensors_get_detected_chips(NULL, &a))) {
        b = 0;
        while ((features = sensors_get_features(chip, &b))) {
            c = 0;
            while ((subfeatures = sensors_get_all_subfeatures(chip, features, &c))) {
                if (subfeatures->type == SENSORS_SUBFEATURE_FAN_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_CURR_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_POWER_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_IN_INPUT ||
                    subfeatures->type == SENSORS_SUBFEATURE_VID ||
                    subfeatures->type == SENSORS_SUBFEATURE_TEMP_INPUT) {

                    if (subfeatures->flags & SENSORS_MODE_R) { // Check if the subfeature is readable
                        double value;
                        if (sensors_get_value(chip, subfeatures->number, &value) == 0) {
                            stringstream key;
                            char *label = sensors_get_label(chip, features);
                            key << label << "_" << sensorType(subfeatures->type);
                            free(label);  // Clean up label
                            //processSensor(key.str(), sampleID, value);
                            if (createSensor(key.str()) == 1) {
                                // Assuming createSensor populates _items with new sensor data
                            //    updateSensorData(chip, subfeatures);  // Process the sensor data
                            processSensor(key.str(), sampleID, value);
                            }
                        }
                    }
                }
            }
        }
    }
}
*/


/*int StatsSensors::sensorType(int type)
{
	switch(type)
	{
		case SENSORS_SUBFEATURE_FAN_INPUT:
			return 2;
		break;
		case SENSORS_SUBFEATURE_CURR_INPUT:
			return 4;
		break;
		case SENSORS_SUBFEATURE_POWER_INPUT:
			return 5;
		break;
		case SENSORS_SUBFEATURE_IN_INPUT:
		case SENSORS_SUBFEATURE_VID:
			return 3;
		break;
		default:
			return 0;
		break;
	}
	return 0;
}*/
int StatsSensors::sensorType(int type)
{
    switch(type)
    {
        case SENSORS_SUBFEATURE_FAN_INPUT:
            return 2;
        case SENSORS_SUBFEATURE_CURR_INPUT:
            return 4;
        case SENSORS_SUBFEATURE_POWER_INPUT:
            return 5;
        case SENSORS_SUBFEATURE_IN_INPUT:
        case SENSORS_SUBFEATURE_VID:
            return 3;
        case SENSORS_SUBFEATURE_TEMP_INPUT:
            return 1; // Ensure this is correctly handled
        default:
            return 0;
    }
    return 0;
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

static bool read_ll_file(const std::string &path, long long &out) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    char *end = buf;
    errno = 0;
    long long v = strtoll(buf, &end, 10);
    if (errno || end == buf) return false;
    out = v;
    return true;
}

void StatsSensors::init_sysfs_cpufreq() {
#if defined(__linux__)
    // Prefer per-policy paths (present with intel_pstate)
    bool any = false;
    for (int p = 0; p < 64; ++p) {
        char test[256];
        snprintf(test, sizeof(test),
                 "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq", p);
        struct stat st{};
        if (stat(test, &st) != 0) continue;

        // Determine which CPUs belong to this policy
        char rel_path[256];
        snprintf(rel_path, sizeof(rel_path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/related_cpus", p);
        std::string related;
        {
            FILE *f = fopen(rel_path, "r");
            if (f) {
                char buf[256]; size_t n = fread(buf,1,sizeof(buf)-1,f); fclose(f);
                buf[n] = '\0';
                related.assign(buf);
            }
        }
        // Parse CPU list line (e.g., "0 1" or "0-3")
        std::vector<int> cpus;
        if (!related.empty()) {
            // very small parser: split on space and dashes
            int a=-1,b=-1;
            const char *s = related.c_str();
            while (*s) {
                while (*s==' '||*s=='\n'||*s=='\t') ++s;
                if (!*s) break;
                a = strtol(s, (char**)&s, 10);
                if (*s=='-') { ++s; b = strtol(s,(char**)&s,10); }
                else b = a;
                for (int i=a;i<=b;i++) cpus.push_back(i);
            }
        }

        if (cpus.empty()) {
            // Fallback: just create one “policy p” sensor
            std::stringstream key, label;
            key   << "cpufreq:policy" << p;
            label << "CPU policy " << p << " Frequency";
            if (createSensor(key.str()) == 1) {
                for (auto &s : _items) if (s.key == key.str()) {
                    s.method = 7;   // new method id for Linux cpufreq
                    s.kind   = 8;   // “frequency” type in your UI mapping
                    s.label  = label.str();
                }
            }
            any = true;
            continue;
        }

        // Create one sensor per CPU in this policy
        for (int cpu : cpus) {
            std::stringstream key, label;
            key   << "cpufreq:cpu" << cpu;
            label << "CPU " << cpu << " Frequency";
            if (createSensor(key.str()) == 1) {
                for (auto &s : _items) if (s.key == key.str()) {
                    s.method = 7;
                    s.kind   = 8;
                    s.label  = label.str();
                }
            }
            any = true;
        }
    }

    // If no policy* paths, try per-CPU cpufreq directories
    if (!any) {
        for (int cpu = 0; cpu < 256; ++cpu) {
            char test[256];
            snprintf(test, sizeof(test),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
            struct stat st{};
            if (stat(test, &st) != 0) continue;

            std::stringstream key, label;
            key   << "cpufreq:cpu" << cpu;
            label << "CPU " << cpu << " Frequency";
            if (createSensor(key.str()) == 1) {
                for (auto &s : _items) if (s.key == key.str()) {
                    s.method = 7;
                    s.kind   = 8;
                    s.label  = label.str();
                }
            }
        }
    }
#endif
}

void StatsSensors::update_sysfs_cpufreq(long long sampleID) {
#if defined(__linux__)
    // Update per-CPU
    for (int cpu = 0; cpu < 256; ++cpu) {
        std::stringstream key;
        key << "cpufreq:cpu" << cpu;
        bool have = false;
        for (auto &s : _items) if (s.key == key.str()) { have = true; break; }
        if (!have) continue;

        char path[256];
        // Prefer per-policy mapping if exists: policyX often includes cpu list,
        // but for updating we can read per-cpu if available, else policy0
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
        long long khz = 0;
        if (!read_ll_file(path, khz)) {
            // fallback: policy of cpu (most systems: cpuN -> policyN or policy0)
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq", cpu);
            if (!read_ll_file(path, khz)) continue;
        }
        // Convert kHz -> MHz (guard against Hz outliers)
        double mhz = (khz > 100000) ? (khz / 1000.0) : (double)khz / 1000000.0;
        processSensor(key.str(), sampleID, mhz);
    }

    // Update policy-level fallbacks
    for (int p = 0; p < 64; ++p) {
        std::stringstream key;
        key << "cpufreq:policy" << p;
        bool have = false;
        for (auto &s : _items) if (s.key == key.str()) { have = true; break; }
        if (!have) continue;

        char path[256];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/policy%d/scaling_cur_freq", p);
        long long khz = 0;
        if (!read_ll_file(path, khz)) continue;
        double mhz = (khz > 100000) ? (khz / 1000.0) : (double)khz / 1000000.0;
        processSensor(key.str(), sampleID, mhz);
    }
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
                    s.method = 6;               // New method id for RAPL
                    s.kind   = 5;               // Power (matches your mapping)
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

1 = libsensors
2 = dev.cpu.(x).temperature
3 = qnap
4 = hw.acpi.thermal.tz(x).temperature"
5 = dev.cpu.(x).freq

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
	init_rapl();
	init_sysfs_cpufreq();
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
	update_rapl(sampleID);
	update_sysfs_cpufreq(sampleID);

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
