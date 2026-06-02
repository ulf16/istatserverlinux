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
#include <cmath>
#include <functional>

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

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <stdint.h>

typedef struct __IOHIDEventSystemClient *IOHIDEventSystemClientRef;
typedef struct __IOHIDServiceClient *IOHIDServiceClientRef;
typedef struct __IOHIDEvent *IOHIDEventRef;
typedef void *IOReportSubscriptionRef;

extern "C" IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
extern "C" void IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef client, CFDictionaryRef matching);
extern "C" CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef client);
extern "C" CFTypeRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef service, CFStringRef key);
extern "C" IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef service, int64_t type, int32_t options, int64_t timeout);
extern "C" double IOHIDEventGetFloatValue(IOHIDEventRef event, int32_t field);
extern "C" CFDictionaryRef IOReportCopyChannelsInGroup(CFStringRef group, CFStringRef subgroup, uint64_t a, uint64_t b, uint64_t c);
extern "C" IOReportSubscriptionRef IOReportCreateSubscription(CFTypeRef a, CFMutableDictionaryRef channels, CFMutableDictionaryRef *subscribedChannels, uint64_t b, CFTypeRef c);
extern "C" CFDictionaryRef IOReportCreateSamples(IOReportSubscriptionRef subscription, CFMutableDictionaryRef channels, CFTypeRef a);
extern "C" CFStringRef IOReportChannelGetGroup(CFDictionaryRef channel);
extern "C" CFStringRef IOReportChannelGetChannelName(CFDictionaryRef channel);
extern "C" CFStringRef IOReportChannelGetUnitLabel(CFDictionaryRef channel);
extern "C" int64_t IOReportSimpleGetIntegerValue(CFDictionaryRef channel, int32_t index);

typedef CFDictionaryRef (*IOReportCopyChannelsInGroupFunc)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
typedef IOReportSubscriptionRef (*IOReportCreateSubscriptionFunc)(CFTypeRef, CFMutableDictionaryRef, CFMutableDictionaryRef *, uint64_t, CFTypeRef);
typedef CFDictionaryRef (*IOReportCreateSamplesFunc)(IOReportSubscriptionRef, CFMutableDictionaryRef, CFTypeRef);
typedef CFStringRef (*IOReportChannelGetGroupFunc)(CFDictionaryRef);
typedef CFStringRef (*IOReportChannelGetChannelNameFunc)(CFDictionaryRef);
typedef CFStringRef (*IOReportChannelGetUnitLabelFunc)(CFDictionaryRef);
typedef int64_t (*IOReportSimpleGetIntegerValueFunc)(CFDictionaryRef, int32_t);

struct macos_ioreport_symbols
{
	bool attempted;
	bool ready;
	IOReportCopyChannelsInGroupFunc copyChannelsInGroup;
	IOReportCreateSubscriptionFunc createSubscription;
	IOReportCreateSamplesFunc createSamples;
	IOReportChannelGetGroupFunc channelGetGroup;
	IOReportChannelGetChannelNameFunc channelGetChannelName;
	IOReportChannelGetUnitLabelFunc channelGetUnitLabel;
	IOReportSimpleGetIntegerValueFunc simpleGetIntegerValue;
};

struct macos_ioreport_power_state
{
	bool attempted;
	bool ready;
	CFMutableDictionaryRef channels;
	IOReportSubscriptionRef subscription;
	double lastReadTime;
	double cpuJ;
	double gpuJ;
	double aneJ;
	double ramJ;
	double pciJ;
	bool hasCpu;
	bool hasGpu;
	bool hasAne;
	bool hasRam;
	bool hasPci;
};

#define IOHIDEventFieldBase(type) ((type) << 16)
static const int64_t kIOHIDEventTypeTemperature = 15;
static const int32_t kIOHIDEventFieldTemperatureLevel = IOHIDEventFieldBase(kIOHIDEventTypeTemperature);

struct macos_smc_key_info
{
	uint32_t dataSize;
	uint32_t dataType;
	uint8_t dataAttributes;
	uint8_t pad[3];
};

struct macos_smc_key_data
{
	uint32_t key;
	uint8_t vers[4];
	uint8_t pLimitData[16];
	uint8_t padding0[4];
	macos_smc_key_info keyInfo;
	uint8_t result;
	uint8_t status;
	uint8_t data8;
	uint8_t padding1;
	uint32_t data32;
	uint8_t bytes[32];
};

static uint32_t macos_fourcc(const char *key)
{
	return ((uint32_t)key[0] << 24) | ((uint32_t)key[1] << 16) | ((uint32_t)key[2] << 8) | (uint32_t)key[3];
}

static io_connect_t macos_smc_connection()
{
	static io_connect_t connection = 0;
	if(connection != 0)
		return connection;

	io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
	if(service == 0)
		return 0;

	if(IOServiceOpen(service, mach_task_self(), 0, &connection) != KERN_SUCCESS)
		connection = 0;
	IOObjectRelease(service);

	return connection;
}

static bool macos_smc_call(io_connect_t connection, macos_smc_key_data *input, macos_smc_key_data *output)
{
	size_t inputSize = sizeof(*input);
	size_t outputSize = sizeof(*output);
	return IOConnectCallStructMethod(connection, 2, input, inputSize, output, &outputSize) == KERN_SUCCESS;
}

