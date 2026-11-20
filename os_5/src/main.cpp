#include "imgui.h"
#include "imgui-SFML.h"
#include <SFML/Graphics.hpp>

#include <sys/param.h>
#include <sys/mount.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <fstream>

// Utilities

static std::string runCmd(const char* cmd, int* exitCode = nullptr) {
    std::string out;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) { if (exitCode) *exitCode = -1; return out; }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    int st = pclose(pipe);
    if (exitCode) *exitCode = st;
    return out;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static bool containsCI(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    return toLower(hay).find(toLower(needle)) != std::string::npos;
}

static std::string cfToStd(CFTypeRef v) {
    if (!v) return {};
    CFTypeID tid = CFGetTypeID(v);
    if (tid == CFStringGetTypeID()) {
        char buf[2048];
        if (CFStringGetCString((CFStringRef)v, buf, sizeof(buf), kCFStringEncodingUTF8))
            return buf;
        return {};
    }
    if (tid == CFUUIDGetTypeID()) {
        CFStringRef s = CFUUIDCreateString(kCFAllocatorDefault, (CFUUIDRef)v);
        if (!s) return {};
        char buf[64] = {};
        std::string out;
        if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) out = buf;
        CFRelease(s);
        return out;
    }
    return {};
}

static long long cfInt(CFTypeRef v) {
    if (!v || CFGetTypeID(v) != CFNumberGetTypeID()) return 0;
    long long out = 0;
    CFNumberGetValue((CFNumberRef)v, kCFNumberLongLongType, &out);
    return out;
}

static bool cfBool(CFTypeRef v) {
    if (!v || CFGetTypeID(v) != CFBooleanGetTypeID()) return false;
    return CFBooleanGetValue((CFBooleanRef)v);
}

static std::string humanBytes(long long b) {
    if (b < 0) return "-";
    const char* u[] = {"B","KB","MB","GB","TB","PB"};
    double v = (double)b;
    int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; ++i; }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f %s", v, u[i]);
    return buf;
}

static std::string prettyScheme(const std::string& s) {
    if (s == "GUID_partition_scheme")      return "GPT";
    if (s == "FDisk_partition_scheme")     return "MBR";
    if (s == "Apple_partition_scheme")     return "APM";
    if (s == "Apple_APFS_Container")       return "APFS Container";
    return s.empty() ? "-" : s;
}

static std::string flagsToString(unsigned long f) {
    std::string s;
    auto add = [&](const char* n) { if (!s.empty()) s += ", "; s += n; };
    if (f & MNT_RDONLY)           add("ro");
    if (f & MNT_SYNCHRONOUS)      add("sync");
    if (f & MNT_NOEXEC)           add("noexec");
    if (f & MNT_NOSUID)           add("nosuid");
    if (f & MNT_NODEV)            add("nodev");
    if (f & MNT_ASYNC)            add("async");
    if (f & MNT_EXPORTED)         add("exported");
    if (f & MNT_QUARANTINE)       add("quarantine");
    if (f & MNT_LOCAL)            add("local");
    if (f & MNT_QUOTA)            add("quota");
    if (f & MNT_ROOTFS)           add("rootfs");
    if (f & MNT_DOVOLFS)          add("dovolfs");
    if (f & MNT_DONTBROWSE)       add("nobrowse");
    if (f & MNT_IGNORE_OWNERSHIP) add("ignore-owner");
    if (f & MNT_AUTOMOUNTED)      add("automounted");
    if (f & MNT_JOURNALED)        add("journaled");
    if (f & MNT_NOUSERXATTR)      add("nouserxattr");
    if (f & MNT_DEFWRITE)         add("defwrite");
    if (f & MNT_MULTILABEL)       add("multilabel");
    if (f & MNT_NOATIME)          add("noatime");
    return s.empty() ? "-" : s;
}

static long long parseLastInt(const std::string& s) {
    long long result = 0, mult = 1;
    bool found = false;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        char c = s[i];
        if (std::isdigit((unsigned char)c)) {
            result += (long long)(c - '0') * mult;
            mult *= 10;
            found = true;
        } else if (c == ',' || c == ' ' || c == '\t') {
            if (found) break;
        } else {
            if (found) break;
        }
    }
    return found ? result : -1;
}

static long long parseIntAfter(const std::string& line, const std::string& prefix) {
    size_t p = line.find(prefix);
    if (p == std::string::npos) return -1;
    p += prefix.size();
    while (p < line.size() && !std::isdigit((unsigned char)line[p]) &&
           line[p] != '-' && line[p] != '+') ++p;
    if (p >= line.size()) return -1;

    bool neg = false;
    if (line[p] == '-') { neg = true; ++p; }
    else if (line[p] == '+') { ++p; }

    bool hex = false;
    if (p + 1 < line.size() && line[p] == '0' &&
        (line[p+1] == 'x' || line[p+1] == 'X')) {
        hex = true; p += 2;
    }

    long long val = 0;
    while (p < line.size()) {
        char c = line[p];
        if (std::isdigit((unsigned char)c)) {
            val = val * (hex ? 16 : 10) + (c - '0');
            ++p;
        } else if (hex && std::isxdigit((unsigned char)c)) {
            val = val * 16 + (std::toupper((unsigned char)c) - 'A' + 10);
            ++p;
        } else if (c == ',') {
            ++p;
        } else break;
    }
    return neg ? -val : val;
}

static std::string timestampNow() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

// Data models

struct DeviceDetail {
    std::string deviceNode;
    std::string parentWholeDisk;
    std::string volumeName;
    bool        whole = false;
    bool        mounted = false;
    std::string partitionType;
    std::string filesystemType;
    std::string filesystemUserVisible;
    bool        ownersEnabled = false;
    bool        osCanBeInstalled = false;
    std::string booterDisk;
    std::string recoveryDisk;
    std::string mediaType;
    long long   partitionOffset = -1;
    bool        mediaOSUseOnly = false;
    bool        mediaReadOnly = false;
    bool        volumeReadOnly = false;
    bool        hardwareAESSupport = false;
    bool        encrypted = false;
    bool        fileVault = false;
    bool        sealed = false;
    bool        locked = false;
    std::string apfsSnapshotName;
    std::string apfsSnapshotUUID;
    std::string apfsContainerRef;
    std::string apfsPhysicalStore;
    std::string apfsVolumeGroupID;
    std::string efiDriverInMacOS;
    std::string virtualOrPhysical;
    long long   volumeUsedSpace = 0;
    long long   containerTotalSpace = 0;
    long long   containerFreeSpace = 0;
    long long   freeSpace = 0;
    long long   volumeFreeSpace = 0;
};

struct PartitionInfo {
    std::string devId;
    std::string content;
    std::string volumeName;
    std::string volumeUUID;
    std::string partitionUUID;
    std::string mountPoint;
    long long   size = 0;
    DeviceDetail detail;
};

struct GptEntry {
    uint64_t    start = 0;
    uint64_t    size = 0;
    uint64_t    index = 0;
    bool        hasIndex = false;
    std::string contents;
    std::string uuid;
};

struct GptInfo {
    bool        valid = false;
    std::string errorMsg;
    uint64_t    priHeaderStart = 0, priHeaderSize = 0;
    uint64_t    priTableStart  = 0, priTableSize  = 0;
    uint64_t    secTableStart  = 0, secTableSize  = 0;
    uint64_t    secHeaderStart = 0, secHeaderSize = 0;
    bool        hasPmbr = false;
    uint64_t    pmbrStart = 0, pmbrSize = 0;
    std::vector<GptEntry> entries;
};

struct SmartInfo {
    bool        available = false;
    bool        passed = false;
    std::string statusText = "N/A";
    std::string error;
    long long   temperature      = -1;
    long long   powerOnHours     = -1;
    long long   percentageUsed   = -1;
    long long   availableSpare   = -1;
    long long   criticalWarning  = -1;
    long long   mediaErrors      = -1;
    long long   unsafeShutdowns  = -1;
    long long   reallocated      = -1;
    long long   pending          = -1;
    std::string raw;
};

