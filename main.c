// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

#define VERSION "0.3.0"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreVideo/CoreVideo.h>

#include "vendor/cjson/cJSON.h"

// ---------------------------------------------------------------------------
// System info collectors
// ---------------------------------------------------------------------------

// CPU info: core counts, P/E split (Apple Silicon), brand string, frequency.
static cJSON *collect_cpu(void) {
    cJSON *cpu = cJSON_CreateObject();

    // Physical and logical core counts
    int phys = 0, logical = 0;
    size_t sz = sizeof(int);
    sysctlbyname("hw.physicalcpu", &phys, &sz, NULL, 0);
    sz = sizeof(int);
    sysctlbyname("hw.logicalcpu", &logical, &sz, NULL, 0);
    cJSON_AddNumberToObject(cpu, "physical_cores", phys);
    cJSON_AddNumberToObject(cpu, "logical_cores", logical);

    // Performance and efficiency core counts (Apple Silicon)
    int perflevel_count = 0;
    sz = sizeof(int);
    if (sysctlbyname("hw.nperflevels", &perflevel_count, &sz, NULL, 0) == 0 &&
        perflevel_count > 0) {
        // Level 0 = performance, level 1 = efficiency
        int pcores = 0, ecores = 0;
        sz = sizeof(int);
        sysctlbyname("hw.perflevel0.logicalcpu", &pcores, &sz, NULL, 0);
        sz = sizeof(int);
        sysctlbyname("hw.perflevel1.logicalcpu", &ecores, &sz, NULL, 0);
        cJSON_AddNumberToObject(cpu, "performance_cores", pcores);
        cJSON_AddNumberToObject(cpu, "efficiency_cores", ecores);
    }

    // Brand string
    char brand[256] = {0};
    sz = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &sz, NULL, 0) == 0) {
        cJSON_AddStringToObject(cpu, "brand", brand);
    }

    // CPU frequency (may not be available on Apple Silicon)
    int64_t freq = 0;
    sz = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency", &freq, &sz, NULL, 0) == 0 && freq > 0) {
        cJSON_AddNumberToObject(cpu, "frequency_hz", (double)freq);
    }

    return cpu;
}

// Memory stats: total, free, used, compressed bytes via vm_statistics64.
static cJSON *collect_memory(void) {
    cJSON *mem = cJSON_CreateObject();

    // Total physical memory
    int64_t total = 0;
    size_t sz = sizeof(total);
    sysctlbyname("hw.memsize", &total, &sz, NULL, 0);
    cJSON_AddNumberToObject(mem, "total_bytes", (double)total);

    // Memory pressure via vm_statistics64
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
        vm_size_t page_size;
        host_page_size(mach_host_self(), &page_size);

        int64_t free_pages = vm_stat.free_count + vm_stat.inactive_count;
        int64_t used_pages = vm_stat.active_count + vm_stat.wire_count
                           + vm_stat.compressor_page_count;
        cJSON_AddNumberToObject(mem, "free_bytes",
                                (double)(free_pages * page_size));
        cJSON_AddNumberToObject(mem, "used_bytes",
                                (double)(used_pages * page_size));
        cJSON_AddNumberToObject(mem, "compressed_bytes",
                                (double)((int64_t)vm_stat.compressor_page_count
                                         * page_size));
    }

    return mem;
}

// GPU info via IOKit IOAccelerator: model, VRAM (discrete), core count (Apple Silicon).
static cJSON *collect_gpu(void) {
    cJSON *gpus = cJSON_CreateArray();

    // Query IOKit for GPU accelerators
    CFMutableDictionaryRef match = IOServiceMatching("IOAccelerator");
    if (!match) return gpus;

    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter)
        != KERN_SUCCESS) {
        return gpus;
    }

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        cJSON *gpu = cJSON_CreateObject();

        CFStringRef name = (CFStringRef)IORegistryEntrySearchCFProperty(
            service, kIOServicePlane, CFSTR("model"),
            kCFAllocatorDefault,
            kIORegistryIterateRecursively | kIORegistryIterateParents);
        if (name) {
            char buf[256];
            if (CFGetTypeID(name) == CFDataGetTypeID()) {
                // model is often CFData, not CFString
                CFDataRef data = (CFDataRef)name;
                CFIndex len = CFDataGetLength(data);
                if (len > (CFIndex)(sizeof(buf) - 1))
                    len = sizeof(buf) - 1;
                CFDataGetBytes(data, CFRangeMake(0, len), (UInt8 *)buf);
                buf[len] = '\0';
                cJSON_AddStringToObject(gpu, "model", buf);
            } else if (CFStringGetCString(name, buf, sizeof(buf),
                                          kCFStringEncodingUTF8)) {
                cJSON_AddStringToObject(gpu, "model", buf);
            }
            CFRelease(name);
        }

        // VRAM (may be reported for discrete GPUs)
        CFNumberRef vram = (CFNumberRef)IORegistryEntrySearchCFProperty(
            service, kIOServicePlane, CFSTR("VRAM,totalMB"),
            kCFAllocatorDefault,
            kIORegistryIterateRecursively | kIORegistryIterateParents);
        if (vram) {
            int64_t mb = 0;
            CFNumberGetValue(vram, kCFNumberSInt64Type, &mb);
            if (mb > 0) {
                cJSON_AddNumberToObject(gpu, "vram_mb", (double)mb);
            }
            CFRelease(vram);
        }

        // GPU core count (Apple Silicon)
        CFNumberRef cores = (CFNumberRef)IORegistryEntrySearchCFProperty(
            service, kIOServicePlane, CFSTR("gpu-core-count"),
            kCFAllocatorDefault,
            kIORegistryIterateRecursively | kIORegistryIterateParents);
        if (cores) {
            int64_t n = 0;
            CFNumberGetValue(cores, kCFNumberSInt64Type, &n);
            if (n > 0) {
                cJSON_AddNumberToObject(gpu, "core_count", (double)n);
            }
            CFRelease(cores);
        }

        if (cJSON_GetArraySize(gpu) > 0) {
            cJSON_AddItemToArray(gpus, gpu);
        } else {
            cJSON_Delete(gpu);
        }

        IOObjectRelease(service);
    }
    IOObjectRelease(iter);

    return gpus;
}

