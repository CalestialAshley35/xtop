#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <thread>
#include <mutex>
#include <ncurses.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <algorithm>
#include <cstdio>
#include <sys/utsname.h>
#include <unordered_map>
#include <unordered_set>

std::mutex mtx;

struct DiskUsage {
    std::string path;
    long total;
    long free;
    long used;
};

struct NetworkStats {
    long long bytes_sent;
    long long bytes_recv;
};

struct Process {
    std::string name;
    int pid;
    float cpu_usage;
    float memory_usage;
};

struct SystemCpuTime {
    long total;
    long active;
};

int get_num_cpus() {
    static int num_cpus = 0;
    if (num_cpus == 0) {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("processor") == 0) num_cpus++;
        }
    }
    return num_cpus;
}

SystemCpuTime get_system_cpu_time() {
    std::ifstream cpu_file("/proc/stat");
    std::string line;
    std::getline(cpu_file, line);
    std::istringstream iss(line);
    std::string cpu;
    long user, nice, system, idle, iowait, irq, softirq, steal;
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    long total = user + nice + system + idle + iowait + irq + softirq + steal;
    long active = user + nice + system + irq + softirq + steal;
    return {total, active};
}

std::string get_cpu_usage() {
    static long prev_total = 0;
    static long prev_active = 0;
    SystemCpuTime current = get_system_cpu_time();
    long diff_total = current.total - prev_total;
    long diff_active = current.active - prev_active;
    prev_total = current.total;
    prev_active = current.active;
    return diff_total == 0 ? "0%" : std::to_string((diff_active * 100) / diff_total) + "%";
}

std::string get_memory_usage() {
    struct sysinfo info;
    sysinfo(&info);
    long used = info.totalram - info.freeram;
    return std::to_string(static_cast<int>((used * 100.0) / info.totalram)) + "%";
}

std::string get_system_name() {
    struct utsname buf;
    return uname(&buf) == 0 ? std::string(buf.sysname) + " " + buf.release : "Unknown";
}

DiskUsage get_disk_usage() {
    const std::string path = "/data/data/com.termux/files/home";
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) return {"", 0, 0, 0};
    long total = stat.f_blocks * stat.f_frsize;
    long free = stat.f_bfree * stat.f_frsize;
    return {path, total, free, total - free};
}

NetworkStats get_network_usage() {
    std::ifstream net_file("/proc/net/dev");
    NetworkStats stats = {0, 0};
    std::string line;
    while (std::getline(net_file, line)) {
        if (line.find(":") != std::string::npos) {
            std::istringstream iss(line.substr(line.find(':') + 1));
            long long recv, sent;
            iss >> recv >> sent >> sent >> sent >> sent >> sent >> sent >> sent >> sent;
            stats.bytes_recv += recv;
            stats.bytes_sent += sent;
        }
    }
    return stats;
}

float get_cpu_temperature() {
    for (int i = 0; i < 10; ++i) {
        std::string path = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream temp_file(path);
        if (temp_file) {
            int temp;
            temp_file >> temp;
            return temp / 1000.0f;
        }
    }
    return 0.0f;
}