struct DiskInfo {
    std::string devId;
    std::string content;
    long long   size = 0;
    std::string model;
    std::string serial;
    std::string firmware;
    std::string busProtocol;
    std::string mediaUUID;
    bool        removable   = false;
    bool        solidState  = false;
    bool        internal    = false;
    bool        ejectable   = false;
    std::string smartStatus;
    GptInfo     gpt;
    SmartInfo   smart;
    DeviceDetail detail;
    std::vector<PartitionInfo> partitions;
};

struct MountedFS {
    std::string device;
    std::string mountPoint;
    std::string fstype;
    long long   totalBytes = 0;
    long long   freeBytes  = 0;
    long long   blockSize  = 0;
    long long   ioSize     = 0;
    long long   totalFiles = 0;
    long long   freeFiles  = 0;
    unsigned long flags    = 0;
};

struct ApfsVolume {
    std::string name, devId, roles;
    long long   inUse = 0, quota = 0, reserve = 0;
    long long   blockSize = 0;
    std::string volumeUUID;
    std::string partitionUUID;
    std::string mountPoint;
    bool encrypted   = false;
    bool fileVault   = false;
    bool locked      = false;
    bool sealed      = false;
    bool caseSensitive = false;
    bool cryptoMigration = false;
    DeviceDetail detail;
};

struct ApfsContainer {
    std::string uuid, reference, store;
    long long   ceiling = 0, free = 0;
    std::vector<ApfsVolume> volumes;
};

struct AppState {
    std::vector<DiskInfo>      disks;
    std::vector<MountedFS>     mounts;
    std::vector<ApfsContainer> apfs;
    std::string lastUpdate;
    std::string lastError;
};

struct DiskSel {
    enum Kind { None, Disk, Part, Vol } kind = None;
    int disk = -1;
    int part = -1;
    int vol  = -1;
};

struct UIState {
    char filterDisks[128]   = "";
    char filterParts[128]   = "";
    char filterMounts[128]  = "";
    char filterApfs[128]    = "";
    char filterHealth[128]  = "";

    DiskSel diskSel;
    int     selectedHealthIdx = -1;

    std::string toast;
    double toastUntil = 0.0;
};

// Loaders

static GptInfo readGPT(const std::string& devId) {
    GptInfo info;
    std::string cmd = "/usr/sbin/gpt -r show /dev/" + devId + " 2>/dev/null";
    std::string out = runCmd(cmd.c_str());

    if (out.empty()) {
        info.errorMsg = "Root privileges required (run with sudo) or no GPT";
        return info;
    }

    std::istringstream iss(out);
    std::string line;
    std::getline(iss, line);  // skip column header

    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        GptEntry e;
        if (!(ls >> e.start >> e.size)) continue;

        std::string rest;
        std::getline(ls, rest);
        rest = trim(rest);
        if (rest.empty()) continue;

        // Rows that carry an index look like: "     1  GPT part - <UUID>"
        {
            std::istringstream rs(rest);
            uint64_t idx = 0;
            if (rs >> idx) {
                std::string tail;
                std::getline(rs, tail);
                tail = trim(tail);
                if (tail.rfind("GPT part", 0) == 0) {
                    e.hasIndex = true;
                    e.index = idx;
                    size_t dash = tail.find('-');
                    if (dash != std::string::npos)
                        e.uuid = trim(tail.substr(dash + 1));
                    e.contents = "GPT part";
                    info.entries.push_back(e);
                    continue;
                }
            }
        }

        e.contents = rest;
        if (rest == "PMBR") {
            info.hasPmbr = true;
            info.pmbrStart = e.start;
            info.pmbrSize  = e.size;
        } else if (rest == "Pri GPT header") {
            info.priHeaderStart = e.start;
            info.priHeaderSize  = e.size;
        } else if (rest == "Pri GPT table") {
            info.priTableStart = e.start;
            info.priTableSize  = e.size;
        } else if (rest == "Sec GPT table") {
            info.secTableStart = e.start;
            info.secTableSize  = e.size;
        } else if (rest == "Sec GPT header") {
            info.secHeaderStart = e.start;
            info.secHeaderSize  = e.size;
        }
        info.entries.push_back(e);
    }

    info.valid = (info.priTableStart > 0);
    if (!info.valid)
        info.errorMsg = "Failed to parse GPT output. Are you running with sudo?";
    return info;
}

struct DiskutilInfo {
    std::string serialNumber;
    std::string deviceModel;
    std::string busProtocol;
    std::string volumeUUID;
    std::string partitionUUID;
    std::string mountPoint;
    std::string smartStatus;
    long long   deviceBlockSize = 0;
    long long   allocationBlockSize = 0;
    bool        removable   = false;
    bool        solidState  = false;
    bool        internal    = false;
    bool        ejectable   = false;
    DeviceDetail detail;
};

static DiskutilInfo getDiskutilInfo(const std::string& devId) {
    DiskutilInfo info;
    std::string cmd = "/usr/sbin/diskutil info -plist " + devId + " 2>/dev/null";
    std::string xml = runCmd(cmd.c_str());
    if (xml.empty()) return info;

    CFDataRef data = CFDataCreate(nullptr,
        reinterpret_cast<const UInt8*>(xml.data()), (CFIndex)xml.size());
    if (!data) return info;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(data);
    if (!plist) return info;

    auto dict = static_cast<CFDictionaryRef>(plist);
    auto getStr = [&](CFStringRef k) { return cfToStd(CFDictionaryGetValue(dict, k)); };
    auto getInt = [&](CFStringRef k) { return cfInt  (CFDictionaryGetValue(dict, k)); };
    auto getBool= [&](CFStringRef k) { return cfBool (CFDictionaryGetValue(dict, k)); };

    info.serialNumber = getStr(CFSTR("SerialNumber"));
    if (info.serialNumber.empty()) info.serialNumber = getStr(CFSTR("Serial Number"));
    if (info.serialNumber.empty()) info.serialNumber = getStr(CFSTR("USB Serial Number"));

    info.deviceModel  = getStr(CFSTR("MediaName"));
    info.busProtocol  = getStr(CFSTR("BusProtocol"));
    info.volumeUUID   = getStr(CFSTR("VolumeUUID"));
    info.partitionUUID= getStr(CFSTR("DiskUUID"));
    info.mountPoint   = getStr(CFSTR("MountPoint"));
    info.smartStatus  = getStr(CFSTR("SmartStatus"));
    if (info.smartStatus.empty()) info.smartStatus = getStr(CFSTR("SMARTStatus"));

    info.deviceBlockSize     = getInt(CFSTR("DeviceBlockSize"));
    info.allocationBlockSize = getInt(CFSTR("AllocationBlockSize"));

    info.removable  = getBool(CFSTR("RemovableMedia")) || getBool(CFSTR("Removable"));
    info.solidState = getBool(CFSTR("SolidState"));
    info.internal   = getBool(CFSTR("Internal"));
    info.ejectable  = getBool(CFSTR("Ejectable"));

    auto& d = info.detail;
    d.deviceNode            = getStr(CFSTR("DeviceNode"));
    d.parentWholeDisk       = getStr(CFSTR("ParentWholeDisk"));
    d.volumeName            = getStr(CFSTR("VolumeName"));
    d.whole                 = getBool(CFSTR("Whole"));
    d.mounted               = getBool(CFSTR("Mounted"));
    d.partitionType         = getStr(CFSTR("PartitionType"));
    d.filesystemType        = getStr(CFSTR("FilesystemType"));
    d.filesystemUserVisible = getStr(CFSTR("FilesystemUserVisibleName"));
    d.ownersEnabled         = getBool(CFSTR("OwnersEnabled"));
    d.osCanBeInstalled      = getBool(CFSTR("OSCanBeInstalled"));
    d.booterDisk            = getStr(CFSTR("BooterDisk"));
    d.recoveryDisk          = getStr(CFSTR("RecoveryDisk"));
    d.mediaType             = getStr(CFSTR("MediaType"));
    d.partitionOffset       = getInt(CFSTR("PartitionOffset"));
    d.mediaOSUseOnly        = getBool(CFSTR("MediaOSUseOnly"));
    d.mediaReadOnly         = getBool(CFSTR("MediaReadOnly"));
    d.volumeReadOnly        = getBool(CFSTR("VolumeReadOnly"));
    d.hardwareAESSupport    = getBool(CFSTR("HardwareAESSupport"));
    d.encrypted             = getBool(CFSTR("Encrypted"));
    d.fileVault             = getBool(CFSTR("FileVault"));
    d.sealed                = getBool(CFSTR("Sealed"));
    d.locked                = getBool(CFSTR("Locked"));
    d.apfsSnapshotName      = getStr(CFSTR("APFSSnapshotName"));
    d.apfsSnapshotUUID      = getStr(CFSTR("APFSSnapshotUUID"));
    d.apfsContainerRef      = getStr(CFSTR("APFSContainerReference"));
    d.apfsVolumeGroupID     = getStr(CFSTR("APFSVolumeGroupID"));
    d.efiDriverInMacOS      = getStr(CFSTR("EFIDriverInMacOS"));
    d.virtualOrPhysical     = getStr(CFSTR("VirtualOrPhysical"));
    d.volumeUsedSpace       = getInt(CFSTR("VolumeUsedSpace"));
    d.containerTotalSpace   = getInt(CFSTR("ContainerTotalSpace"));
    d.containerFreeSpace    = getInt(CFSTR("ContainerFreeSpace"));
    d.freeSpace             = getInt(CFSTR("FreeSpace"));
    d.volumeFreeSpace       = getInt(CFSTR("VolumeFreeSpace"));

    auto stores = static_cast<CFArrayRef>(
        CFDictionaryGetValue(dict, CFSTR("APFSPhysicalStores")));
    if (stores) {
        CFIndex n = CFArrayGetCount(stores);
        for (CFIndex i = 0; i < n; ++i) {
            auto sd = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(stores, i));
            std::string s = cfToStd(CFDictionaryGetValue(sd, CFSTR("APFSPhysicalStore")));
            if (s.empty()) continue;
            if (!d.apfsPhysicalStore.empty()) d.apfsPhysicalStore += ", ";
            d.apfsPhysicalStore += s;
        }
    }

    CFRelease(plist);
    return info;
}