// Disk usage for root filesystem via statvfs.
static cJSON *collect_disk(void) {
    cJSON *disks = cJSON_CreateArray();

    // Root filesystem
    struct statvfs st;
    if (statvfs("/", &st) == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "mount", "/");
        double total = (double)st.f_blocks * st.f_frsize;
        double free = (double)st.f_bavail * st.f_frsize;
        cJSON_AddNumberToObject(root, "total_bytes", total);
        cJSON_AddNumberToObject(root, "free_bytes", free);
        cJSON_AddNumberToObject(root, "used_bytes", total - free);
        cJSON_AddItemToArray(disks, root);
    }

    return disks;
}

// OS info: uname fields, macOS version, hostname, boot time.
static cJSON *collect_os(void) {
    cJSON *os = cJSON_CreateObject();

    struct utsname u;
    if (uname(&u) == 0) {
        cJSON_AddStringToObject(os, "sysname", u.sysname);
        cJSON_AddStringToObject(os, "release", u.release);
        cJSON_AddStringToObject(os, "version", u.version);
        cJSON_AddStringToObject(os, "machine", u.machine);
    }

    // macOS product version
    char version[64] = {0};
    size_t sz = sizeof(version);
    if (sysctlbyname("kern.osproductversion", version, &sz, NULL, 0) == 0) {
        cJSON_AddStringToObject(os, "macos_version", version);
    }

    // Hostname
    char hostname[256] = {0};
    sz = sizeof(hostname);
    if (sysctlbyname("kern.hostname", hostname, &sz, NULL, 0) == 0) {
        cJSON_AddStringToObject(os, "hostname", hostname);
    }

    // Boot time
    struct timeval boottime;
    sz = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &sz, NULL, 0) == 0) {
        cJSON_AddNumberToObject(os, "boot_time_unix", (double)boottime.tv_sec);
    }

    return os;
}