static bool macos_smc_read_key(const char *key, macos_smc_key_data *output)
{
	io_connect_t connection = macos_smc_connection();
	if(connection == 0)
		return false;

	macos_smc_key_data input;
	memset(&input, 0, sizeof(input));
	memset(output, 0, sizeof(*output));
	input.key = macos_fourcc(key);
	input.data8 = 9;
	if(!macos_smc_call(connection, &input, output))
		return false;

	input.keyInfo = output->keyInfo;
	input.data8 = 5;
	memset(output, 0, sizeof(*output));
	if(!macos_smc_call(connection, &input, output))
		return false;
	output->keyInfo = input.keyInfo;

	return true;
}

static bool macos_smc_read_uint8(const char *key, uint8_t *value)
{
	macos_smc_key_data output;
	if(!macos_smc_read_key(key, &output) || output.keyInfo.dataSize < 1)
		return false;

	*value = output.bytes[0];
	return true;
}

static bool macos_smc_read_float(const char *key, double *value)
{
	macos_smc_key_data output;
	if(!macos_smc_read_key(key, &output))
		return false;

	if(output.keyInfo.dataSize == 4)
	{
		uint32_t bits = (uint32_t)output.bytes[0] | ((uint32_t)output.bytes[1] << 8) | ((uint32_t)output.bytes[2] << 16) | ((uint32_t)output.bytes[3] << 24);
		float f;
		memcpy(&f, &bits, sizeof(f));
		*value = (double)f;
		return true;
	}

	if(output.keyInfo.dataSize == 2)
	{
		uint16_t raw = ((uint16_t)output.bytes[0] << 8) | output.bytes[1];
		*value = (double)raw / 4.0;
		return true;
	}

	if(output.keyInfo.dataSize == 1)
	{
		*value = output.bytes[0];
		return true;
	}

	return false;
}

static const char *kPowermetricsKV = "/opt/istatserverlinux/var/powermetrics.kv";

static macos_ioreport_symbols &macos_ioreport()
{
	static macos_ioreport_symbols symbols = {};
	if(symbols.attempted)
		return symbols;

	symbols.attempted = true;
	symbols.copyChannelsInGroup = IOReportCopyChannelsInGroup;
	symbols.createSubscription = IOReportCreateSubscription;
	symbols.createSamples = IOReportCreateSamples;
	symbols.channelGetGroup = IOReportChannelGetGroup;
	symbols.channelGetChannelName = IOReportChannelGetChannelName;
	symbols.channelGetUnitLabel = IOReportChannelGetUnitLabel;
	symbols.simpleGetIntegerValue = IOReportSimpleGetIntegerValue;
	symbols.ready = symbols.copyChannelsInGroup != NULL
		&& symbols.createSubscription != NULL
		&& symbols.createSamples != NULL
		&& symbols.channelGetGroup != NULL
		&& symbols.channelGetChannelName != NULL
		&& symbols.channelGetUnitLabel != NULL
		&& symbols.simpleGetIntegerValue != NULL;

	return symbols;
}

static macos_ioreport_power_state &macos_ioreport_power()
{
	static macos_ioreport_power_state state = {};
	return state;
}

static string macos_cfstring(CFTypeRef value)
{
	if(value == NULL || CFGetTypeID(value) != CFStringGetTypeID())
		return "";

	char buffer[256];
	if(CFStringGetCString((CFStringRef)value, buffer, sizeof(buffer), kCFStringEncodingUTF8))
		return string(buffer);

	return "";
}

static bool macos_cfnumber_double(CFTypeRef value, double *out)
{
	if(value == NULL || CFGetTypeID(value) != CFNumberGetTypeID())
		return false;

	double result = 0.0;
	if(!CFNumberGetValue((CFNumberRef)value, kCFNumberDoubleType, &result))
		return false;

	*out = result;
	return true;
}

static string macos_sensor_key(const string &prefix, const string &label)
{
	string key = prefix;
	for(string::const_iterator cur = label.begin(); cur != label.end(); ++cur)
	{
		unsigned char c = (unsigned char)*cur;
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			key += (char)c;
		else
			key += '_';
	}
	return key;
}