static bool hasSmartctl() {
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    int rc = 0;
    runCmd("command -v smartctl >/dev/null 2>&1", &rc);
    cached = (rc == 0) ? 1 : 0;
    return cached == 1;
}

static SmartInfo readSmart(const std::string& devId) {
    SmartInfo s;
    if (!hasSmartctl()) {
        s.error = "smartctl not installed (brew install smartmontools)";
        return s;
    }
    std::string out = runCmd(("smartctl -a /dev/" + devId + " 2>&1").c_str());
    if (out.empty()) { s.error = "smartctl: empty output"; return s; }

    s.raw = out;
    s.available = true;

    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        std::string t = trim(line);

        if (t.find("SMART overall-health") != std::string::npos) {
            s.passed = (t.find("PASSED") != std::string::npos);
            s.statusText = s.passed ? "PASSED" : "FAILED";
        }
        else if (t.rfind("Temperature:", 0) == 0) {
            s.temperature = parseIntAfter(t, "Temperature:");
        }
        else if (t.rfind("Power On Hours:", 0) == 0) {
            s.powerOnHours = parseIntAfter(t, "Power On Hours:");
        }
        else if (t.rfind("Percentage Used:", 0) == 0) {
            s.percentageUsed = parseIntAfter(t, "Percentage Used:");
        }
        else if (t.rfind("Available Spare:", 0) == 0 &&
                 t.find("Threshold") == std::string::npos) {
            s.availableSpare = parseIntAfter(t, "Available Spare:");
        }
        else if (t.rfind("Critical Warning:", 0) == 0) {
            s.criticalWarning = parseIntAfter(t, "Critical Warning:");
        }
        else if (t.rfind("Media and Data Integrity Errors:", 0) == 0) {
            s.mediaErrors = parseIntAfter(t, "Media and Data Integrity Errors:");
        }
        else if (t.rfind("Unsafe Shutdowns:", 0) == 0) {
            s.unsafeShutdowns = parseIntAfter(t, "Unsafe Shutdowns:");
        }
        else {
            int attrId = 0; char name[64] = {};
            if (sscanf(t.c_str(), "%d %63s", &attrId, name) == 2) {
                if (attrId == 5)   s.reallocated = parseLastInt(t);
                if (attrId == 197) s.pending     = parseLastInt(t);
                if (attrId == 9  && s.powerOnHours < 0) s.powerOnHours = parseLastInt(t);
                if (attrId == 194&& s.temperature  < 0) s.temperature  = parseLastInt(t);
            }
        }
    }
    return s;
}

static std::string extractJsonString(const std::string& json, const std::string& key,
                                     size_t startPos) {
    std::string search = "\"" + key + "\"";
    size_t p = json.find(search, startPos);
    if (p == std::string::npos) return {};
    p += search.size();
    while (p < json.size() &&
           (json[p] == ' ' || json[p] == ':' || json[p] == '\t' ||
            json[p] == '\n' || json[p] == '\r')) ++p;
    if (p >= json.size() || json[p] != '"') return {};
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) ++p;
        out += json[p++];
    }
    return out;
}

static std::map<std::string, std::string> loadFirmwareMap() {
    std::map<std::string, std::string> out;
    std::string json = runCmd("system_profiler SPStorageDataType -json 2>/dev/null");
    if (json.empty()) return out;

    size_t pos = 0;
    while (pos < json.size()) {
        size_t bsdPos = json.find("\"bsd_name\"", pos);
        if (bsdPos == std::string::npos) break;
        std::string bsd = extractJsonString(json, "bsd_name", bsdPos);
        size_t nextBsd = json.find("\"bsd_name\"", bsdPos + 1);
        size_t endPos = (nextBsd == std::string::npos) ? json.size() : nextBsd;

        size_t fwPos = json.find("\"firmware_revision\"", bsdPos);
        if (fwPos != std::string::npos && fwPos < endPos) {
            std::string fw = extractJsonString(json, "firmware_revision", fwPos);
            if (!fw.empty() && !bsd.empty()) out[bsd] = fw;
        }
        pos = endPos;
    }
    return out;
}

static std::vector<DiskInfo> loadDisks() {
    std::vector<DiskInfo> out;
    std::string xml = runCmd("/usr/sbin/diskutil list -plist 2>/dev/null");
    if (xml.empty()) return out;

    CFDataRef data = CFDataCreate(nullptr,
        reinterpret_cast<const UInt8*>(xml.data()), (CFIndex)xml.size());
    if (!data) return out;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(data);
    if (!plist) return out;

    auto root = static_cast<CFDictionaryRef>(plist);
    auto all  = static_cast<CFArrayRef>(
        CFDictionaryGetValue(root, CFSTR("AllDisksAndPartitions")));

    auto fwMap = loadFirmwareMap();

    if (all) {
        CFIndex n = CFArrayGetCount(all);
        for (CFIndex i = 0; i < n; ++i) {
            auto d = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(all, i));
            DiskInfo di;
            di.devId   = cfToStd(CFDictionaryGetValue(d, CFSTR("DeviceIdentifier")));
            di.content = cfToStd(CFDictionaryGetValue(d, CFSTR("Content")));
            di.size    = cfInt  (CFDictionaryGetValue(d, CFSTR("Size")));

            DiskutilInfo wi = getDiskutilInfo(di.devId);
            di.model       = wi.deviceModel;
            di.serial      = wi.serialNumber;
            di.busProtocol = wi.busProtocol;
            di.mediaUUID   = wi.partitionUUID;
            di.removable   = wi.removable;
            di.solidState  = wi.solidState;
            di.internal    = wi.internal;
            di.ejectable   = wi.ejectable;
            di.smartStatus = wi.smartStatus;
            di.detail      = wi.detail;
            if (fwMap.count(di.devId)) di.firmware = fwMap[di.devId];

            if (di.content == "GUID_partition_scheme")
                di.gpt = readGPT(di.devId);

            auto parts = static_cast<CFArrayRef>(
                CFDictionaryGetValue(d, CFSTR("Partitions")));
            if (parts) {
                CFIndex m = CFArrayGetCount(parts);
                for (CFIndex j = 0; j < m; ++j) {
                    auto p = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(parts, j));
                    PartitionInfo pi;
                    pi.devId         = cfToStd(CFDictionaryGetValue(p, CFSTR("DeviceIdentifier")));
                    pi.content       = cfToStd(CFDictionaryGetValue(p, CFSTR("Content")));
                    pi.volumeName    = cfToStd(CFDictionaryGetValue(p, CFSTR("VolumeName")));
                    pi.volumeUUID    = cfToStd(CFDictionaryGetValue(p, CFSTR("VolumeUUID")));
                    pi.partitionUUID = cfToStd(CFDictionaryGetValue(p, CFSTR("DiskUUID")));
                    pi.size          = cfInt  (CFDictionaryGetValue(p, CFSTR("Size")));

                    DiskutilInfo pInfo = getDiskutilInfo(pi.devId);
                    if (!pInfo.volumeUUID.empty())    pi.volumeUUID    = pInfo.volumeUUID;
                    if (!pInfo.partitionUUID.empty()) pi.partitionUUID = pInfo.partitionUUID;
                    if (!pInfo.mountPoint.empty())    pi.mountPoint    = pInfo.mountPoint;
                    pi.detail = pInfo.detail;

                    di.partitions.push_back(std::move(pi));
                }
            }
            out.push_back(std::move(di));
        }
    }
    CFRelease(plist);
    return out;
}