std::vector<Process> get_processes() {
    static long prev_system_total = 0;
    static std::unordered_map<int, std::pair<long, long>> prev_process_times;
    std::vector<Process> processes;
    std::unordered_set<int> current_pids;
    DIR* dir = opendir("/proc");
    struct dirent* entry;
    SystemCpuTime current_system = get_system_cpu_time();
    long system_delta = current_system.total - prev_system_total;
    prev_system_total = current_system.total;
    int num_cpus = get_num_cpus();

    while ((entry = readdir(dir)) != nullptr) {
        if (!isdigit(entry->d_name[0])) continue;
        int pid = atoi(entry->d_name);
        current_pids.insert(pid);
        std::string stat_file = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream stat(stat_file);
        if (!stat.is_open()) continue;
        std::string line;
        std::getline(stat, line);
        size_t open_paren = line.find('(');
        size_t close_paren = line.rfind(')');
        if (open_paren == std::string::npos || close_paren == std::string::npos) continue;
        std::string name = line.substr(open_paren + 1, close_paren - open_paren - 1);
        std::istringstream iss(line.substr(close_paren + 2));
        std::string state, ppid, dummy;
        long utime, stime, rss;
        iss >> state >> ppid >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> utime >> stime;
        for (int i = 0; i < 8; ++i) iss >> dummy;
        iss >> rss;
        long process_time = utime + stime;
        auto& prev_times = prev_process_times[pid];
        float cpu_usage = 0.0f;
        if (prev_times.first != 0 || prev_times.second != 0) {
            long process_delta = process_time - (prev_times.first + prev_times.second);
            if (system_delta > 0) cpu_usage = (process_delta * 100.0f / system_delta) * num_cpus;
        }
        prev_times = {utime, stime};
        float mem_usage = rss * sysconf(_SC_PAGESIZE) / (1024.0f * 1024.0f);
        processes.push_back({name, pid, cpu_usage, mem_usage});
    }
    closedir(dir);

    auto it = prev_process_times.begin();
    while (it != prev_process_times.end()) {
        if (!current_pids.count(it->first)) it = prev_process_times.erase(it);
        else ++it;
    }
    std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
        return a.cpu_usage > b.cpu_usage;
    });
    return processes;
}

void setup_colors() {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_WHITE, COLOR_BLUE);  // System Header
    init_pair(2, COLOR_GREEN, -1);          // CPU Usage
    init_pair(3, COLOR_CYAN, -1);           // Memory Usage
    init_pair(4, COLOR_YELLOW, -1);         // Disk Usage
    init_pair(5, COLOR_RED, -1);            // Temperature
    init_pair(6, COLOR_MAGENTA, -1);        // Network
    init_pair(7, COLOR_GREEN, COLOR_BLACK); // Process Header
    init_pair(8, COLOR_YELLOW, COLOR_BLACK); // Process CPU Usage
    init_pair(9, COLOR_WHITE, COLOR_BLACK); // Process Memory Usage
}

void draw_ui(WINDOW* win) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string cpu_usage = get_cpu_usage();
    std::string memory_usage = get_memory_usage();
    DiskUsage disk = get_disk_usage();
    NetworkStats net = get_network_usage();
    float temp = get_cpu_temperature();
    std::vector<Process> processes = get_processes();

    std::stringstream ss;
    ss << "System: " << get_system_name() << "\n";
    ss << "CPU: " << cpu_usage << "  Memory: " << memory_usage << "\n";
    ss << "Disk: " << disk.used/(1024*1024) << "MB/" << disk.total/(1024*1024) << "MB\n";
    ss << "Net: ↑" << net.bytes_sent/1024 << "KB ↓" << net.bytes_recv/1024 << "KB\n";
    ss << "Temp: " << std::fixed << std::setprecision(1) << temp << "°C\n\n";
    ss << "Top Processes:\n";

    for (size_t i = 0; i < processes.size() && i < 5; ++i) {
        ss << std::left << std::setw(20) << processes[i].name.substr(0, 19)
           << " PID: " << std::setw(6) << processes[i].pid
           << " CPU: " << std::setw(5) << std::setprecision(1) << std::fixed << processes[i].cpu_usage << "%"
           << " Mem: " << std::setw(5) << processes[i].memory_usage << "MB\n";
    }

    wattron(win, COLOR_PAIR(1));
    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 1, "%s", ss.str().c_str());
    wattroff(win, COLOR_PAIR(1));
    wrefresh(win);
}

void monitor_system(WINDOW* win) {
    while (true) {
        draw_ui(win);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    initscr();
    noecho();
    curs_set(0);
    setup_colors();
    WINDOW* win = newwin(LINES, COLS, 0, 0);
    nodelay(win, TRUE);
    std::thread(monitor_system, win).join();
    endwin();
    return 0;
}