// Network interfaces: name, IPv4/IPv6, MAC, primary flag, router.
static cJSON *collect_network(void) {
    cJSON *interfaces = cJSON_CreateArray();

    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return interfaces;

    // First pass: collect interface names with their addresses
    // Group by interface name
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6 && family != AF_LINK)
            continue;

        // Find or create interface entry
        cJSON *iface = NULL;
        int n = cJSON_GetArraySize(interfaces);
        for (int i = 0; i < n; i++) {
            cJSON *entry = cJSON_GetArrayItem(interfaces, i);
            cJSON *nm = cJSON_GetObjectItem(entry, "name");
            if (nm && strcmp(nm->valuestring, ifa->ifa_name) == 0) {
                iface = entry;
                break;
            }
        }
        if (!iface) {
            iface = cJSON_CreateObject();
            cJSON_AddStringToObject(iface, "name", ifa->ifa_name);
            cJSON_AddItemToArray(interfaces, iface);
        }

        if (family == AF_INET) {
            if (!cJSON_GetObjectItem(iface, "ipv4")) {
                char addr[INET_ADDRSTRLEN];
                struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof(addr));
                cJSON_AddStringToObject(iface, "ipv4", addr);
            }
        } else if (family == AF_INET6) {
            char addr[INET6_ADDRSTRLEN];
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &sa6->sin6_addr, addr, sizeof(addr));
            // Only add the first IPv6 (skip link-local fe80:: clutter)
            if (!cJSON_GetObjectItem(iface, "ipv6") &&
                strncmp(addr, "fe80:", 5) != 0) {
                cJSON_AddStringToObject(iface, "ipv6", addr);
            }
        } else if (family == AF_LINK) {
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
            if (sdl->sdl_alen == 6) {
                unsigned char *mac = (unsigned char *)LLADDR(sdl);
                char macstr[18];
                snprintf(macstr, sizeof(macstr),
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                // Skip zero MACs
                if (strcmp(macstr, "00:00:00:00:00:00") != 0) {
                    cJSON_AddStringToObject(iface, "mac", macstr);
                }
            }
        }
    }
    freeifaddrs(ifap);

    // Add Wi-Fi SSID if available (via CoreWLAN through SystemConfiguration)
    SCDynamicStoreRef store = SCDynamicStoreCreate(
        NULL, CFSTR("sysinfo-mcp"), NULL, NULL);
    if (store) {
        CFStringRef key = SCDynamicStoreKeyCreateNetworkInterface(
            NULL, kSCDynamicStoreDomainState);
        CFPropertyListRef val = SCDynamicStoreCopyValue(store, key);
        CFRelease(key);
        if (val) CFRelease(val);

        // Get primary interface and its IPv4 router
        CFStringRef globalKey = SCDynamicStoreKeyCreateNetworkGlobalEntity(
            NULL, kSCDynamicStoreDomainState, kSCEntNetIPv4);
        CFDictionaryRef global = SCDynamicStoreCopyValue(store, globalKey);
        CFRelease(globalKey);
        if (global) {
            CFStringRef primary = CFDictionaryGetValue(global,
                kSCDynamicStorePropNetPrimaryInterface);
            if (primary) {
                char pname[64];
                CFStringGetCString(primary, pname, sizeof(pname),
                                   kCFStringEncodingUTF8);
                // Mark the primary interface
                int n = cJSON_GetArraySize(interfaces);
                for (int i = 0; i < n; i++) {
                    cJSON *entry = cJSON_GetArrayItem(interfaces, i);
                    cJSON *nm = cJSON_GetObjectItem(entry, "name");
                    if (nm && strcmp(nm->valuestring, pname) == 0) {
                        cJSON_AddBoolToObject(entry, "primary", 1);
                        break;
                    }
                }

                // Get router
                CFStringRef router = CFDictionaryGetValue(global,
                    kSCEntNetIPv4);
                if (router) {
                    // Router is in the Router key
                    CFArrayRef routers = CFDictionaryGetValue(global,
                        CFSTR("Router"));
                    if (routers && CFGetTypeID(routers) == CFStringGetTypeID()) {
                        char rbuf[64];
                        CFStringGetCString((CFStringRef)routers, rbuf,
                                           sizeof(rbuf),
                                           kCFStringEncodingUTF8);
                        // Add to primary interface
                        int nn = cJSON_GetArraySize(interfaces);
                        for (int j = 0; j < nn; j++) {
                            cJSON *entry = cJSON_GetArrayItem(interfaces, j);
                            cJSON *nm = cJSON_GetObjectItem(entry, "name");
                            if (nm && strcmp(nm->valuestring, pname) == 0) {
                                cJSON_AddStringToObject(entry, "router", rbuf);
                                break;
                            }
                        }
                    }
                }
            }
            CFRelease(global);
        }
        CFRelease(store);
    }

    return interfaces;
}