static std::vector<MountedFS> loadMounts() {
    std::vector<MountedFS> out;
    struct statfs* mounts = nullptr;
    int n = getmntinfo(&mounts, MNT_NOWAIT);
    if (n <= 0) return out;
    out.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        MountedFS m;
        m.device     = mounts[i].f_mntfromname;
        m.mountPoint = mounts[i].f_mntonname;
        m.fstype     = mounts[i].f_fstypename;
        m.blockSize  = mounts[i].f_bsize;
        m.ioSize     = mounts[i].f_iosize;
        m.totalFiles = mounts[i].f_files;
        m.freeFiles  = mounts[i].f_ffree;
        m.flags      = mounts[i].f_flags;
        m.totalBytes = (long long)mounts[i].f_blocks * mounts[i].f_bsize;
        m.freeBytes  = (long long)mounts[i].f_bfree  * mounts[i].f_bsize;
        out.push_back(std::move(m));
    }
    return out;
}

static std::vector<ApfsContainer> loadApfs() {
    std::vector<ApfsContainer> out;
    std::string xml = runCmd("/usr/sbin/diskutil apfs list -plist 2>/dev/null");
    if (xml.empty()) return out;

    CFDataRef data = CFDataCreate(nullptr,
        reinterpret_cast<const UInt8*>(xml.data()), (CFIndex)xml.size());
    if (!data) return out;
    CFPropertyListRef plist = CFPropertyListCreateWithData(
        nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(data);
    if (!plist) return out;

    auto root = static_cast<CFDictionaryRef>(plist);
    auto containers = static_cast<CFArrayRef>(
        CFDictionaryGetValue(root, CFSTR("Containers")));

    if (containers) {
        CFIndex n = CFArrayGetCount(containers);
        for (CFIndex i = 0; i < n; ++i) {
            auto c = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(containers, i));
            ApfsContainer ci;
            ci.uuid      = cfToStd(CFDictionaryGetValue(c, CFSTR("APFSContainerUUID")));
            ci.reference = cfToStd(CFDictionaryGetValue(c, CFSTR("ContainerReference")));
            ci.store     = cfToStd(CFDictionaryGetValue(c, CFSTR("DesignatedPhysicalStore")));
            ci.ceiling   = cfInt  (CFDictionaryGetValue(c, CFSTR("CapacityCeiling")));
            ci.free      = cfInt  (CFDictionaryGetValue(c, CFSTR("CapacityFree")));

            auto vols = static_cast<CFArrayRef>(CFDictionaryGetValue(c, CFSTR("Volumes")));
            if (vols) {
                CFIndex m = CFArrayGetCount(vols);
                for (CFIndex j = 0; j < m; ++j) {
                    auto v = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(vols, j));
                    ApfsVolume vi;
                    vi.name        = cfToStd(CFDictionaryGetValue(v, CFSTR("Name")));
                    vi.devId       = cfToStd(CFDictionaryGetValue(v, CFSTR("DeviceIdentifier")));
                    vi.inUse       = cfInt  (CFDictionaryGetValue(v, CFSTR("CapacityInUse")));
                    vi.quota       = cfInt  (CFDictionaryGetValue(v, CFSTR("CapacityQuota")));
                    vi.reserve     = cfInt  (CFDictionaryGetValue(v, CFSTR("CapacityReserve")));
                    vi.encrypted   = cfBool (CFDictionaryGetValue(v, CFSTR("Encryption")));
                    vi.fileVault   = cfBool (CFDictionaryGetValue(v, CFSTR("FileVault")));
                    vi.locked      = cfBool (CFDictionaryGetValue(v, CFSTR("Locked")));
                    vi.sealed      = cfBool (CFDictionaryGetValue(v, CFSTR("Sealed")));
                    vi.caseSensitive = cfBool(CFDictionaryGetValue(v, CFSTR("CaseSensitive")));
                    vi.cryptoMigration = cfBool(CFDictionaryGetValue(v, CFSTR("CryptoMigrationOn")));

                    DiskutilInfo vi2 = getDiskutilInfo(vi.devId);
                    vi.blockSize     = vi2.allocationBlockSize > 0
                                     ? vi2.allocationBlockSize
                                     : vi2.deviceBlockSize;
                    vi.volumeUUID    = vi2.volumeUUID;
                    vi.partitionUUID = vi2.partitionUUID;
                    vi.mountPoint    = vi2.mountPoint;
                    vi.detail        = vi2.detail;

                    auto roles = static_cast<CFArrayRef>(CFDictionaryGetValue(v, CFSTR("Roles")));
                    if (roles) {
                        CFIndex r = CFArrayGetCount(roles);
                        for (CFIndex k = 0; k < r; ++k) {
                            if (k) vi.roles += ", ";
                            vi.roles += cfToStd(CFArrayGetValueAtIndex(roles, k));
                        }
                    }
                    ci.volumes.push_back(std::move(vi));
                }
            }
            out.push_back(std::move(ci));
        }
    }
    CFRelease(plist);
    return out;
}

static void linkMountPoints(std::vector<DiskInfo>& disks,
                            const std::vector<ApfsContainer>& apfs,
                            const std::vector<MountedFS>& mounts) {
    for (auto& d : disks) {
        for (auto& p : d.partitions) {
            if (!p.mountPoint.empty()) continue;

            if (p.content == "Apple_APFS") {
                for (auto& c : apfs) {
                    if (c.store == p.devId || c.reference == p.devId) {
                        auto pick = [&](const char* role) -> const ApfsVolume* {
                            for (auto& v : c.volumes)
                                if (v.roles.find(role) != std::string::npos &&
                                    !v.mountPoint.empty()) return &v;
                            return nullptr;
                        };
                        if (auto* v = pick("System"))     { p.mountPoint = v->mountPoint; break; }
                        if (auto* v = pick("Data"))       { p.mountPoint = v->mountPoint; break; }
                        for (auto& v : c.volumes)
                            if (!v.mountPoint.empty()) { p.mountPoint = v.mountPoint; break; }
                        break;
                    }
                }
            }

            if (p.mountPoint.empty()) {
                for (auto& m : mounts) {
                    std::string dev = m.device;
                    if (dev.rfind("/dev/", 0) == 0) dev = dev.substr(5);
                    if (dev == p.devId) { p.mountPoint = m.mountPoint; break; }
                }
            }
        }
        d.smart = readSmart(d.devId);
    }
}

static void refreshAll(AppState& s) {
    s.lastError.clear();
    s.disks  = loadDisks();
    s.mounts = loadMounts();
    s.apfs   = loadApfs();
    linkMountPoints(s.disks, s.apfs, s.mounts);
    s.lastUpdate = timestampNow();
    if (s.disks.empty())
        s.lastError = "Failed to retrieve disk list (check permissions)";
}

static const ApfsContainer* findContainerForPart(const AppState& s,
                                                 const std::string& partDevId) {
    for (auto& c : s.apfs) {
        if (c.store == partDevId || c.reference == partDevId) return &c;
    }
    return nullptr;
}

// UI helpers