static bool macos_has_suffix(const string &value, const string &suffix)
{
	return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static double macos_ioreport_energy_joules(double value, const string &unit)
{
	if(unit == "mJ")
		return value / 1e3;
	if(unit == "uJ")
		return value / 1e6;
	if(unit == "nJ")
		return value / 1e9;
	return 0.0;
}

static void macos_ioreport_note_power_channel(macos_ioreport_power_state &state, const string &channel)
{
	if(macos_has_suffix(channel, "CPU Energy"))
		state.hasCpu = true;
	else if(macos_has_suffix(channel, "GPU Energy"))
		state.hasGpu = true;
	else if(channel.rfind("ANE", 0) == 0 && macos_has_suffix(channel, "Energy"))
		state.hasAne = true;
	else if(channel.rfind("DRAM", 0) == 0 && macos_has_suffix(channel, "Energy"))
		state.hasRam = true;
	else if((channel.rfind("PCI", 0) == 0 || channel.rfind("apcie", 0) == 0) && macos_has_suffix(channel, "Energy"))
		state.hasPci = true;
}

static bool macos_parse_number_suffix(const string &label, const string &prefix, string *number)
{
	if(label.rfind(prefix, 0) != 0)
		return false;

	string suffix = label.substr(prefix.size());
	if(suffix.size() == 0)
		return false;

	for(string::const_iterator cur = suffix.begin(); cur != suffix.end(); ++cur)
	{
		if(*cur < '0' || *cur > '9')
			return false;
	}

	*number = suffix;
	return true;
}

static string macos_pretty_hid_temperature_label(const string &label)
{
	string n;
	if(macos_parse_number_suffix(label, "pACC MTR Temp Sensor", &n))
		return "CPU performance core " + n;
	if(macos_parse_number_suffix(label, "eACC MTR Temp Sensor", &n))
		return "CPU efficiency core " + n;
	if(macos_parse_number_suffix(label, "GPU MTR Temp Sensor", &n))
		return "GPU core " + n;
	if(macos_parse_number_suffix(label, "SOC MTR Temp Sensor", &n))
		return "SOC core " + n;
	if(macos_parse_number_suffix(label, "ANE MTR Temp Sensor", &n))
		return "Neural engine " + n;
	if(macos_parse_number_suffix(label, "ISP MTR Temp Sensor", &n))
		return "Image signal processor " + n;
	if(macos_parse_number_suffix(label, "PMGR SOC Die Temp Sensor", &n))
		return "Power manager die " + n;
	if(macos_parse_number_suffix(label, "PMU tdev", &n))
		return "Power management unit dev " + n;
	if(macos_parse_number_suffix(label, "PMU2 tdev", &n))
		return "Power management unit 2 dev " + n;
	if(macos_parse_number_suffix(label, "PMU tdie", &n))
		return "Power management unit die " + n;
	if(macos_parse_number_suffix(label, "PMU2 tdie", &n))
		return "Power management unit 2 die " + n;

	if(label == "NAND CH0 temp")
		return "NAND channel 0";
	if(label == "PMU tcal")
		return "Power management unit calibration";
	if(label == "PMU2 tcal")
		return "Power management unit 2 calibration";
	if(label == "gas gauge battery")
		return "Battery";

	return label;
}

static void set_macos_sensor_meta(vector<sensor_info> &items, const string &key, const string &label, int kind, int method)
{
	for(vector<sensor_info>::iterator cur = items.begin(); cur != items.end(); ++cur)
	{
		if((*cur).key == key)
		{
			(*cur).method = method;
			(*cur).kind = kind;
			(*cur).label = label;
			break;
		}
	}
}

static void enumerate_macos_hid_temperatures(const function<void(const string&, const string&, double)> &callback)
{
	IOHIDEventSystemClientRef client = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
	if(client == NULL)
		return;

	int pageValue = 0xff00;
	int usageValue = 5;
	CFNumberRef page = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pageValue);
	CFNumberRef usage = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usageValue);
	const void *keys[] = { CFSTR(kIOHIDPrimaryUsagePageKey), CFSTR(kIOHIDPrimaryUsageKey) };
	const void *values[] = { page, usage };
	CFDictionaryRef matching = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

	IOHIDEventSystemClientSetMatching(client, matching);
	CFArrayRef services = IOHIDEventSystemClientCopyServices(client);
	CFIndex count = services != NULL ? CFArrayGetCount(services) : 0;

	for(CFIndex i = 0; i < count; ++i)
	{
		IOHIDServiceClientRef service = (IOHIDServiceClientRef)CFArrayGetValueAtIndex(services, i);
		CFTypeRef productRef = IOHIDServiceClientCopyProperty(service, CFSTR(kIOHIDProductKey));
		string label = macos_cfstring(productRef);
		if(productRef != NULL)
			CFRelease(productRef);

		if(label.size() == 0)
			continue;

		IOHIDEventRef event = IOHIDServiceClientCopyEvent(service, kIOHIDEventTypeTemperature, 0, 0);
		if(event == NULL)
			continue;

		double value = IOHIDEventGetFloatValue(event, kIOHIDEventFieldTemperatureLevel);
		CFRelease(event);

		if(value >= 0.0 && value <= 150.0)
			callback(macos_sensor_key("applehidtemp:", label), macos_pretty_hid_temperature_label(label), value);
	}

	if(services != NULL)
		CFRelease(services);
	if(matching != NULL)
		CFRelease(matching);
	if(page != NULL)
		CFRelease(page);
	if(usage != NULL)
		CFRelease(usage);
	CFRelease(client);
}

static void init_macos_hid_temperature_sensors(vector<sensor_info> &items, const function<int(const string&)> &createSensor)
{
	enumerate_macos_hid_temperatures([&items, &createSensor](const string &key, const string &label, double value) {
		(void)value;
		createSensor(key);
		set_macos_sensor_meta(items, key, label, 0, 21);
	});
}

static void update_macos_hid_temperatures(long long sampleID, const function<int(const string&)> &createSensor, vector<sensor_info> &items, const function<void(const string&, long long, double)> &processSensor)
{
	enumerate_macos_hid_temperatures([sampleID, &createSensor, &items, &processSensor](const string &key, const string &label, double value) {
		if(createSensor(key) == 1)
			set_macos_sensor_meta(items, key, label, 0, 21);
		processSensor(key, sampleID, value);
	});
}

