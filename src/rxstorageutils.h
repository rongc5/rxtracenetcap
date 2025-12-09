#ifndef RXNET_STORAGE_UTILS_H
#define RXNET_STORAGE_UTILS_H

#include <string>
#include <time.h>
#include <pcap/pcap.h>

// Include PDEF types
#include "pdef/pdef_types.h"

struct CRxDumpCtx {
    pcap_t* p;
    pcap_dumper_t* d;
    long max_bytes;
    long written;
    int seq;
    time_t start_time;
    std::string base_dir;
    std::string pattern;
    std::string category;
    std::string iface;
    std::string proc;
    int port;
    std::string current_path;
    bool compress_enabled;
    std::string compress_cmd;

    // PDEF protocol filter
    std::string protocol_filter_path;  // Path to .pdef file
    ProtocolDef* protocol_def;         // Loaded protocol definition
    unsigned long packets_filtered;    // Count of packets filtered out

    // Filter thread integration
    uint32_t filter_thread_index;      // Index of filter thread (0 = not using filter thread)
};

class CRxStorageUtils {
public:

    static std::string expand_pattern(const CRxDumpCtx* dc);
    static std::string ymd_date(time_t t);
    static std::string join_path(const std::string& a, const std::string& b);

    static void ensure_dir(const std::string& path);

    static void rotate_open(CRxDumpCtx* dc);
    static void dump_cb(u_char* user, const struct pcap_pkthdr* h, const u_char* bytes);

private:
    static std::string two_digits(int v);
};

#endif