static const ImVec4 COL_OK     {0.40f, 0.90f, 0.40f, 1.0f};
static const ImVec4 COL_WARN   {1.00f, 0.75f, 0.20f, 1.0f};
static const ImVec4 COL_ERR    {1.00f, 0.35f, 0.35f, 1.0f};
static const ImVec4 COL_MUTED  {0.60f, 0.60f, 0.60f, 1.0f};
static const ImVec4 COL_ACCENT {0.40f, 0.80f, 1.00f, 1.0f};

static float usageRatio(long long used, long long total) {
    if (total <= 0) return 0.0f;
    return std::min(1.0f, std::max(0.0f, (float)used / (float)total));
}

static ImU32 usageColor(float pct) {
    if (pct < 0.70f) return IM_COL32( 80, 200, 120, 255);
    if (pct < 0.90f) return IM_COL32(230, 180,  60, 255);
    return IM_COL32(220,  80,  80, 255);
}

static void usageBar(long long used, long long total) {
    float pct = usageRatio(used, total);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f%%  (%s / %s)",
                  pct * 100.0f, humanBytes(used).c_str(), humanBytes(total).c_str());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          ImGui::ColorConvertU32ToFloat4(usageColor(pct)));
    ImGui::ProgressBar(pct, ImVec2(-1, 0), buf);
    ImGui::PopStyleColor();
}

static void badge(const char* txt, const ImVec4& col) {
    ImGui::TextColored(col, "%s", txt);
}

static void filterInput(const char* id, char* buf, size_t sz, const char* hint) {
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint(id, hint, buf, sz);
}

static bool findPartUsage(const AppState& s, const std::string& devId,
                          long long& used, long long& total) {
    for (auto& c : s.apfs) {
        if (c.store == devId || c.reference == devId) {
            if (c.ceiling > 0) {
                total = c.ceiling;
                used = c.ceiling - c.free;
                return true;
            }
        }
    }

    for (auto& m : s.mounts) {
        std::string dev = m.device;
        if (dev.rfind("/dev/", 0) == 0) dev = dev.substr(5);
        if (dev == devId) {
            total = m.totalBytes;
            used = m.totalBytes - m.freeBytes;
            return total > 0;
        }
    }

    return false;
}

