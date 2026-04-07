// Copyright 2026 Marcelo Cantos
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <mach/mach.h>
#include <mach/host_info.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#include "vendor/cjson/cJSON.h"

// ---------------------------------------------------------------------------
// System info collectors
// ---------------------------------------------------------------------------

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

static cJSON *collect_all(void) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "cpu", collect_cpu());
    cJSON_AddItemToObject(result, "memory", collect_memory());
    cJSON_AddItemToObject(result, "gpu", collect_gpu());
    cJSON_AddItemToObject(result, "disk", collect_disk());
    cJSON_AddItemToObject(result, "os", collect_os());
    cJSON_AddItemToObject(result, "thermal", collect_thermal());
    return result;
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
    cJSON_AddStringToObject(info, "version", "0.1.0");
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
        "disk (total, free, used), OS (version, hostname, uptime), and "
        "thermal pressure. Use 'category' to request a specific section or "
        "omit for all.");

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *props = cJSON_CreateObject();
    cJSON *cat = cJSON_CreateObject();
    cJSON_AddStringToObject(cat, "type", "string");
    cJSON_AddStringToObject(cat, "description",
        "Category to report: cpu, memory, gpu, disk, os, thermal, or all");
    cJSON *cat_enum = cJSON_CreateArray();
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("all"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("cpu"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("memory"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("gpu"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("disk"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("os"));
    cJSON_AddItemToArray(cat_enum, cJSON_CreateString("thermal"));
    cJSON_AddItemToObject(cat, "enum", cat_enum);
    cJSON_AddItemToObject(props, "category", cat);

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

    // Parse category
    const char *category = "all";
    cJSON *args = cJSON_GetObjectItem(params, "arguments");
    if (args) {
        cJSON *cat = cJSON_GetObjectItem(args, "category");
        if (cat && cJSON_IsString(cat)) {
            category = cat->valuestring;
        }
    }

    cJSON *data;
    if (strcmp(category, "all") == 0) {
        data = collect_all();
    } else if (strcmp(category, "cpu") == 0) {
        data = collect_cpu();
    } else if (strcmp(category, "memory") == 0) {
        data = collect_memory();
    } else if (strcmp(category, "gpu") == 0) {
        data = collect_gpu();
    } else if (strcmp(category, "disk") == 0) {
        data = collect_disk();
    } else if (strcmp(category, "os") == 0) {
        data = collect_os();
    } else if (strcmp(category, "thermal") == 0) {
        data = collect_thermal();
    } else {
        data = collect_all();
    }

    char *text = cJSON_Print(data);
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

int main(void) {
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
            send_response(id, handle_tools_call(params));
        } else {
            send_error(id, -32601, "Method not found");
        }

        cJSON_Delete(req);
    }

    free(line);
    return 0;
}
