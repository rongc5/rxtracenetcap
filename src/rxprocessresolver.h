#ifndef RX_PROCESS_RESOLVER_H
#define RX_PROCESS_RESOLVER_H

#include <string>
#include <vector>
#include <set>
#include <sys/types.h>

struct SProcessInfo {
    pid_t pid;
    std::string cmdline;
    std::string comm;
    std::vector<int> listening_ports;
    std::string netns_path;

    SProcessInfo() : pid(-1) {}
};

class CRxProcessResolver {
public:

    static std::vector<SProcessInfo> FindProcessesByName(const std::string& proc_name);

    static bool GetProcessInfo(pid_t pid, SProcessInfo& info);

    static std::vector<int> GetListeningPorts(pid_t pid);

    static std::string GetNetNsPath(pid_t pid);

    static std::string GetCmdline(pid_t pid);

    static std::string GetComm(pid_t pid);

    static bool IsProcessAlive(pid_t pid);

private:

    static std::vector<int> ParseTcpFile(const std::string& tcp_file,
                                         const std::set<unsigned long>& inodes);

    static std::vector<unsigned long> GetSocketInodes(pid_t pid);

    static std::string ReadFile(const std::string& path);

    static bool MatchProcessName(const std::string& cmdline,
                                  const std::string& comm,
                                  const std::string& pattern);
};

#endif