static void init_macos_smc_fan_sensors(vector<sensor_info> &items, const function<int(const string&)> &createSensor)
{
	uint8_t count = 0;
	if(!macos_smc_read_uint8("FNum", &count))
		return;

	for(uint8_t i = 0; i < count; ++i)
	{
		string key = "applesmc:fan" + std::to_string((int)i);
		createSensor(key);
		set_macos_sensor_meta(items, key, "Fan " + std::to_string((int)i), 2, 22);
	}
}

static void update_macos_smc_fans(long long sampleID, const function<void(const string&, long long, double)> &processSensor)
{
	uint8_t count = 0;
	if(!macos_smc_read_uint8("FNum", &count))
		return;

	for(uint8_t i = 0; i < count; ++i)
	{
		char key[5];
		snprintf(key, sizeof(key), "F%dAc", i);

		double rpm = 0.0;
		if(macos_smc_read_float(key, &rpm) && rpm >= 0.0)
			processSensor("applesmc:fan" + std::to_string((int)i), sampleID, rpm);
	}
}

static bool init_macos_ioreport_power()
{
	macos_ioreport_power_state &state = macos_ioreport_power();
	if(state.attempted)
		return state.ready;
	state.attempted = true;

	macos_ioreport_symbols &symbols = macos_ioreport();
	if(!symbols.ready)
		return false;

	CFDictionaryRef rawChannels = symbols.copyChannelsInGroup(CFSTR("Energy Model"), NULL, 0, 0, 0);
	if(rawChannels == NULL)
		return false;

	state.channels = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, rawChannels);
	CFRelease(rawChannels);
	if(state.channels == NULL)
		return false;

	CFMutableDictionaryRef subscribedChannels = NULL;
	state.subscription = symbols.createSubscription(NULL, state.channels, &subscribedChannels, 0, NULL);
	if(subscribedChannels != NULL)
		CFRelease(subscribedChannels);
	if(state.subscription == NULL)
	{
		CFRelease(state.channels);
		state.channels = NULL;
		return false;
	}

	CFDictionaryRef sample = symbols.createSamples(state.subscription, state.channels, NULL);
	if(sample != NULL)
	{
		CFArrayRef channels = (CFArrayRef)CFDictionaryGetValue(sample, CFSTR("IOReportChannels"));
		CFIndex count = channels != NULL && CFGetTypeID(channels) == CFArrayGetTypeID() ? CFArrayGetCount(channels) : 0;
		for(CFIndex i = 0; i < count; ++i)
		{
			CFDictionaryRef item = (CFDictionaryRef)CFArrayGetValueAtIndex(channels, i);
			if(macos_cfstring(symbols.channelGetGroup(item)) == "Energy Model")
				macos_ioreport_note_power_channel(state, macos_cfstring(symbols.channelGetChannelName(item)));
		}
		CFRelease(sample);
	}

	state.ready = true;
	return true;
}

static bool macos_ioreport_power_available()
{
	return init_macos_ioreport_power();
}

static void init_macos_ioreport_power_sensors(vector<sensor_info> &items, const function<int(const string&)> &createSensor)
{
	if(!init_macos_ioreport_power())
		return;

	struct sensor_def {
		const char *key;
		const char *label;
	};

	macos_ioreport_power_state &state = macos_ioreport_power();
	if(state.hasCpu)
	{
		createSensor("apple:cpu_power_w");
		set_macos_sensor_meta(items, "apple:cpu_power_w", "CPU Core Power", 5, 23);
	}
	if(state.hasGpu)
	{
		createSensor("apple:gpu_power_w");
		set_macos_sensor_meta(items, "apple:gpu_power_w", "GPU Core Power", 5, 23);
	}
	if(state.hasAne)
	{
		createSensor("apple:ane_power_w");
		set_macos_sensor_meta(items, "apple:ane_power_w", "Neural Engine Power", 5, 23);
	}
	if(state.hasRam)
	{
		createSensor("apple:ram_power_w");
		set_macos_sensor_meta(items, "apple:ram_power_w", "Memory Power", 5, 23);
	}
	if(state.hasPci)
	{
		createSensor("apple:pci_power_w");
		set_macos_sensor_meta(items, "apple:pci_power_w", "PCIe Power", 5, 23);
	}
}