static bool isMountOnDisk(const MountedFS& m, const DiskInfo& d, const AppState& s) {
    std::string dev = m.device;
    if (dev.rfind("/dev/", 0) == 0) dev = dev.substr(5);

    if (dev == d.devId) return true;
    for (auto& p : d.partitions) {
        if (dev == p.devId) return true;
        if (dev.find(p.devId) == 0) return true;
        if (p.content == "Apple_APFS") {
            for (auto& c : s.apfs) {
                if (c.store == p.devId || c.reference == p.devId) {
                    for (auto& v : c.volumes) {
                        if (dev == v.devId) return true;
                        if (dev.find(v.devId) == 0) return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool diskMatchesFilter(const DiskInfo& d, const char* filter, const AppState& s) {
    if (!filter || !*filter) return true;
    std::string hay = d.devId + " " + d.model + " " + d.serial + " " + d.busProtocol;
    if (containsCI(hay, filter)) return true;
    for (auto& p : d.partitions) {
        std::string ph = p.devId + " " + p.content + " " + p.volumeName + " " + p.mountPoint;
        if (containsCI(ph, filter)) return true;
        if (p.content == "Apple_APFS") {
            if (auto* c = findContainerForPart(s, p.devId)) {
                for (auto& v : c->volumes) {
                    std::string vh = v.devId + " " + v.name + " " + v.roles + " " + v.mountPoint;
                    if (containsCI(vh, filter)) return true;
                }
            }
        }
    }
    return false;
}

// Extended properties table

static void drawDetailTable(const char* id, const DeviceDetail& d) {
    if (!ImGui::BeginTable(id, 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable)) {
        return;
    }

    auto row = [](const char* k, const std::string& v) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
    };
    auto rowb = [](const char* k, bool v) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(v ? "yes" : "no");
    };
    auto rowi = [](const char* k, long long v) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
        ImGui::TableNextColumn();
        if (v < 0) ImGui::TextDisabled("-");
        else       ImGui::Text("%lld", v);
    };

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextColored(COL_ACCENT, "Identity");
    ImGui::TableNextColumn();
    row("Device node",          d.deviceNode);
    row("Parent whole disk",    d.parentWholeDisk);
    rowb("Whole disk",          d.whole);
    row("Volume name",          d.volumeName);
    rowb("Mounted",             d.mounted);
    row("Partition type",       d.partitionType);
    row("Filesystem type",      d.filesystemType);
    row("Filesystem name",      d.filesystemUserVisible);
    row("Virtual / Physical",   d.virtualOrPhysical);

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextColored(COL_ACCENT, "Capabilities");
    ImGui::TableNextColumn();
    rowb("OS can be installed",   d.osCanBeInstalled);
    row("Booter disk",            d.booterDisk);
    row("Recovery disk",          d.recoveryDisk);
    row("Media type",             d.mediaType);
    rowb("Owners enabled",        d.ownersEnabled);
    rowb("Hardware AES support",  d.hardwareAESSupport);

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextColored(COL_ACCENT, "Layout");
    ImGui::TableNextColumn();
    rowi("Partition offset (bytes)", d.partitionOffset);

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextColored(COL_ACCENT, "Access");
    ImGui::TableNextColumn();
    rowb("Media OS use only",  d.mediaOSUseOnly);
    rowb("Media read-only",    d.mediaReadOnly);
    rowb("Volume read-only",   d.volumeReadOnly);

    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextColored(COL_ACCENT, "Encryption");
    ImGui::TableNextColumn();
    rowb("Encrypted",  d.encrypted);
    rowb("FileVault",  d.fileVault);
    rowb("Sealed",     d.sealed);
    rowb("Locked",     d.locked);

    if (!d.apfsContainerRef.empty() || !d.apfsPhysicalStore.empty() ||
        !d.apfsSnapshotName.empty()) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(COL_ACCENT, "APFS");
        ImGui::TableNextColumn();
        row("Container",       d.apfsContainerRef);
        row("Physical store",  d.apfsPhysicalStore);
        row("Volume group",    d.apfsVolumeGroupID);
        row("Snapshot name",   d.apfsSnapshotName);
        row("Snapshot UUID",   d.apfsSnapshotUUID);
        row("EFI driver",      d.efiDriverInMacOS);
    }

    if (d.volumeUsedSpace > 0 || d.containerTotalSpace > 0 || d.freeSpace > 0) {
        ImGui::TableNextRow(); ImGui::TableNextColumn();
        ImGui::TextColored(COL_ACCENT, "Space");
        ImGui::TableNextColumn();
        if (d.volumeUsedSpace     > 0) row("Volume used",     humanBytes(d.volumeUsedSpace));
        if (d.volumeFreeSpace     > 0) row("Volume free",     humanBytes(d.volumeFreeSpace));
        if (d.containerTotalSpace > 0) row("Container total", humanBytes(d.containerTotalSpace));
        if (d.containerFreeSpace  > 0) row("Container free",  humanBytes(d.containerFreeSpace));
        if (d.freeSpace           > 0) row("Free space",      humanBytes(d.freeSpace));
    }

    ImGui::EndTable();
}

// Disk tab: tree and detail

static void drawDiskTree(const AppState& s, UIState& ui) {
    ImGui::BeginChild("##disktree", ImVec2(340, 0), true);

    for (int di = 0; di < (int)s.disks.size(); ++di) {
        auto& d = s.disks[di];
        if (!diskMatchesFilter(d, ui.filterDisks, s)) continue;

        bool hasParts = !d.partitions.empty();
        bool sel = (ui.diskSel.kind == DiskSel::Disk && ui.diskSel.disk == di);

        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasParts) f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (sel)       f |= ImGuiTreeNodeFlags_Selected;

        std::string label = d.devId + " - " +
            (d.model.empty() ? std::string("(no model)") : d.model);
        if (!d.detail.virtualOrPhysical.empty() && d.detail.virtualOrPhysical != "Unknown") {
            label += " [" + d.detail.virtualOrPhysical + "]";
        }
        else if (d.detail.virtualOrPhysical == "Unknown") {
            label += " [Physical]";
        }

        ImGui::PushID(di);
        bool open = ImGui::TreeNodeEx("##disk", f, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            ui.diskSel = {};
            ui.diskSel.kind = DiskSel::Disk;
            ui.diskSel.disk = di;
        }

        if (open && hasParts) {
            for (int pi = 0; pi < (int)d.partitions.size(); ++pi) {
                auto& p = d.partitions[pi];
                ImGui::PushID(pi);

                bool psel = (ui.diskSel.kind == DiskSel::Part
                             && ui.diskSel.disk == di
                             && ui.diskSel.part == pi);
                std::string plabel = p.devId + " - " +
                    (p.volumeName.empty() ? p.content : p.volumeName);

                const ApfsContainer* cont = nullptr;
                if (p.content == "Apple_APFS")
                    cont = findContainerForPart(s, p.devId);
                bool hasVols = cont && !cont->volumes.empty();

                ImGuiTreeNodeFlags pf = ImGuiTreeNodeFlags_OpenOnArrow
                                      | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (!hasVols) pf |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (psel)     pf |= ImGuiTreeNodeFlags_Selected;

                bool popen = ImGui::TreeNodeEx("##part", pf, "%s", plabel.c_str());
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    ui.diskSel = {};
                    ui.diskSel.kind = DiskSel::Part;
                    ui.diskSel.disk = di;
                    ui.diskSel.part = pi;
                }

                if (popen && hasVols) {
                    for (int vi = 0; vi < (int)cont->volumes.size(); ++vi) {
                        auto& v = cont->volumes[vi];
                        ImGui::PushID(vi);

                        bool vsel = (ui.diskSel.kind == DiskSel::Vol
                                     && ui.diskSel.disk == di
                                     && ui.diskSel.part == pi
                                     && ui.diskSel.vol == vi);
                        ImGuiTreeNodeFlags vf = ImGuiTreeNodeFlags_OpenOnArrow
                                              | ImGuiTreeNodeFlags_SpanAvailWidth
                                              | ImGuiTreeNodeFlags_Leaf
                                              | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        if (vsel) vf |= ImGuiTreeNodeFlags_Selected;

                        std::string vlabel = v.devId + " - " + v.name;
                        if (!v.roles.empty()) vlabel += " (" + v.roles + ")";

                        ImGui::TreeNodeEx("##vol", vf, "%s", vlabel.c_str());
                        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                            ui.diskSel = {};
                            ui.diskSel.kind = DiskSel::Vol;
                            ui.diskSel.disk = di;
                            ui.diskSel.part = pi;
                            ui.diskSel.vol = vi;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
}

static void drawDiskDetailNode(const AppState& s, const DiskInfo& d) {
    if (ImGui::BeginTabBar("##disktabs")) {
        if (ImGui::BeginTabItem("Summary")) {
            if (ImGui::BeginTable("##sum", 2,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable)) {
                auto row = [](const char* k, const std::string& v) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
                };
                row("Device",            d.devId);
                row("Model",             d.model);
                row("Serial",            d.serial);
                row("Firmware",          d.firmware);
                row("Protocol",          d.busProtocol);
                row("Scheme",            prettyScheme(d.content));
                row("Size",              humanBytes(d.size));
                row("Media UUID",        d.mediaUUID);
                row("Removable",         d.removable ? "yes" : "no");
                row("Solid state",       d.solidState ? "yes" : "no");
                row("Internal",          d.internal ? "yes" : "no");
                row("Ejectable",         d.ejectable ? "yes" : "no");
                row("Virtual / Physical", d.detail.virtualOrPhysical == "Unknown" ? "Physical" : "Virtual");
                row("SMART (diskutil)",  d.smartStatus);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Properties")) {
            drawDetailTable("##propstable", d.detail);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Partitions")) {
            if (d.partitions.empty()) { ImGui::TextDisabled("No partitions."); }
            else if (ImGui::BeginTable("##parts", 6,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Partition");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Size");
                ImGui::TableSetupColumn("Usage");
                ImGui::TableSetupColumn("Mounted");
                ImGui::TableHeadersRow();
                for (auto& p : d.partitions) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(p.devId.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(p.content.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        p.volumeName.empty() ? "-" : p.volumeName.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        humanBytes(p.size).c_str());
                    ImGui::TableNextColumn();
                    long long u = 0, t = 0;
                    if (findPartUsage(s, p.devId, u, t)) usageBar(u, t);
                    else ImGui::TextDisabled("-");
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        p.mountPoint.empty() ? "-" : p.mountPoint.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("GPT")) {
            if (d.content != "GUID_partition_scheme") {
                ImGui::TextDisabled("Not a GPT disk.");
            } else if (!d.gpt.valid) {
                ImGui::TextColored(COL_ERR, "%s", d.gpt.errorMsg.c_str());
                ImGui::TextDisabled("Run with sudo to read GPT.");
            } else if (ImGui::BeginTable("##gpt", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Index",  ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Start LBA", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("Size (sectors)", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("Contents / UUID");
                ImGui::TableHeadersRow();

                for (const auto& e : d.gpt.entries) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (e.hasIndex) ImGui::Text("%llu", (unsigned long long)e.index);
                    else            ImGui::TextDisabled("-");
                    ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)e.start);
                    ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)e.size);
                    ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)(e.size * 512));
                    ImGui::TableNextColumn();
                    if (e.hasIndex && !e.uuid.empty())
                        ImGui::Text("%s - %s", e.contents.c_str(), e.uuid.c_str());
                    else
                        ImGui::TextUnformatted(e.contents.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("SMART")) {
            if (!d.smart.available) {
                ImGui::TextColored(COL_WARN, "%s",
                    d.smart.error.empty() ? "SMART unavailable" : d.smart.error.c_str());
                if (!d.smartStatus.empty())
                    ImGui::Text("diskutil: %s", d.smartStatus.c_str());
            } else {
                auto row = [](const char* k, long long v, const char* suf = "") {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
                    ImGui::TableNextColumn();
                    if (v < 0) ImGui::TextDisabled("-");
                    else ImGui::Text("%lld%s", v, suf);
                };
                if (ImGui::BeginTable("##smart", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_Resizable)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("Overall health");
                    ImGui::TableNextColumn();
                    if (d.smart.passed) badge("PASSED", COL_OK);
                    else                badge("FAILED", COL_ERR);
                    row("Temperature",         d.smart.temperature,     " C");
                    row("Power-on hours",      d.smart.powerOnHours);
                    row("Percentage used",     d.smart.percentageUsed,  " %");
                    row("Available spare",     d.smart.availableSpare,  " %");
                    row("Critical warning",    d.smart.criticalWarning);
                    row("Media errors",        d.smart.mediaErrors);
                    row("Unsafe shutdowns",    d.smart.unsafeShutdowns);
                    row("Reallocated sectors", d.smart.reallocated);
                    row("Pending sectors",     d.smart.pending);
                    ImGui::EndTable();
                }
                if (ImGui::CollapsingHeader("Raw smartctl output")) {
                    ImGui::BeginChild("##raw", ImVec2(0, 260), true);
                    ImGui::TextUnformatted(d.smart.raw.c_str());
                    ImGui::EndChild();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mounts")) {
            bool any = false;
            for (auto& m : s.mounts) {
                if (!isMountOnDisk(m, d, s)) continue;
                any = true;
                ImGui::Text("%s  ->  %s  (%s)",
                            m.device.c_str(), m.mountPoint.c_str(), m.fstype.c_str());
                usageBar(m.totalBytes - m.freeBytes, m.totalBytes);
            }
            if (!any) ImGui::TextDisabled("No mounted filesystems on this disk.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

static void drawPartDetailNode(const AppState& s, const DiskInfo& d, const PartitionInfo& p) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::Text("Partition: %s", p.devId.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (ImGui::BeginTable("##pinfo", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable)) {
        auto row = [](const char* k, const std::string& v) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
        };
        row("Disk",           d.devId);
        row("Device",         p.devId);
        row("Type",           p.content);
        row("Name",           p.volumeName);
        row("Size",           humanBytes(p.size));
        row("Mount point",    p.mountPoint);
        row("Volume UUID",    p.volumeUUID);
        row("Partition UUID", p.partitionUUID);
        ImGui::EndTable();
    }

    long long u = 0, t = 0;
    if (findPartUsage(s, p.devId, u, t)) {
        ImGui::Spacing();
        ImGui::TextDisabled("Usage:");
        usageBar(u, t);
    }

    if (p.content == "Apple_APFS") {
        if (auto* c = findContainerForPart(s, p.devId)) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("APFS Container");
            if (ImGui::BeginTable("##cinfo", 2,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable)) {
                auto row = [](const char* k, const std::string& v) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
                };
                row("UUID",           c->uuid);
                row("Reference",      c->reference);
                row("Physical store", c->store);
                row("Capacity",       humanBytes(c->ceiling));
                row("Free",           humanBytes(c->free));
                ImGui::EndTable();
            }
            usageBar(c->ceiling - c->free, c->ceiling);

            ImGui::Spacing();
            ImGui::TextDisabled("Volumes (%zu)", c->volumes.size());
            for (auto& v : c->volumes) {
                std::string roles = v.roles.empty() ? "" : " (" + v.roles + ")";
                std::string mnt   = v.mountPoint.empty() ? "" : " @" + v.mountPoint;
                ImGui::Text("  %s - %s%s%s", v.devId.c_str(), v.name.c_str(),
                            roles.c_str(), mnt.c_str());
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Extended properties")) {
        drawDetailTable("##pext", p.detail);
    }
}

static void drawVolDetailNode(const AppState&, const DiskInfo& d,
                              const PartitionInfo& p, const ApfsVolume& v) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::Text("APFS Volume: %s", v.name.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (ImGui::BeginTable("##vinfo", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable)) {
        auto row = [](const char* k, const std::string& val) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(val.empty() ? "-" : val.c_str());
        };
        row("Disk",           d.devId);
        row("Container",      p.devId);
        row("Device",         v.devId);
        row("Name",           v.name);
        row("Roles",          v.roles);
        row("Mount point",    v.mountPoint);
        row("Used",           humanBytes(v.inUse));
        row("Quota",          v.quota   > 0 ? humanBytes(v.quota)   : "");
        row("Reserve",        v.reserve > 0 ? humanBytes(v.reserve) : "");
        row("Block size",     std::to_string(v.blockSize));
        row("Volume UUID",    v.volumeUUID);
        row("Partition UUID", v.partitionUUID);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("State");
    bool any = false;
    auto flag = [&](bool c, const ImVec4& col, const char* txt) {
        if (!c) return;
        if (any) ImGui::SameLine();
        ImGui::TextColored(col, "%s", txt);
        any = true;
    };
    flag(v.encrypted,       COL_WARN,  "[encrypted]");
    flag(v.fileVault,       COL_OK,    "[FileVault]");
    flag(v.locked,          COL_ERR,   "[locked]");
    flag(v.sealed,          COL_MUTED, "[sealed]");
    flag(v.caseSensitive,   COL_MUTED, "[case-sensitive]");
    flag(v.cryptoMigration, COL_WARN,  "[crypto-migration]");
    if (!any) ImGui::TextDisabled("(none)");

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Extended properties")) {
        drawDetailTable("##vext", v.detail);
    }
}

static void drawDetailPanel(const AppState& s, UIState& ui) {
    auto& sel = ui.diskSel;
    if (sel.kind == DiskSel::None || sel.disk < 0 || sel.disk >= (int)s.disks.size()) {
        ImGui::TextDisabled("Select a disk, partition, or volume on the left.");
        return;
    }
    auto& d = s.disks[sel.disk];

    if (sel.kind == DiskSel::Disk) {
        drawDiskDetailNode(s, d);
    } else if (sel.kind == DiskSel::Part) {
        if (sel.part < 0 || sel.part >= (int)d.partitions.size()) return;
        drawPartDetailNode(s, d, d.partitions[sel.part]);
    } else if (sel.kind == DiskSel::Vol) {
        if (sel.part < 0 || sel.part >= (int)d.partitions.size()) return;
        auto& p = d.partitions[sel.part];
        auto* c = findContainerForPart(s, p.devId);
        if (!c || sel.vol < 0 || sel.vol >= (int)c->volumes.size()) return;
        drawVolDetailNode(s, d, p, c->volumes[sel.vol]);
    }
}

static void drawDisksTab(const AppState& s, UIState& ui) {
    if (s.disks.empty()) { ImGui::TextDisabled("No disks found."); return; }

    filterInput("##filterDisks", ui.filterDisks, sizeof(ui.filterDisks),
                "Search disks, partitions, volumes...");
    ImGui::Separator();

    drawDiskTree(s, ui);
    ImGui::SameLine();

    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    drawDetailPanel(s, ui);
    ImGui::EndChild();
}

// Partitions tab

static void drawPartitionsTab(const AppState& s, UIState& ui) {
    filterInput("##filterParts", ui.filterParts, sizeof(ui.filterParts),
                "Search by device, type, name...");
    ImGui::SameLine();
    ImGui::TextDisabled("| All partitions across all disks");
    ImGui::Separator();

    struct Row { const DiskInfo* disk; const PartitionInfo* part; };
    std::vector<Row> rows;
    for (auto& d : s.disks) {
        for (auto& p : d.partitions) {
            std::string hay = d.devId + " " + p.devId + " " + p.content + " " +
                              p.volumeName + " " + p.mountPoint;
            if (!containsCI(hay, ui.filterParts)) continue;
            rows.push_back({&d, &p});
        }
    }

    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;

    if (ImGui::BeginTable("##allparts", 8, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Disk",      ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Partition", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Size",      ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Usage",     ImGuiTableColumnFlags_WidthFixed, 170);
        ImGui::TableSetupColumn("Mounted at");
        ImGui::TableSetupColumn("UUID",      ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                auto& r = rows[(size_t)i];
                auto& p = *r.part;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r.disk->devId.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(p.devId.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(p.content.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    p.volumeName.empty() ? "-" : p.volumeName.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    humanBytes(p.size).c_str());
                ImGui::TableNextColumn();
                long long u = 0, t = 0;
                if (findPartUsage(s, p.devId, u, t)) usageBar(u, t);
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    p.mountPoint.empty() ? "-" : p.mountPoint.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    p.partitionUUID.empty() ? "-" : p.partitionUUID.c_str());
            }
        }
        ImGui::EndTable();
    }
}

// Filesystems tab

static void drawFilesystemsTab(const AppState& s, UIState& ui) {
    filterInput("##filterMounts", ui.filterMounts, sizeof(ui.filterMounts),
                "Search by device/mount/type...");
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu mounted filesystems", s.mounts.size());
    ImGui::Separator();

    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;

    if (ImGui::BeginTable("##mounts", 9, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Mount point");
        ImGui::TableSetupColumn("FS type",  ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Block",    ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("IO",       ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Usage",    ImGuiTableColumnFlags_WidthFixed, 210);
        ImGui::TableSetupColumn("Free",     ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Inodes",   ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Flags");
        ImGui::TableHeadersRow();

        std::vector<const MountedFS*> rows;
        for (auto& m : s.mounts) {
            std::string hay = m.device + " " + m.mountPoint + " " + m.fstype;
            if (!containsCI(hay, ui.filterMounts)) continue;
            rows.push_back(&m);
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                auto& m = *rows[(size_t)i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(m.device.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(m.mountPoint.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(m.fstype.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%lld", m.blockSize);
                ImGui::TableNextColumn(); ImGui::Text("%lld", m.ioSize);
                ImGui::TableNextColumn();
                usageBar(m.totalBytes - m.freeBytes, m.totalBytes);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    humanBytes(m.freeBytes).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%lld / %lld", m.freeFiles, m.totalFiles);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(
                    flagsToString(m.flags).c_str());
            }
        }
        ImGui::EndTable();
    }
}

// APFS tab

static void drawApfsTab(const AppState& s, UIState& ui) {
    filterInput("##filterApfs", ui.filterApfs, sizeof(ui.filterApfs),
                "Search by name, device, UUID...");
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu APFS containers", s.apfs.size());
    ImGui::Separator();

    if (s.apfs.empty()) { ImGui::TextDisabled("No APFS containers found."); return; }

    for (auto& c : s.apfs) {
        std::string header = c.reference + "  |  " + c.store + "  |  " +
            humanBytes(c.ceiling - c.free) + " / " + humanBytes(c.ceiling);
        if (!containsCI(header, ui.filterApfs) &&
            !containsCI(c.uuid, ui.filterApfs)) {
            bool hit = false;
            for (auto& v : c.volumes) {
                std::string hay = v.name + " " + v.devId + " " + v.roles + " " +
                                  v.volumeUUID + " " + v.partitionUUID + " " + v.mountPoint;
                if (containsCI(hay, ui.filterApfs)) { hit = true; break; }
            }
            if (!hit) continue;
        }

        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Container UUID: %s", c.uuid.c_str());
            usageBar(c.ceiling - c.free, c.ceiling);
            ImGui::Spacing();

            if (ImGui::BeginTable(("##vols" + c.reference).c_str(), 9,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Volume");
                ImGui::TableSetupColumn("Device");
                ImGui::TableSetupColumn("Roles");
                ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("Quota / Reserve", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Mount point");
                ImGui::TableSetupColumn("UUID", ImGuiTableColumnFlags_WidthFixed, 260);
                ImGui::TableSetupColumn("State");
                ImGui::TableHeadersRow();

                for (auto& v : c.volumes) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.name.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.devId.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        v.roles.empty() ? "-" : v.roles.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        humanBytes(v.inUse).c_str());
                    ImGui::TableNextColumn();
                    if (v.quota > 0 || v.reserve > 0)
                        ImGui::Text("%s / %s", humanBytes(v.quota).c_str(),
                                                humanBytes(v.reserve).c_str());
                    else ImGui::TextDisabled("-");
                    ImGui::TableNextColumn(); ImGui::Text("%lld", v.blockSize);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        v.mountPoint.empty() ? "-" : v.mountPoint.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(
                        v.volumeUUID.empty() ? "-" : v.volumeUUID.c_str());
                    ImGui::TableNextColumn();
                    bool printed = false;
                    auto fl = [&](bool cond, const ImVec4& col, const char* txt) {
                        if (!cond) return;
                        if (printed) ImGui::SameLine();
                        ImGui::TextColored(col, "%s", txt);
                        printed = true;
                    };
                    fl(v.encrypted,       COL_WARN,  "encrypted");
                    fl(v.fileVault,       COL_OK,    "FileVault");
                    fl(v.locked,          COL_ERR,   "locked");
                    fl(v.sealed,          COL_MUTED, "sealed");
                    fl(v.caseSensitive,   COL_MUTED, "case-sensitive");
                    fl(v.cryptoMigration, COL_WARN,  "crypto-migration");
                    if (!printed) ImGui::TextDisabled("-");
                }
                ImGui::EndTable();
            }
        }
    }
}

// Health tab

static void drawHealthTab(const AppState& s, UIState& ui) {
    filterInput("##filterHealth", ui.filterHealth, sizeof(ui.filterHealth), "Search by model/serial...");
    ImGui::SameLine();
    int healthy = 0, failing = 0, unknown = 0;
    for (auto& d : s.disks) {
        if (!d.smart.available) ++unknown;
        else if (d.smart.passed) ++healthy;
        else ++failing;
    }
    ImGui::TextDisabled("| ");
    ImGui::SameLine();
    ImGui::TextColored(COL_OK,  "%d OK", healthy);   ImGui::SameLine();
    ImGui::TextDisabled("/");
    ImGui::SameLine();
    ImGui::TextColored(COL_ERR, "%d FAIL", failing); ImGui::SameLine();
    ImGui::TextDisabled("/");
    ImGui::SameLine();
    ImGui::TextColored(COL_MUTED, "%d N/A", unknown);
    ImGui::Separator();

    std::vector<const DiskInfo*> rows;
    for (auto& d : s.disks) {
        std::string hay = d.devId + " " + d.model + " " + d.serial;
        if (!containsCI(hay, ui.filterHealth)) continue;
        rows.push_back(&d);
    }

    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;

    ImGui::BeginChild("##healtable", ImVec2(0, 220), true);
    if (ImGui::BeginTable("##health", 10, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Model");
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Temp",   ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Hours",  ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("%Used",  ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Spare",  ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Errors", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Shutdn", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Realloc",ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < rows.size(); ++i) {
            auto& d = *rows[i];
            bool selected = (ui.selectedHealthIdx == (int)i);
            ImGui::TableNextRow();
            if (ImGui::TableSetColumnIndex(0)) {
                if (ImGui::Selectable(d.devId.c_str(), selected,
                        ImGuiSelectableFlags_SpanAllColumns))
                    ui.selectedHealthIdx = (int)i;
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(d.model.c_str());
            ImGui::TableNextColumn();
            if (!d.smart.available)                 ImGui::TextColored(COL_MUTED, "N/A");
            else if (d.smart.passed)                ImGui::TextColored(COL_OK, "PASSED");
            else                                    ImGui::TextColored(COL_ERR, "FAILED");
            auto num = [](long long v, const char* suf = "") {
                if (v < 0) ImGui::TextDisabled("-");
                else       ImGui::Text("%lld%s", v, suf);
            };
            ImGui::TableNextColumn(); num(d.smart.temperature, "C");
            ImGui::TableNextColumn(); num(d.smart.powerOnHours);
            ImGui::TableNextColumn(); num(d.smart.percentageUsed, "%");
            ImGui::TableNextColumn(); num(d.smart.availableSpare, "%");
            ImGui::TableNextColumn(); num(d.smart.mediaErrors);
            ImGui::TableNextColumn(); num(d.smart.unsafeShutdowns);
            ImGui::TableNextColumn(); num(d.smart.reallocated);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    if (ui.selectedHealthIdx >= 0 && ui.selectedHealthIdx < (int)rows.size()) {
        auto& d = *rows[(size_t)ui.selectedHealthIdx];
        ImGui::TextColored(COL_ACCENT, "SMART Details: %s (%s)",
                           d.devId.c_str(), d.model.c_str());
        if (!d.smart.available) {
            ImGui::TextColored(COL_WARN, "%s",
                d.smart.error.empty() ? "no data" : d.smart.error.c_str());
        } else {
            ImGui::BeginChild("##smartraw", ImVec2(0, 0), true);
            ImGui::TextUnformatted(d.smart.raw.c_str());
            ImGui::EndChild();
        }
    } else {
        ImGui::TextDisabled("Select a disk in the table for details.");
    }
}

// Main

int main() {
    AppState state;
    UIState  ui;
    refreshAll(state);

    sf::RenderWindow window(sf::VideoMode({1200, 820}), "Disk Info");
    window.setFramerateLimit(60);

    ImGui::SFML::Init(window);

    sf::Clock frameClock;

    while (window.isOpen()) {
        while (auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);
            if (event->is<sf::Event::Closed>()) window.close();
        }
        ImGui::SFML::Update(window, frameClock.restart());

        ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
        auto ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ds, ImGuiCond_Always);
        ImGui::Begin("##main", nullptr, wf);

        if (ImGui::Button("Refresh")) {
            refreshAll(state);
            ui.diskSel = {};
            ui.selectedHealthIdx = -1;
            ui.toast = "Data refreshed";
            ui.toastUntil = ImGui::GetTime() + 2.0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| Updated: %s", state.lastUpdate.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| Disks: %zu   Volumes: %zu   APFS: %zu",
                            state.disks.size(), state.mounts.size(), state.apfs.size());
        if (!state.lastError.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(COL_ERR, "| %s", state.lastError.c_str());
        }
        ImGui::Separator();

        if (!ui.toast.empty() && ImGui::GetTime() < ui.toastUntil) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_OK);
            ImGui::TextUnformatted(ui.toast.c_str());
            ImGui::PopStyleColor();
        } else if (ImGui::GetTime() >= ui.toastUntil) {
            ui.toast.clear();
        }
        if (!ui.toast.empty()) ImGui::Separator();

        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Disks"))       { drawDisksTab(state, ui);       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Partitions"))  { drawPartitionsTab(state, ui);  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Filesystems")) { drawFilesystemsTab(state, ui); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("APFS"))        { drawApfsTab(state, ui);        ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Health"))      { drawHealthTab(state, ui);      ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        ImGui::End();
        window.clear(sf::Color(28, 28, 30));
        ImGui::SFML::Render(window);
        window.display();
    }
    ImGui::SFML::Shutdown();
    return 0;
}