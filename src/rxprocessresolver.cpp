#include "rxprocessresolver.h"
#include "legacy_core.h"

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <set>

std::vector<SProcessInfo> CRxProcessResolver::FindProcessesByName(const std::string& proc_name)
{
    std::vector<SProcessInfo> result;

    if (proc_name.empty()) {
        return result;
    }

    DIR* dir = opendir("/proc");
    if (!dir) {
        LOG_WARNING("Failed to open /proc directory");
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {

        if (entry->d_type != DT_DIR) {
            continue;
        }

        char* endptr;
        pid_t pid = strtol(entry->d_name, &endptr, 10);
        if (*endptr != '\0' || pid <= 0) {
            continue;
        }

        std::string cmdline = GetCmdline(pid);
        std::string comm = GetComm(pid);

        if (MatchProcessName(cmdline, comm, proc_name)) {
            SProcessInfo info;
            info.pid = pid;
            info.cmdline.swap(cmdline);
            info.comm.swap(comm);
            info.listening_ports = GetListeningPorts(pid);
            info.netns_path = GetNetNsPath(pid);
            result.push_back(info);
        }
    }

    closedir(dir);

    LOG_NOTICE("Found %zu processes matching '%s'", result.size(), proc_name.c_str());
    return result;
}

bool CRxProcessResolver::GetProcessInfo(pid_t pid, SProcessInfo& info)
{
    info.pid = pid;
    info.cmdline = GetCmdline(pid);
    info.comm = GetComm(pid);
    info.listening_ports = GetListeningPorts(pid);
    info.netns_path = GetNetNsPath(pid);

    return !info.comm.empty();
}

std::vector<int> CRxProcessResolver::GetListeningPorts(pid_t pid)
{
    if (pid <= 0) {
        return std::vector<int>();
    }

    std::set<int> ports;

    std::vector<unsigned long> socket_inodes = GetSocketInodes(pid);
    std::set<unsigned long> inode_set(socket_inodes.begin(), socket_inodes.end());

    std::vector<int> tcp_ports = ParseTcpFile("/proc/net/tcp", inode_set);
    ports.insert(tcp_ports.begin(), tcp_ports.end());

    std::vector<int> tcp6_ports = ParseTcpFile("/proc/net/tcp6", inode_set);
    ports.insert(tcp6_ports.begin(), tcp6_ports.end());

    if (!ports.empty()) {
        std::ostringstream oss;
        size_t idx = 0;
        for (std::set<int>::const_iterator it = ports.begin(); it != ports.end(); ++it, ++idx) {
            if (idx > 0) {
                oss << ',';
            }
            oss << *it;
        }
        LOG_NOTICE("Detected listening ports for pid %d: %s (socket_inodes=%zu)",
                   pid, oss.str().c_str(), socket_inodes.size());
    } else {
        LOG_NOTICE("No listening ports detected for pid %d (socket_inodes=%zu)",
                   pid, socket_inodes.size());
    }

    return std::vector<int>(ports.begin(), ports.end());
}

std::vector<int> CRxProcessResolver::ParseTcpFile(const std::string& tcp_file,
                                                  const std::set<unsigned long>& inodes)
{
    std::vector<int> ports;
    std::ifstream file(tcp_file.c_str());

    if (!file.is_open()) {
        return ports;
    }

    std::string line;

    std::getline(file, line);

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::vector<std::string> fields;
        std::string field;
        while (iss >> field) {
            fields.push_back(field);
        }
        if (fields.size() < 10) {
            continue;
        }

        const std::string& local_address = fields[1];
        const std::string& st = fields[3];
        const std::string& inode_str = fields[9];

        size_t colon = local_address.find(':');
        if (colon == std::string::npos || colon + 1 >= local_address.size()) {
            continue;
        }

        if (st != "0A") {
            continue;
        }

        std::string port_hex = local_address.substr(colon + 1);
        int port = strtol(port_hex.c_str(), NULL, 16);
        if (port <= 0) {
            continue;
        }

        unsigned long inode = strtoul(inode_str.c_str(), NULL, 10);
        if (inode > 0 && inodes.find(inode) != inodes.end()) {
            ports.push_back(port);
        }
    }

    file.close();
    return ports;
}

std::vector<unsigned long> CRxProcessResolver::GetSocketInodes(pid_t pid)
{
    std::vector<unsigned long> inodes;

    char fd_path[256];
    snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);

    DIR* dir = opendir(fd_path);
    if (!dir) {
        return inodes;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char link_path[512];
        snprintf(link_path, sizeof(link_path), "%s/%s", fd_path, entry->d_name);

        char target[512];
        ssize_t len = readlink(link_path, target, sizeof(target) - 1);
        if (len <= 0) {
            continue;
        }

        target[len] = '\0';

        if (strncmp(target, "socket:[", 8) == 0) {
            unsigned long inode = strtoul(target + 8, NULL, 10);
            if (inode > 0) {
                inodes.push_back(inode);
            }
        }
    }

    closedir(dir);
    return inodes;
}

std::string CRxProcessResolver::GetNetNsPath(pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/ns/net", pid);

    char link[512];
    ssize_t len = readlink(path, link, sizeof(link) - 1);
    if (len > 0) {
        link[len] = '\0';
        return std::string(link);
    }

    return std::string();
}

std::string CRxProcessResolver::GetCmdline(pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

    std::string content = ReadFile(path);

    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\0') {
            content[i] = ' ';
        }
    }

    while (!content.empty() && content[content.size() - 1] == ' ') {
        content.erase(content.size() - 1);
    }

    return content;
}

std::string CRxProcessResolver::GetComm(pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);

    std::string content = ReadFile(path);

    if (!content.empty() && content[content.size() - 1] == '\n') {
        content.erase(content.size() - 1);
    }

    return content;
}

bool CRxProcessResolver::IsProcessAlive(pid_t pid)
{
    if (pid <= 0) {
        return false;
    }

    char path[256];
    snprintf(path, sizeof(path), "/proc/%d", pid);

    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

std::string CRxProcessResolver::ReadFile(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return std::string();
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

bool CRxProcessResolver::MatchProcessName(const std::string& cmdline,
                                          const std::string& comm,
                                          const std::string& pattern)
{
    if (pattern.empty()) {
        return false;
    }

    if (comm == pattern) {
        return true;
    }

    if (comm.find(pattern) != std::string::npos) {
        return true;
    }

    if (cmdline.find(pattern) != std::string::npos) {
        return true;
    }

    size_t last_slash = cmdline.rfind('/');
    if (last_slash != std::string::npos) {
        std::string exe_name = cmdline.substr(last_slash + 1);
        size_t space = exe_name.find(' ');
        if (space != std::string::npos) {
            exe_name = exe_name.substr(0, space);
        }

        if (exe_name == pattern || exe_name.find(pattern) != std::string::npos) {
            return true;
        }
    }

    return false;
}