static void update_macos_ioreport_power(long long sampleID, const function<void(const string&, long long, double)> &processSensor)
{
	if(!init_macos_ioreport_power())
		return;

	macos_ioreport_symbols &symbols = macos_ioreport();
	macos_ioreport_power_state &state = macos_ioreport_power();
	CFDictionaryRef sample = symbols.createSamples(state.subscription, state.channels, NULL);
	if(sample == NULL)
		return;

	double cpuJ = 0.0;
	double gpuJ = 0.0;
	double aneJ = 0.0;
	double ramJ = 0.0;
	double pciJ = 0.0;

	CFArrayRef channels = (CFArrayRef)CFDictionaryGetValue(sample, CFSTR("IOReportChannels"));
	CFIndex count = channels != NULL && CFGetTypeID(channels) == CFArrayGetTypeID() ? CFArrayGetCount(channels) : 0;
	for(CFIndex i = 0; i < count; ++i)
	{
		CFDictionaryRef item = (CFDictionaryRef)CFArrayGetValueAtIndex(channels, i);
		string group = macos_cfstring(symbols.channelGetGroup(item));
		if(group != "Energy Model")
			continue;

		string channel = macos_cfstring(symbols.channelGetChannelName(item));
		string unit = macos_cfstring(symbols.channelGetUnitLabel(item));
		double joules = macos_ioreport_energy_joules((double)symbols.simpleGetIntegerValue(item, 0), unit);
		if(joules <= 0.0)
			continue;

		if(macos_has_suffix(channel, "CPU Energy"))
			cpuJ = joules;
		else if(macos_has_suffix(channel, "GPU Energy"))
			gpuJ = joules;
		else if(channel.rfind("ANE", 0) == 0 && macos_has_suffix(channel, "Energy"))
			aneJ += joules;
		else if(channel.rfind("DRAM", 0) == 0 && macos_has_suffix(channel, "Energy"))
			ramJ += joules;
		else if((channel.rfind("PCI", 0) == 0 || channel.rfind("apcie", 0) == 0) && macos_has_suffix(channel, "Energy"))
			pciJ += joules;
	}
	CFRelease(sample);

	double now = get_current_time();
	if(state.lastReadTime > 0.0)
	{
		double elapsed = now - state.lastReadTime;
		if(elapsed > 0.0)
		{
			if(state.hasCpu && cpuJ >= state.cpuJ)
				processSensor("apple:cpu_power_w", sampleID, (cpuJ - state.cpuJ) / elapsed);
			if(state.hasGpu && gpuJ >= state.gpuJ)
				processSensor("apple:gpu_power_w", sampleID, (gpuJ - state.gpuJ) / elapsed);
			if(state.hasAne && aneJ >= state.aneJ)
				processSensor("apple:ane_power_w", sampleID, (aneJ - state.aneJ) / elapsed);
			if(state.hasRam && ramJ >= state.ramJ)
				processSensor("apple:ram_power_w", sampleID, (ramJ - state.ramJ) / elapsed);
			if(state.hasPci && pciJ >= state.pciJ)
				processSensor("apple:pci_power_w", sampleID, (pciJ - state.pciJ) / elapsed);
		}
	}

	state.lastReadTime = now;
	state.cpuJ = cpuJ;
	state.gpuJ = gpuJ;
	state.aneJ = aneJ;
	state.ramJ = ramJ;
	state.pciJ = pciJ;
}

static void init_macos_powermetrics_sensors(vector<sensor_info> &items, const function<int(const string&)> &createSensor)
{
	struct sensor_def {
		const char *key;
		const char *label;
		int kind;
		int method;
	};

	const sensor_def powerDefs[] = {
		{"apple:cpu_power_w", "CPU Package Power", 5, 20},
		{"apple:gpu_power_w", "GPU Package Power", 5, 20},
		{"apple:ane_power_w", "Neural Engine Power", 5, 20},
	};

	const sensor_def defs[] = {
		{"apple:e_cluster_mhz", "E-Cluster Frequency", 8, 20},
		{"apple:p_cluster_mhz", "P-Cluster Frequency", 8, 20},
		{"apple:gpu_mhz", "GPU Frequency", 8, 20},
	};

	if(!macos_ioreport_power_available())
	{
		for(size_t i = 0; i < sizeof(powerDefs) / sizeof(powerDefs[0]); ++i)
		{
			createSensor(powerDefs[i].key);
			set_macos_sensor_meta(items, powerDefs[i].key, powerDefs[i].label, powerDefs[i].kind, powerDefs[i].method);
		}
	}

	for (size_t i = 0; i < sizeof(defs) / sizeof(defs[0]); ++i)
	{
		createSensor(defs[i].key);
		set_macos_sensor_meta(items, defs[i].key, defs[i].label, defs[i].kind, defs[i].method);
	}
}