// Battery/power info via AppleSmartBattery IOKit service.
static cJSON *collect_power(void) {
    cJSON *power = cJSON_CreateObject();

    // Find the AppleSmartBattery service
    io_service_t battery = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"));
    if (battery != IO_OBJECT_NULL) {
        cJSON_AddBoolToObject(power, "has_battery", 1);

        // CurrentCapacity/MaxCapacity are percentages on modern macOS.
        // AppleRawCurrentCapacity/AppleRawMaxCapacity are actual mAh.
        CFNumberRef pct = IORegistryEntryCreateCFProperty(
            battery, CFSTR("CurrentCapacity"), NULL, 0);
        if (pct) {
            int p = 0;
            CFNumberGetValue(pct, kCFNumberIntType, &p);
            cJSON_AddNumberToObject(power, "battery_percent", p);
            CFRelease(pct);
        }

        CFNumberRef rawcur = IORegistryEntryCreateCFProperty(
            battery, CFSTR("AppleRawCurrentCapacity"), NULL, 0);
        if (rawcur) {
            int mah = 0;
            CFNumberGetValue(rawcur, kCFNumberIntType, &mah);
            cJSON_AddNumberToObject(power, "capacity_mah", mah);
            CFRelease(rawcur);
        }

        CFNumberRef rawmax = IORegistryEntryCreateCFProperty(
            battery, CFSTR("AppleRawMaxCapacity"), NULL, 0);
        if (rawmax) {
            int mah = 0;
            CFNumberGetValue(rawmax, kCFNumberIntType, &mah);
            cJSON_AddNumberToObject(power, "max_capacity_mah", mah);
            CFRelease(rawmax);
        }

        CFNumberRef designcap = IORegistryEntryCreateCFProperty(
            battery, CFSTR("DesignCapacity"), NULL, 0);
        if (designcap && rawmax) {
            int design = 0, max = 0;
            CFNumberGetValue(designcap, kCFNumberIntType, &design);
            CFNumberGetValue(rawmax, kCFNumberIntType, &max);
            if (design > 0) {
                cJSON_AddNumberToObject(power, "battery_health_percent",
                                        (double)max * 100.0 / design);
            }
        }
        if (designcap) CFRelease(designcap);
        if (rawmax) CFRelease(rawmax);

        CFBooleanRef charging = IORegistryEntryCreateCFProperty(
            battery, CFSTR("IsCharging"), NULL, 0);
        if (charging) {
            cJSON_AddBoolToObject(power, "charging",
                                  CFBooleanGetValue(charging));
            CFRelease(charging);
        }

        CFBooleanRef external = IORegistryEntryCreateCFProperty(
            battery, CFSTR("ExternalConnected"), NULL, 0);
        if (external) {
            cJSON_AddStringToObject(power, "power_source",
                                    CFBooleanGetValue(external)
                                        ? "ac" : "battery");
            CFRelease(external);
        }

        CFNumberRef cycles = IORegistryEntryCreateCFProperty(
            battery, CFSTR("CycleCount"), NULL, 0);
        if (cycles) {
            int n = 0;
            CFNumberGetValue(cycles, kCFNumberIntType, &n);
            cJSON_AddNumberToObject(power, "cycle_count", n);
            CFRelease(cycles);
        }

        CFNumberRef timeleft = IORegistryEntryCreateCFProperty(
            battery, CFSTR("TimeRemaining"), NULL, 0);
        if (timeleft) {
            int mins = 0;
            CFNumberGetValue(timeleft, kCFNumberIntType, &mins);
            if (mins > 0 && mins < 6000) {
                cJSON_AddNumberToObject(power, "time_remaining_minutes", mins);
            }
            CFRelease(timeleft);
        }

        IOObjectRelease(battery);
    } else {
        cJSON_AddBoolToObject(power, "has_battery", 0);
        cJSON_AddStringToObject(power, "power_source", "ac");
    }

    return power;
}

