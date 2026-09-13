#ifndef ISTAT_SMART_METADATA_H
#define ISTAT_SMART_METADATA_H

#include <libxml/parser.h>
#include <map>
#include <string>
#include <ctime>
#include <cstdlib>
#include <sys/stat.h>

namespace istat {
struct DiskHealth {
    std::string devices, state, detail;
    long checked;
    DiskHealth() : checked(0) {}
};

class SmartMetadata {
    time_t lastRead;
    std::map<std::string, DiskHealth> volumes;
    static std::string attribute(xmlNodePtr node, const char *name) {
        xmlChar *value = xmlGetProp(node, BAD_CAST name);
        std::string result = value ? (const char *)value : "";
        if (value) xmlFree(value);
        return result.size() <= 4096 ? result : "";
    }
public:
    SmartMetadata() : lastRead(0) {}
    void refresh(const char *path = "/var/run/istatserver-smart/status.xml") {
        time_t now = time(NULL);
        if (now >= lastRead && now - lastRead < 30) return;
        lastRead = now;
        volumes.clear();
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size > 1024 * 1024) return;
        xmlDocPtr doc = xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (!doc) return;
        xmlNodePtr root = xmlDocGetRootElement(doc);
        if (!doc->intSubset && !doc->extSubset && root && xmlStrEqual(root->name, BAD_CAST "health") && attribute(root, "version") == "1") {
            for (xmlNodePtr node = root->children; node; node = node->next) {
                if (!xmlStrEqual(node->name, BAD_CAST "volume")) continue;
                DiskHealth health;
                health.devices = attribute(node, "devices");
                health.state = attribute(node, "state");
                health.detail = attribute(node, "detail");
                std::string checked = attribute(node, "checked");
                char *end = NULL;
                health.checked = strtol(checked.c_str(), &end, 10);
                if (!end || *end || health.checked <= 0) health.checked = 0;
                volumes[attribute(node, "bsd")] = health;
            }
        }
        xmlFreeDoc(doc);
    }
    DiskHealth lookup(const std::string &bsd) const {
        std::map<std::string, DiskHealth>::const_iterator found = volumes.find(bsd);
        DiskHealth result;
        if (found == volumes.end()) return result;
        result = found->second;
        time_t now = time(NULL);
        if (result.checked <= 0 || result.checked > now + 60 || now - result.checked > 900) result.state = "stale";
        return result;
    }
};
}
#endif