static void update_macos_powermetrics(long long sampleID, const function<void(const string&, long long, double)> &processSensor)
{
	static double next_allowed = 0.0;
	double now = get_current_time();
	if (now < next_allowed)
		return;
	next_allowed = now + 10.0;

	double cpu_power_w = NAN;
	double gpu_power_w = NAN;
	double ane_power_w = NAN;
	double e_mhz = NAN;
	double p_mhz = NAN;
	double gpu_mhz = NAN;
	int thermal_pressure = -1;

	FILE *fp = fopen(kPowermetricsKV, "r");
	if (fp == NULL)
		return;

	char line[256];
	while (fgets(line, sizeof(line), fp))
	{
		char *s = line;
		while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
			++s;

		if (strncmp(s, "E=", 2) == 0)
		{
			double value = strtod(s + 2, NULL);
			if (value > 0)
				e_mhz = value;
		}
		else if (strncmp(s, "P=", 2) == 0)
		{
			double value = strtod(s + 2, NULL);
			if (value > 0)
				p_mhz = value;
		}
		else if (strncmp(s, "GPUF=", 5) == 0)
		{
			double value = strtod(s + 5, NULL);
			if (value > 0)
				gpu_mhz = value;
		}
		else if (strncmp(s, "CPUW=", 5) == 0)
		{
			double value = strtod(s + 5, NULL);
			if (value >= 0)
				cpu_power_w = value;
		}
		else if (strncmp(s, "GPUW=", 5) == 0)
		{
			double value = strtod(s + 5, NULL);
			if (value >= 0)
				gpu_power_w = value;
		}
		else if (strncmp(s, "ANEW=", 5) == 0)
		{
			double value = strtod(s + 5, NULL);
			if (value >= 0)
				ane_power_w = value;
		}
		else if (strncmp(s, "TP=", 3) == 0)
		{
			long value = strtol(s + 3, NULL, 10);
			if (value >= 0 && value <= 3)
				thermal_pressure = (int)value;
		}
	}
	fclose(fp);

	if (std::isnan(ane_power_w))
		ane_power_w = 0.0;

	if (!macos_ioreport_power_available() && !std::isnan(cpu_power_w))
		processSensor("apple:cpu_power_w", sampleID, cpu_power_w);
	if (!macos_ioreport_power_available() && !std::isnan(gpu_power_w))
		processSensor("apple:gpu_power_w", sampleID, gpu_power_w);
	if (!macos_ioreport_power_available() && !std::isnan(ane_power_w))
		processSensor("apple:ane_power_w", sampleID, ane_power_w);
	if (!std::isnan(e_mhz))
		processSensor("apple:e_cluster_mhz", sampleID, e_mhz);
	if (!std::isnan(p_mhz))
		processSensor("apple:p_cluster_mhz", sampleID, p_mhz);
	if (!std::isnan(gpu_mhz))
		processSensor("apple:gpu_mhz", sampleID, gpu_mhz);
	(void)thermal_pressure;
}

static bool macos_copy_agx_performance_statistics(CFDictionaryRef *statsOut)
{
	*statsOut = NULL;

	io_iterator_t iterator = IO_OBJECT_NULL;
#if defined(MAC_OS_VERSION_12_0)
	mach_port_t mainPort = kIOMainPortDefault;
#else
	mach_port_t mainPort = kIOMasterPortDefault;
#endif
	kern_return_t kr = IOServiceGetMatchingServices(mainPort, IOServiceMatching("IOAccelerator"), &iterator);
	if(kr != KERN_SUCCESS)
		return false;

	bool found = false;
	io_object_t service;
	while((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL)
	{
		CFTypeRef className = IORegistryEntryCreateCFProperty(service, CFSTR("IOClass"), kCFAllocatorDefault, 0);
		string ioClass = macos_cfstring(className);
		if(className != NULL)
			CFRelease(className);

		bool isAgx = ioClass.find("AGXAccelerator") != string::npos;
		if(isAgx)
		{
			CFTypeRef stats = IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
			if(stats != NULL && CFGetTypeID(stats) == CFDictionaryGetTypeID())
			{
				*statsOut = (CFDictionaryRef)stats;
				found = true;
				IOObjectRelease(service);
				break;
			}
			if(stats != NULL)
				CFRelease(stats);
		}

		IOObjectRelease(service);
	}
	IOObjectRelease(iterator);
	return found;
}

static bool macos_agx_stat(CFDictionaryRef stats, CFStringRef key, double *out)
{
	return macos_cfnumber_double(CFDictionaryGetValue(stats, key), out);
}

static void init_macos_agx_gpu_sensors(vector<sensor_info> &items, const function<int(const string&)> &createSensor)
{
	CFDictionaryRef stats = NULL;
	if(!macos_copy_agx_performance_statistics(&stats))
		return;
	CFRelease(stats);

	struct sensor_def {
		const char *key;
		const char *label;
		int kind;
	};

	const sensor_def defs[] = {
		{"apple:gpu_load_percent", "GPU Load", 6},
		{"apple:gpu_renderer_percent", "GPU Renderer", 6},
		{"apple:gpu_tiler_percent", "GPU Tiler", 6},
		{"apple:gpu_memory_used_bytes", "GPU Memory Used", 7},
		{"apple:gpu_memory_allocated_bytes", "GPU Memory Allocated", 7},
	};

	for(size_t i = 0; i < sizeof(defs) / sizeof(defs[0]); ++i)
	{
		createSensor(defs[i].key);
		set_macos_sensor_meta(items, defs[i].key, defs[i].label, defs[i].kind, 24);
	}
}

static void update_macos_agx_gpu_stats(long long sampleID, const function<void(const string&, long long, double)> &processSensor)
{
	CFDictionaryRef stats = NULL;
	if(!macos_copy_agx_performance_statistics(&stats))
		return;

	double value = 0.0;
	if(macos_agx_stat(stats, CFSTR("Device Utilization %"), &value))
		processSensor("apple:gpu_load_percent", sampleID, value);
	if(macos_agx_stat(stats, CFSTR("Renderer Utilization %"), &value))
		processSensor("apple:gpu_renderer_percent", sampleID, value);
	if(macos_agx_stat(stats, CFSTR("Tiler Utilization %"), &value))
		processSensor("apple:gpu_tiler_percent", sampleID, value);
	if(macos_agx_stat(stats, CFSTR("In use system memory"), &value))
		processSensor("apple:gpu_memory_used_bytes", sampleID, value);
	if(macos_agx_stat(stats, CFSTR("Alloc system memory"), &value))
		processSensor("apple:gpu_memory_allocated_bytes", sampleID, value);

	CFRelease(stats);
}
#endif

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
#if defined(__linux__)
static vector<string> list_gpu_devfreq_dirs() {
    vector<string> dirs;
    long long tmp;
    if (read_sysfs_ll("/sys/class/devfreq/ffe40000.gpu/cur_freq", tmp))
        dirs.push_back("/sys/class/devfreq/ffe40000.gpu");

    DIR *d = opendir("/sys/class/devfreq"); if (!d) return dirs;
    struct dirent *de; 
    while ((de=readdir(d))) {
        if (de->d_name[0]=='.') continue;
        std::string dir = std::string("/sys/class/devfreq/")+de->d_name;
        std::string cand = dir + "/cur_freq";
        if (read_sysfs_ll(cand, tmp)) {
            bool exists = false;
            for (const string &known : dirs) if (known == dir) { exists = true; break; }
            if (!exists) dirs.push_back(dir);
        }
    }
    closedir(d); return dirs;
}

static string devfreq_gpu_label(const string &dir, size_t index) {
    string name;
    if (read_sysfs_string(dir + "/name", name) && !name.empty())
        return name + " GPU";

    size_t slash = dir.find_last_of('/');
    string base = slash == string::npos ? dir : dir.substr(slash + 1);
    if (base.find("gpu") != string::npos || base.find("mali") != string::npos)
        return index == 0 ? "GPU" : "GPU " + std::to_string(index);
    return index == 0 ? "GPU" : "GPU " + std::to_string(index);
}

static vector<string> drm_gpu_memory_files() {
    vector<string> files;
    const char *base = "/sys/class/drm";
    DIR *d = opendir(base); if (!d) return files;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "card", 4) != 0) continue;
        string dir = string(base) + "/" + de->d_name + "/device";
        const char *names[] = {
            "mem_info_vram_used",
            "mem_info_gtt_used",
            "gpu_busy_percent",
        };
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            long long tmp = 0;
            string path = dir + "/" + names[i];
            if (read_sysfs_ll(path, tmp))
                files.push_back(path);
        }
    }
    closedir(d);
    return files;
}