// Convert a CFString to a malloc'd UTF-8 C string. Caller frees.
static char *cfstring_to_cstring(CFStringRef s) {
    if (!s || CFGetTypeID(s) != CFStringGetTypeID()) return NULL;
    CFIndex max = CFStringGetMaximumSizeForEncoding(
        CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    char *out = malloc(max);
    if (out && !CFStringGetCString(s, out, max, kCFStringEncodingUTF8)) {
        free(out);
        out = NULL;
    }
    return out;
}

// Read SInt32 out of a CFNumberRef property; returns 0 if absent or wrong type.
static SInt32 dict_int(CFDictionaryRef d, CFStringRef key) {
    if (!d) return 0;
    CFNumberRef n = CFDictionaryGetValue(d, key);
    if (!n || CFGetTypeID(n) != CFNumberGetTypeID()) return 0;
    SInt32 v = 0;
    CFNumberGetValue(n, kCFNumberSInt32Type, &v);
    return v;
}

// Read SInt64 (for ProductID values that don't fit in 32 bits).
static SInt64 dict_int64(CFDictionaryRef d, CFStringRef key) {
    if (!d) return 0;
    CFNumberRef n = CFDictionaryGetValue(d, key);
    if (!n || CFGetTypeID(n) != CFNumberGetTypeID()) return 0;
    SInt64 v = 0;
    CFNumberGetValue(n, kCFNumberSInt64Type, &v);
    return v;
}

// Localized product name for a display, or NULL if unavailable. Caller frees.
//
// Walks the full IORegistry. CGDisplayIOServicePort is deprecated and returns
// 0 on macOS 10.15+, and `IODisplayConnect` services don't exist on Apple
// Silicon. Two property shapes are checked, covering both eras:
//   1. Intel-era IODisplayConnect: `DisplayProductName` (localized dict)
//      paired with `DisplayVendorID`/`DisplayProductID` integers.
//   2. Apple-Silicon IOMobileFramebuffer: `DisplayAttributes.ProductAttributes`
//      containing `ProductName` (CFString) plus `LegacyManufacturerID` and
//      `ProductID` integers.
static char *display_name(CGDirectDisplayID display_id) {
    uint32_t want_vendor  = CGDisplayVendorNumber(display_id);
    uint32_t want_product = CGDisplayModelNumber(display_id);

    io_iterator_t iter;
    if (IORegistryCreateIterator(kIOMainPortDefault, kIOServicePlane,
                                 kIORegistryIterateRecursively, &iter)
        != KERN_SUCCESS) {
        return NULL;
    }

    char *result = NULL;
    io_service_t serv;
    while (!result && (serv = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        CFMutableDictionaryRef props = NULL;
        if (IORegistryEntryCreateCFProperties(
                serv, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {

            // Shape 1: classic IODisplayConnect.
            CFDictionaryRef names = CFDictionaryGetValue(
                props, CFSTR(kDisplayProductName));
            if (names && CFGetTypeID(names) == CFDictionaryGetTypeID() &&
                (uint32_t)dict_int(props, CFSTR(kDisplayVendorID))
                    == want_vendor &&
                (uint32_t)dict_int(props, CFSTR(kDisplayProductID))
                    == want_product &&
                CFDictionaryGetCount(names) > 0) {
                CFIndex count = CFDictionaryGetCount(names);
                const void **values = malloc(count * sizeof(void *));
                if (values) {
                    CFDictionaryGetKeysAndValues(names, NULL, values);
                    result = cfstring_to_cstring((CFStringRef)values[0]);
                    free(values);
                }
            }

            // Shape 2: Apple-Silicon IOMobileFramebuffer.
            if (!result) {
                CFDictionaryRef attrs = CFDictionaryGetValue(
                    props, CFSTR("DisplayAttributes"));
                if (attrs && CFGetTypeID(attrs) == CFDictionaryGetTypeID()) {
                    CFDictionaryRef pa = CFDictionaryGetValue(
                        attrs, CFSTR("ProductAttributes"));
                    if (pa && CFGetTypeID(pa) == CFDictionaryGetTypeID() &&
                        (uint32_t)dict_int(pa, CFSTR("LegacyManufacturerID"))
                            == want_vendor &&
                        (uint32_t)dict_int64(pa, CFSTR("ProductID"))
                            == want_product) {
                        CFStringRef nm = CFDictionaryGetValue(
                            pa, CFSTR("ProductName"));
                        result = cfstring_to_cstring(nm);
                    }
                }
            }

            CFRelease(props);
        }
        IOObjectRelease(serv);
    }
    IOObjectRelease(iter);
    return result;
}

// CGDisplayModeGetRefreshRate returns 0 for built-in displays on some macOS
// versions. Fall back to CoreVideo's nominal output refresh period.
static double display_refresh_fallback(CGDirectDisplayID display_id) {
    double hz = 0.0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CVDisplayLinkRef link = NULL;
    if (CVDisplayLinkCreateWithCGDisplay(display_id, &link) == kCVReturnSuccess
        && link) {
        CVTime period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
        if (!(period.flags & kCVTimeIsIndefinite) && period.timeValue > 0) {
            hz = (double)period.timeScale / (double)period.timeValue;
        }
        CVDisplayLinkRelease(link);
    }
#pragma clang diagnostic pop
    return hz;
}

// Display configuration: per-monitor name, resolution, refresh, scale, main.
static cJSON *collect_display(void) {
    cJSON *displays = cJSON_CreateArray();

    CGDirectDisplayID active[16];
    uint32_t count = 0;
    if (CGGetActiveDisplayList(16, active, &count) != kCGErrorSuccess)
        return displays;

    CGDirectDisplayID main_id = CGMainDisplayID();

    for (uint32_t i = 0; i < count; i++) {
        CGDirectDisplayID id = active[i];
        cJSON *d = cJSON_CreateObject();

        char *nm = display_name(id);
        if (nm) {
            cJSON_AddStringToObject(d, "name", nm);
            free(nm);
        } else if (CGDisplayIsBuiltin(id)) {
            // Apple Silicon built-in panels don't expose a `ProductName` via
            // public IOKit APIs — fall back to a generic label.
            cJSON_AddStringToObject(d, "name", "Built-in Display");
        }

        cJSON_AddNumberToObject(d, "id", (double)id);
        cJSON_AddBoolToObject(d, "main", id == main_id);

        const char *connection;
        if (CGDisplayMirrorsDisplay(id) != kCGNullDirectDisplay) {
            connection = "mirrored";
        } else if (CGDisplayIsBuiltin(id)) {
            connection = "internal";
        } else {
            connection = "external";
        }
        cJSON_AddStringToObject(d, "connection", connection);

        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(id);
        if (mode) {
            size_t pw = CGDisplayModeGetPixelWidth(mode);
            size_t ph = CGDisplayModeGetPixelHeight(mode);
            size_t lw = CGDisplayModeGetWidth(mode);
            size_t lh = CGDisplayModeGetHeight(mode);

            cJSON *res_px = cJSON_CreateArray();
            cJSON_AddItemToArray(res_px, cJSON_CreateNumber((double)pw));
            cJSON_AddItemToArray(res_px, cJSON_CreateNumber((double)ph));
            cJSON_AddItemToObject(d, "resolution_pixels", res_px);

            cJSON *res_log = cJSON_CreateArray();
            cJSON_AddItemToArray(res_log, cJSON_CreateNumber((double)lw));
            cJSON_AddItemToArray(res_log, cJSON_CreateNumber((double)lh));
            cJSON_AddItemToObject(d, "resolution_logical", res_log);

            if (lw > 0) {
                cJSON_AddNumberToObject(d, "scale",
                                        (double)pw / (double)lw);
            }

            double hz = CGDisplayModeGetRefreshRate(mode);
            if (hz <= 0.0) hz = display_refresh_fallback(id);
            if (hz > 0.0) cJSON_AddNumberToObject(d, "refresh_hz", hz);

            // Refresh range across all modes at the same pixel resolution.
            CFArrayRef all_modes = CGDisplayCopyAllDisplayModes(id, NULL);
            if (all_modes) {
                double min_hz = 0.0, max_hz = 0.0;
                CFIndex n_modes = CFArrayGetCount(all_modes);
                for (CFIndex j = 0; j < n_modes; j++) {
                    CGDisplayModeRef m = (CGDisplayModeRef)
                        CFArrayGetValueAtIndex(all_modes, j);
                    if (CGDisplayModeGetPixelWidth(m) != pw ||
                        CGDisplayModeGetPixelHeight(m) != ph)
                        continue;
                    double r = CGDisplayModeGetRefreshRate(m);
                    if (r <= 0.0) continue;
                    if (min_hz == 0.0 || r < min_hz) min_hz = r;
                    if (r > max_hz) max_hz = r;
                }
                CFRelease(all_modes);
                if (min_hz > 0.0 && max_hz > min_hz) {
                    cJSON *range = cJSON_CreateArray();
                    cJSON_AddItemToArray(range, cJSON_CreateNumber(min_hz));
                    cJSON_AddItemToArray(range, cJSON_CreateNumber(max_hz));
                    cJSON_AddItemToObject(d, "refresh_range_hz", range);
                }
            }

            CGDisplayModeRelease(mode);
        }

        cJSON_AddItemToArray(displays, d);
    }

    return displays;
}

// Thermal pressure level via kern.thermalpressure sysctl.
static cJSON *collect_thermal(void) {
    cJSON *thermal = cJSON_CreateObject();

    // Thermal pressure via host_statistics
    // This uses the older thermal_pressure sysctl if available
    int pressure = 0;
    size_t sz = sizeof(pressure);
    if (sysctlbyname("kern.thermalpressure", &pressure, &sz, NULL, 0) == 0) {
        const char *level;
        switch (pressure) {
        case 0: level = "nominal"; break;
        case 1: level = "moderate"; break;
        case 2: level = "heavy"; break;
        case 3: level = "critical"; break;
        default: level = "unknown"; break;
        }
        cJSON_AddStringToObject(thermal, "pressure", level);
    }

    return thermal;
}


// ---------------------------------------------------------------------------
// MCP JSON-RPC protocol
// ---------------------------------------------------------------------------

static void send_response(cJSON *id, cJSON *result) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    cJSON_AddItemToObject(resp, "result", result);
    char *out = cJSON_PrintUnformatted(resp);
    fprintf(stdout, "%s\n", out);
    fflush(stdout);
    free(out);
    cJSON_Delete(resp);
}

static void send_error(cJSON *id, int code, const char *message) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(resp, "id");
    }
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    cJSON_AddItemToObject(resp, "error", err);
    char *out = cJSON_PrintUnformatted(resp);
    fprintf(stdout, "%s\n", out);
    fflush(stdout);
    free(out);
    cJSON_Delete(resp);
}

static cJSON *handle_initialize(void) {
    cJSON *result = cJSON_CreateObject();

    cJSON *capabilities = cJSON_CreateObject();
    cJSON *tools_cap = cJSON_CreateObject();
    cJSON_AddItemToObject(capabilities, "tools", tools_cap);
    cJSON_AddItemToObject(result, "capabilities", capabilities);

    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", "sysinfo");
    cJSON_AddStringToObject(info, "version", VERSION);
    cJSON_AddItemToObject(result, "serverInfo", info);

    cJSON_AddStringToObject(result, "protocolVersion", "2024-11-05");

    return result;
}

static cJSON *handle_tools_list(void) {
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();

    // system_info tool
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "name", "system_info");
    cJSON_AddStringToObject(tool, "description",
        "Report hardware and OS information: CPU (cores, brand, P/E split), "
        "memory (total, free, used, compressed), GPU (model, cores, VRAM), "
        "disk (total, free, used), OS (version, hostname, uptime), display "
        "(per-monitor resolution, refresh, scale, main flag), and thermal "
        "pressure. Pass 'categories' to select specific sections "
        "(e.g. [\"cpu\", \"memory\"]) or omit for all.");

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *props = cJSON_CreateObject();
    cJSON *cat = cJSON_CreateObject();
    cJSON_AddStringToObject(cat, "type", "array");
    cJSON_AddStringToObject(cat, "description",
        "Sections to include. Omit for all.");
    cJSON *items = cJSON_CreateObject();
    cJSON_AddStringToObject(items, "type", "string");
    cJSON *cat_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("cpu"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("memory"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("gpu"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("disk"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("os"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("network"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("power"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("thermal"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("display"));
    cJSON_AddItemToObject(items, "enum", cat_enum);
    cJSON_AddItemToObject(cat, "items", items);
    cJSON_AddItemToObject(props, "categories", cat);

    cJSON_AddItemToObject(schema, "properties", props);
    cJSON_AddItemToObject(tool, "inputSchema", schema);
    cJSON_AddItemToArray(tools, tool);

    cJSON_AddItemToObject(result, "tools", tools);
    return result;
}

static cJSON *handle_tools_call(cJSON *params) {
    cJSON *name = cJSON_GetObjectItem(params, "name");
    if (!name || strcmp(name->valuestring, "system_info") != 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON *content = cJSON_CreateArray();
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "type", "text");
        cJSON_AddStringToObject(item, "text", "Unknown tool");
        cJSON_AddItemToArray(content, item);
        cJSON_AddItemToObject(result, "content", content);
        cJSON_AddBoolToObject(result, "isError", 1);
        return result;
    }

    // Parse categories array (omit = all)
    cJSON *args = cJSON_GetObjectItem(params, "arguments");
    cJSON *cats = args ? cJSON_GetObjectItem(args, "categories") : NULL;

    // Evaluates to 1 if the named category is requested (present in cats array, or cats is absent/not an array meaning 'all').
    #define WANT(name) ({ \
        int _w = 1; \
        if (cats && cJSON_IsArray(cats)) { \
            _w = 0; \
            cJSON *_c; \
            cJSON_ArrayForEach(_c, cats) { \
                if (cJSON_IsString(_c) && strcmp(_c->valuestring, name) == 0) \
                    { _w = 1; break; } \
            } \
        } \
        _w; \
    })

    cJSON *data = cJSON_CreateObject();
    if (WANT("cpu"))     cJSON_AddItemToObject(data, "cpu", collect_cpu());
    if (WANT("memory"))  cJSON_AddItemToObject(data, "memory", collect_memory());
    if (WANT("gpu"))     cJSON_AddItemToObject(data, "gpu", collect_gpu());
    if (WANT("disk"))    cJSON_AddItemToObject(data, "disk", collect_disk());
    if (WANT("os"))      cJSON_AddItemToObject(data, "os", collect_os());
    if (WANT("network")) cJSON_AddItemToObject(data, "network", collect_network());
    if (WANT("power"))   cJSON_AddItemToObject(data, "power", collect_power());
    if (WANT("thermal")) cJSON_AddItemToObject(data, "thermal", collect_thermal());
    if (WANT("display")) cJSON_AddItemToObject(data, "display", collect_display());

    #undef WANT

    char *text = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);

    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    free(text);
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(result, "content", content);

    return result;
}

static const char HELP[] =
    "sysinfo-mcp " VERSION " — MCP server for macOS system information\n"
    "\n"
    "Usage: sysinfo-mcp [OPTIONS]\n"
    "\n"
    "Runs an MCP server on stdio, exposing a single tool (system_info)\n"
    "that reports CPU, memory, GPU, disk, OS, network, power, thermal, and\n"
    "display information.\n"
    "\n"
    "Options:\n"
    "  --version      Print version and exit\n"
    "  --help         Print this help and exit\n"
    "  --help-agent   Print agent guide and exit\n";

static const char AGENT_GUIDE[] =
    "# Agent's Guide: sysinfo-mcp\n"
    "\n"
    "## What this is\n"
    "\n"
    "A C MCP server for macOS that reports system hardware/OS info over stdio JSON-RPC (protocol version 2024-11-05). Exposes a single tool, `system_info`, with an optional `categories` array parameter for selective reporting. No categories = all categories.\n"
    "\n"
    "## Repository layout\n"
    "\n"
    "```\n"
    "main.c              — entire implementation (collectors + MCP protocol + main loop)\n"
    "vendor/cjson/       — vendored cJSON library\n"
    "Makefile            — build entry point\n"
    "```\n"
    "\n"
    "## Architecture\n"
    "\n"
    "**Collector functions** (`main.c:26–489`) — one per category, each returns a `cJSON*`:\n"
    "\n"
    "| Function | Category key | Returns |\n"
    "|---|---|---|\n"
    "| `collect_cpu()` | `\"cpu\"` | object |\n"
    "| `collect_memory()` | `\"memory\"` | object |\n"
    "| `collect_gpu()` | `\"gpu\"` | array of objects |\n"
    "| `collect_disk()` | `\"disk\"` | array of objects |\n"
    "| `collect_os()` | `\"os\"` | object |\n"
    "| `collect_network()` | `\"network\"` | array of interface objects |\n"
    "| `collect_power()` | `\"power\"` | object |\n"
    "| `collect_thermal()` | `\"thermal\"` | object |\n"
    "| `collect_display()` | `\"display\"` | array of display objects |\n"
    "\n"
    "**Protocol handlers** (`main.c:496–647`):\n"
    "- `handle_initialize()` — returns server capabilities and `serverInfo`\n"
    "- `handle_tools_list()` — returns JSON Schema for `system_info` including the `categories` enum\n"
    "- `handle_tools_call(params)` — invokes collectors via `WANT` macro, serialises result as text content\n"
    "\n"
    "**`WANT(name)` macro** (`main.c:609–620`): evaluates to 1 if `name` is in the `categories` array, or if `categories` was omitted entirely.\n"
    "\n"
    "**Main loop** (`main.c:649–702`): reads newline-delimited JSON-RPC from stdin, dispatches on `method` string (`initialize`, `notifications/initialized`, `tools/list`, `tools/call`), writes responses to stdout. Both streams are line-buffered.\n"
    "\n"
    "## Installation\n"
    "\n"
    "Installation is a **multi-step process** — it is not complete until all steps succeed.\n"
    "\n"
    "1. Install the binary:\n"
    "   ```bash\n"
    "   brew install marcelocantos/tap/sysinfo-mcp\n"
    "   ```\n"
    "2. Register as an MCP server:\n"
    "   ```bash\n"
    "   claude mcp add --scope user sysinfo -- sysinfo-mcp\n"
    "   ```\n"
    "3. **Restart the agent session** to pick up the new server.\n"
    "\n"
    "For other MCP clients, add to the client config:\n"
    "```json\n"
    "{\n"
    "  \"mcpServers\": {\n"
    "    \"sysinfo\": {\n"
    "      \"command\": \"sysinfo-mcp\"\n"
    "    }\n"
    "  }\n"
    "}\n"
    "```\n"
    "\n"
    "## Build from source\n"
    "\n"
    "```bash\n"
    "make\n"
    "```\n"
    "\n"
    "Requires macOS with Xcode Command Line Tools. Links: `IOKit`, `CoreFoundation`, `SystemConfiguration`.\n"
    "\n"
    "## Adding a new category\n"
    "\n"
    "1. Write a static collector: `static cJSON *collect_foo(void) { ... }` returning a `cJSON*`.\n"
    "2. In `handle_tools_call` (around `main.c:623`), add:\n"
    "   ```c\n"
    "   if (WANT(\"foo\")) cJSON_AddItemToObject(data, \"foo\", collect_foo());\n"
    "   ```\n"
    "3. In `handle_tools_list` (around `main.c:569`), add to the enum array:\n"
    "   ```c\n"
    "   cJSON_AddItemToArray(cat_enum, cJSON_CreateString(\"foo\"));\n"
    "   ```\n"
    "4. Update the tool description string in `handle_tools_list` to mention the new category.\n"
    "\n"
    "## Key conventions\n"
    "\n"
    "- All JSON construction uses cJSON. Never use a different JSON library.\n"
    "- Collectors must not abort on missing data — use `if (sysctlbyname(...) == 0)` guards and emit only fields that are available.\n"
    "- IOKit objects must be released with `IOObjectRelease`; CF objects with `CFRelease`. No leaks on error paths.\n"
    "- Error responses use `send_error(id, code, message)`; success responses use `send_response(id, result)`.\n"
    "- The `WANT` macro is defined locally inside `handle_tools_call` and `#undef`-ed after use.\n";

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("sysinfo-mcp %s\n", VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            fputs(HELP, stdout);
            return 0;
        } else if (strcmp(argv[i], "--help-agent") == 0) {
            fputs(HELP, stdout);
            fputs("\n", stdout);
            fputs(AGENT_GUIDE, stdout);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fputs(HELP, stderr);
            return 1;
        }
    }

    // Line-buffered stdin/stdout for JSON-RPC over stdio
    setvbuf(stdin, NULL, _IOLBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&line, &cap, stdin)) > 0) {
        // Strip trailing newline
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len == 0) continue;

        cJSON *req = cJSON_Parse(line);
        if (!req) {
            send_error(NULL, -32700, "Parse error");
            continue;
        }

        cJSON *id = cJSON_GetObjectItem(req, "id");
        cJSON *method = cJSON_GetObjectItem(req, "method");

        if (!method || !cJSON_IsString(method)) {
            send_error(id, -32600, "Invalid request: missing method");
            cJSON_Delete(req);
            continue;
        }

        const char *m = method->valuestring;

        if (strcmp(m, "initialize") == 0) {
            send_response(id, handle_initialize());
        } else if (strcmp(m, "notifications/initialized") == 0) {
            // Notification — no response needed
        } else if (strcmp(m, "tools/list") == 0) {
            send_response(id, handle_tools_list());
        } else if (strcmp(m, "tools/call") == 0) {
            cJSON *params = cJSON_GetObjectItem(req, "params");
            if (!params || !cJSON_IsObject(params)) {
                send_error(id, -32602, "Invalid params");
            } else {
                send_response(id, handle_tools_call(params));
            }
        } else {
            send_error(id, -32601, "Method not found");
        }

        cJSON_Delete(req);
    }

    free(line);
    return 0;
}