static vector<string> i915_debugfs_frequency_files() {
    vector<string> files;
    const char *base = "/sys/kernel/debug/dri";
    DIR *d = opendir(base);
    if (!d) return files;

    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        string path = string(base) + "/" + de->d_name + "/gt0/frequency";
        struct stat st;
        if (stat(path.c_str(), &st) == 0)
            files.push_back(path);
    }
    closedir(d);
    return files;
}

static bool read_i915_debugfs_frequency_mhz(const string &path, double &mhz) {
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return false;

    char line[256];
    bool ok = false;
    while (fgets(line, sizeof(line), fp) != NULL) {
        double value = 0.0;
        if (sscanf(line, "CAGF: %lfMHz", &value) == 1) {
            mhz = value;
            ok = true;
            break;
        }
    }
    fclose(fp);
    return ok;
}
#endif

void StatsSensors::init_sysfs_devfreq_gpu() {
#if defined(__linux__)
    devfreqGpu_.clear();
    vector<string> dirs = list_gpu_devfreq_dirs();
    for (size_t i = 0; i < dirs.size(); ++i) {
        long long hz = 0;
        if (!read_sysfs_ll(dirs[i] + "/cur_freq", hz)) continue;

        DevfreqGpu gpu;
        gpu.dir = dirs[i];
        gpu.name = devfreq_gpu_label(dirs[i], i);
        devfreqGpu_.push_back(gpu);

        string suffix = i == 0 ? string("") : std::to_string(i);
        string freqKey = i == 0 ? "gpu_freq" : "gpu" + suffix + "_freq";
        if (createSensor(freqKey)==1) for (auto &s:_items) if (s.key==freqKey) { s.method=12; s.kind=8; s.label=gpu.name + " Frequency"; }

        long long busy = 0, total = 0;
        if (read_sysfs_ll(dirs[i] + "/busy_time", busy) && read_sysfs_ll(dirs[i] + "/total_time", total)) {
            string loadKey = i == 0 ? "gpu_load_percent" : "gpu" + suffix + "_load_percent";
            if (createSensor(loadKey)==1) for (auto &s:_items) if (s.key==loadKey) { s.method=14; s.kind=6; s.label=gpu.name + " Load"; }
        }
    }

    vector<string> memoryFiles = drm_gpu_memory_files();
    for (const string &path : memoryFiles) {
        if (path.find("gpu_busy_percent") != string::npos) {
            if (createSensor("gpu_drm_load_percent")==1) for (auto &s:_items) if (s.key=="gpu_drm_load_percent") { s.method=15; s.kind=6; s.label="GPU Load"; }
        } else if (path.find("mem_info_vram_used") != string::npos) {
            if (createSensor("gpu_memory_used_bytes")==1) for (auto &s:_items) if (s.key=="gpu_memory_used_bytes") { s.method=15; s.kind=7; s.label="GPU Memory Used"; }
        } else if (path.find("mem_info_gtt_used") != string::npos) {
            if (createSensor("gpu_gtt_memory_used_bytes")==1) for (auto &s:_items) if (s.key=="gpu_gtt_memory_used_bytes") { s.method=15; s.kind=7; s.label="GPU GTT Memory Used"; }
        }
    }

    vector<string> i915Files = i915_debugfs_frequency_files();
    for (size_t i = 0; i < i915Files.size(); ++i) {
        double mhz = 0.0;
        if (!read_i915_debugfs_frequency_mhz(i915Files[i], mhz)) continue;
        string key = i == 0 ? "gpu_i915_freq" : "gpu_i915_" + std::to_string(i) + "_freq";
        string label = i == 0 ? "GPU Frequency" : "GPU " + std::to_string(i) + " Frequency";
        if (createSensor(key)==1) for (auto &s:_items) if (s.key==key) { s.method=16; s.kind=8; s.label=label; }
    }
#endif
}
void StatsSensors::update_sysfs_devfreq_gpu(long long sampleID) {
#if defined(__linux__)
    if (devfreqGpu_.empty()) {
        init_sysfs_devfreq_gpu();
    }

    for (size_t i = 0; i < devfreqGpu_.size(); ++i) {
        string suffix = i == 0 ? string("") : std::to_string(i);
        long long hz=0;
        if (read_sysfs_ll(devfreqGpu_[i].dir + "/cur_freq", hz)) {
            string freqKey = i == 0 ? "gpu_freq" : "gpu" + suffix + "_freq";
            processSensor(freqKey, sampleID, (double)hz / 1.0e6);
        }

        long long busy = 0, total = 0;
        if (read_sysfs_ll(devfreqGpu_[i].dir + "/busy_time", busy) && read_sysfs_ll(devfreqGpu_[i].dir + "/total_time", total)) {
            long long deltaBusy = busy - devfreqGpu_[i].last_busy;
            long long deltaTotal = total - devfreqGpu_[i].last_total;
            if (devfreqGpu_[i].last_busy >= 0 && devfreqGpu_[i].last_total >= 0 && deltaBusy >= 0 && deltaTotal > 0) {
                double pct = ((double)deltaBusy / (double)deltaTotal) * 100.0;
                if (pct < 0.0) pct = 0.0;
                if (pct > 100.0) pct = 100.0;
                string loadKey = i == 0 ? "gpu_load_percent" : "gpu" + suffix + "_load_percent";
                processSensor(loadKey, sampleID, pct);
            }
            devfreqGpu_[i].last_busy = busy;
            devfreqGpu_[i].last_total = total;
        }
    }

    vector<string> memoryFiles = drm_gpu_memory_files();
    for (const string &path : memoryFiles) {
        long long value = 0;
        if (!read_sysfs_ll(path, value)) continue;
        if (path.find("gpu_busy_percent") != string::npos)
            processSensor("gpu_drm_load_percent", sampleID, (double)value);
        else if (path.find("mem_info_vram_used") != string::npos)
            processSensor("gpu_memory_used_bytes", sampleID, (double)value);
        else if (path.find("mem_info_gtt_used") != string::npos)
            processSensor("gpu_gtt_memory_used_bytes", sampleID, (double)value);
    }

    vector<string> i915Files = i915_debugfs_frequency_files();
    for (size_t i = 0; i < i915Files.size(); ++i) {
        double mhz = 0.0;
        if (!read_i915_debugfs_frequency_mhz(i915Files[i], mhz)) continue;
        string key = i == 0 ? "gpu_i915_freq" : "gpu_i915_" + std::to_string(i) + "_freq";
        processSensor(key, sampleID, mhz);
    }
#endif
}

void StatsSensors::init_rapl() {
#if defined(__linux__)
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
#endif
}

void StatsSensors::update_rapl(long long sampleID) {
#if defined(__linux__)
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
#endif
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
14 = Linux sysfs GPU load (/sys/class/devfreq/ * /busy_time + total_time)
15 = Linux DRM GPU load/memory (/sys/class/drm/card* /device)
16 = Linux i915 debugfs frequency (/sys/kernel/debug/dri/<card>/gt0/frequency)
20 = macOS powermetrics helper
21 = macOS IOHID temperature sensors
22 = macOS AppleSMC fan speed
23 = macOS IOReport Energy Model power counters
24 = macOS AGX accelerator performance statistics

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
	#ifdef __APPLE__
	init_macos_hid_temperature_sensors(_items, [this](const string &key) { return this->createSensor(key); });
	init_macos_smc_fan_sensors(_items, [this](const string &key) { return this->createSensor(key); });
	init_macos_ioreport_power_sensors(_items, [this](const string &key) { return this->createSensor(key); });
	init_macos_powermetrics_sensors(_items, [this](const string &key) { return this->createSensor(key); });
	init_macos_agx_gpu_sensors(_items, [this](const string &key) { return this->createSensor(key); });
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
	#ifdef __APPLE__
	update_macos_hid_temperatures(sampleID, [this](const string &key) { return this->createSensor(key); }, _items, [this](const string &key, long long sid, double value) { this->processSensor(key, sid, value); });
	update_macos_smc_fans(sampleID, [this](const string &key, long long sid, double value) { this->processSensor(key, sid, value); });
	update_macos_ioreport_power(sampleID, [this](const string &key, long long sid, double value) { this->processSensor(key, sid, value); });
	update_macos_powermetrics(sampleID, [this](const string &key, long long sid, double value) { this->processSensor(key, sid, value); });
	update_macos_agx_gpu_stats(sampleID, [this](const string &key, long long sid, double value) { this->processSensor(key, sid, value); });
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
