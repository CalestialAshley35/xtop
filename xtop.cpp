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
#include <cmath>
#include <ctime>
#include <signal.h>

std::mutex mtx;
volatile bool resize_flag = false;

struct SystemInfo {
    std::string os_name;
    std::string hostname;
    std::string architecture;
    long uptime;
};

struct CpuInfo {
    float usage;
    std::vector<float> core_usage;
    float temperature;
};

struct MemoryInfo {
    float total;
    float used;
    float free;
};

struct DiskInfo {
    std::string mount_point;
    float total;
    float used;
    float free;
};

struct NetworkInfo {
    float tx_rate;
    float rx_rate;
    long long tx_total;
    long long rx_total;
};

struct BatteryInfo {
    bool present;
    int capacity;
    std::string status;
};

struct ProcessInfo {
    std::string name;
    int pid;
    std::string user;
    float cpu;
    float mem;
    std::string state;
};

// Color pairs
enum Colors {
    HEADER = 1,
    CPU_BAR,
    MEM_BAR,
    DISK_BAR,
    NETWORK_BAR,
    TEMP_BAR,
    BATTERY_GOOD,
    BATTERY_WARN,
    BATTERY_CRIT,
    PROCESS_HEADER,
    PROCESS_HIGHLIGHT
};

void handle_resize(int sig) {
    resize_flag = true;
}

void init_ncurses() {
    initscr();
    start_color();
    use_default_colors();
    curs_set(0);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    timeout(100);
    
    // Initialize color pairs
    init_pair(HEADER, COLOR_WHITE, COLOR_BLUE);
    init_pair(CPU_BAR, COLOR_BLACK, COLOR_CYAN);
    init_pair(MEM_BAR, COLOR_BLACK, COLOR_MAGENTA);
    init_pair(DISK_BAR, COLOR_BLACK, COLOR_YELLOW);
    init_pair(NETWORK_BAR, COLOR_BLACK, COLOR_GREEN);
    init_pair(TEMP_BAR, COLOR_BLACK, COLOR_RED);
    init_pair(BATTERY_GOOD, COLOR_GREEN, -1);
    init_pair(BATTERY_WARN, COLOR_YELLOW, -1);
    init_pair(BATTERY_CRIT, COLOR_RED, -1);
    init_pair(PROCESS_HEADER, COLOR_WHITE, COLOR_BLUE);
    init_pair(PROCESS_HIGHLIGHT, COLOR_YELLOW, -1);
}

SystemInfo get_system_info() {
    SystemInfo info;
    struct utsname buf;
    if (uname(&buf) == 0) {
        info.os_name = buf.sysname;
        info.hostname = buf.nodename;
        info.architecture = buf.machine;
    }

    std::ifstream uptime_file("/proc/uptime");
    if (uptime_file) {
        double uptime_seconds;
        uptime_file >> uptime_seconds;
        info.uptime = static_cast<long>(uptime_seconds);
    }
    return info;
}

CpuInfo get_cpu_info() {
    static std::vector<std::pair<long, long>> prev_cpu_times;
    CpuInfo info;
    std::ifstream stat_file("/proc/stat");
    std::string line;
    int core_count = 0;

    while (std::getline(stat_file, line)) {
        if (line.substr(0, 3) == "cpu" && isdigit(line[3])) {
            std::istringstream iss(line.substr(5));
            long user, nice, system, idle, iowait, irq, softirq, steal;
            iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
            long total = user + nice + system + idle + iowait + irq + softirq + steal;
            long active = total - idle;

            if (prev_cpu_times.size() > core_count) {
                long diff_total = total - prev_cpu_times[core_count].first;
                long diff_active = active - prev_cpu_times[core_count].second;
                float usage = diff_total > 0 ? (100.0f * diff_active / diff_total) : 0.0f;
                
                if (core_count == 0) {
                    info.usage = usage;
                } else {
                    info.core_usage.push_back(usage);
                }
            }

            if (core_count >= prev_cpu_times.size()) {
                prev_cpu_times.emplace_back(total, active);
            } else {
                prev_cpu_times[core_count] = {total, active};
            }
            core_count++;
        }
    }

    // Get temperature
    for (int i = 0; i < 10; ++i) {
        std::string path = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream temp_file(path);
        if (temp_file) {
            int temp;
            temp_file >> temp;
            info.temperature = temp / 1000.0f;
            break;
        }
    }

    return info;
}

MemoryInfo get_memory_info() {
    MemoryInfo info;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    long total = 0, free = 0, buffers = 0, cached = 0;

    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        long value;
        iss >> key >> value;
        if (key == "MemTotal:") total = value;
        if (key == "MemFree:") free = value;
        if (key == "Buffers:") buffers = value;
        if (key == "Cached:") cached = value;
    }

    info.total = total / 1024.0f;
    info.free = free / 1024.0f;
    info.used = (total - free - buffers - cached) / 1024.0f;
    return info;
}

std::vector<DiskInfo> get_disk_info() {
    std::vector<DiskInfo> disks;
    std::ifstream mounts("/proc/mounts");
    std::string line;

    while (std::getline(mounts, line)) {
        std::istringstream iss(line);
        std::string device, mount_point, type;
        iss >> device >> mount_point >> type;

        if (mount_point.find("/data") != std::string::npos || 
            mount_point.find("/storage") != std::string::npos) {
            struct statvfs vfs;
            if (statvfs(mount_point.c_str(), &vfs) == 0) {
                DiskInfo disk;
                disk.mount_point = mount_point;
                disk.total = (vfs.f_blocks * vfs.f_frsize) / (1024.0f * 1024.0f);
                disk.free = (vfs.f_bfree * vfs.f_frsize) / (1024.0f * 1024.0f);
                disk.used = disk.total - disk.free;
                disks.push_back(disk);
            }
        }
    }
    return disks;
}

NetworkInfo get_network_info() {
    static long long prev_tx = 0, prev_rx = 0;
    static auto prev_time = std::chrono::steady_clock::now();
    
    NetworkInfo info;
    std::ifstream net_file("/proc/net/dev");
    std::string line;
    long long tx = 0, rx = 0;

    while (std::getline(net_file, line)) {
        if (line.find(":") != std::string::npos) {
            std::istringstream iss(line.substr(line.find(':') + 1));
            long long r, t;
            iss >> r >> t >> t >> t >> t >> t >> t >> t >> t;
            rx += r;
            tx += t;
        }
    }

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - prev_time).count();
    
    info.tx_rate = (tx - prev_tx) / elapsed / 1024.0f;
    info.rx_rate = (rx - prev_rx) / elapsed / 1024.0f;
    info.tx_total = tx;
    info.rx_total = rx;

    prev_tx = tx;
    prev_rx = rx;
    prev_time = now;
    return info;
}

BatteryInfo get_battery_info() {
    BatteryInfo info;
    info.present = false;
    
    DIR* dir = opendir("/sys/class/power_supply");
    if (!dir) return info;

    struct dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        
        std::string path = "/sys/class/power_supply/" + std::string(entry->d_name);
        std::ifstream capacity(path + "/capacity");
        std::ifstream status(path + "/status");
        
        if (capacity && status) {
            info.present = true;
            capacity >> info.capacity;
            status >> info.status;
            break;
        }
    }
    closedir(dir);
    return info;
}

std::vector<ProcessInfo> get_processes() {
    std::vector<ProcessInfo> processes;
    DIR* proc_dir = opendir("/proc");
    struct dirent* entry;

    while ((entry = readdir(proc_dir))) {
        if (!isdigit(entry->d_name[0])) continue;
        int pid = atoi(entry->d_name);
        
        // Get process status
        std::string status_path = "/proc/" + std::to_string(pid) + "/status";
        std::ifstream status_file(status_path);
        if (!status_file) continue;

        ProcessInfo proc;
        proc.pid = pid;
        std::string line;
        
        while (std::getline(status_file, line)) {
            std::istringstream iss(line);
            std::string key;
            iss >> key;
            
            if (key == "Name:") {
                iss >> proc.name;
            } else if (key == "State:") {
                char state;
                iss >> state;
                proc.state = std::string(1, state);
            } else if (key == "Uid:") {
                int uid;
                iss >> uid;
                // Convert UID to username (simplified)
                proc.user = std::to_string(uid);
            }
        }

        // Get process stats
        std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream stat_file(stat_path);
        if (stat_file) {
            std::string line;
            std::getline(stat_file, line);
            std::istringstream iss(line);
            std::string dummy;
            for (int i = 0; i < 13; ++i) iss >> dummy; // Skip to utime
            long utime, stime;
            iss >> utime >> stime;
            proc.cpu = (utime + stime) / sysconf(_SC_CLK_TCK);
        }

        processes.push_back(proc);
    }
    closedir(proc_dir);

    // Sort by CPU usage and limit to top 10
    std::sort(processes.begin(), processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        return a.cpu > b.cpu;
    });
    
    if (processes.size() > 10) processes.resize(10);
    return processes;
}

void draw_bar(WINDOW* win, int y, int x, int width, float percent, int color_pair) {
    wattron(win, COLOR_PAIR(color_pair));
    int bars = static_cast<int>(percent * width / 100.0f);
    for (int i = 0; i < width; i++) {
        waddch(win, i < bars ? ' ' : ACS_CKBOARD);
    }
    wattroff(win, COLOR_PAIR(color_pair));
}

void draw_ui() {
    static int last_height = 0;
    static int last_width = 0;
    int height, width;
    getmaxyx(stdscr, height, width);

    if (height != last_height || width != last_width || resize_flag) {
        erase();
        last_height = height;
        last_width = width;
        resize_flag = false;
    }

    // Get system information
    SystemInfo sys_info = get_system_info();
    CpuInfo cpu_info = get_cpu_info();
    MemoryInfo mem_info = get_memory_info();
    auto disks = get_disk_info();
    NetworkInfo net_info = get_network_info();
    BatteryInfo bat_info = get_battery_info();
    auto processes = get_processes();

    // Draw header
    wattron(stdscr, COLOR_PAIR(HEADER));
    mvprintw(0, 0, " %s@%s | %s | Up %ld days %02ld:%02ld ", 
             sys_info.os_name.c_str(), sys_info.hostname.c_str(),
             sys_info.architecture.c_str(),
             sys_info.uptime / 86400, (sys_info.uptime % 86400) / 3600,
             (sys_info.uptime % 3600) / 60);
    whline(stdscr, ' ', width);
    wattroff(stdscr, COLOR_PAIR(HEADER));

    // System columns layout
    int col1_width = width / 3;
    int col2_width = width / 3;
    int col3_width = width - col1_width - col2_width;

    // Column 1: CPU and Memory
    mvprintw(2, 1, "CPU [%.1f%%]", cpu_info.usage);
    draw_bar(stdscr, 3, 1, col1_width - 2, cpu_info.usage, CPU_BAR);
    
    mvprintw(5, 1, "Memory [%.1f/%.1f GB]", mem_info.used, mem_info.total);
    draw_bar(stdscr, 6, 1, col1_width - 2, (mem_info.used / mem_info.total) * 100, MEM_BAR);

    // Column 2: Disk and Network
    int disk_y = 2;
    for (const auto& disk : disks) {
        float used_percent = (disk.used / disk.total) * 100;
        mvprintw(disk_y, col1_width + 1, "Disk %s", disk.mount_point.c_str());
        mvprintw(disk_y + 1, col1_width + 1, "%.1f/%.1f GB", disk.used, disk.total);
        draw_bar(stdscr, disk_y + 2, col1_width + 1, col2_width - 2, used_percent, DISK_BAR);
        disk_y += 4;
    }

    mvprintw(disk_y, col1_width + 1, "Network ↑%.1f KB/s ↓%.1f KB/s",
             net_info.tx_rate, net_info.rx_rate);
    draw_bar(stdscr, disk_y + 1, col1_width + 1, col2_width - 2, 
            std::min(net_info.tx_rate / 100, 100.0f), NETWORK_BAR);

    // Column 3: Temperature and Battery
    mvprintw(2, col1_width + col2_width + 1, "Temperature: %.1f°C", cpu_info.temperature);
    draw_bar(stdscr, 3, col1_width + col2_width + 1, col3_width - 2, 
            cpu_info.temperature, TEMP_BAR);

    if (bat_info.present) {
        int bat_color = BATTERY_GOOD;
        if (bat_info.capacity < 20) bat_color = BATTERY_CRIT;
        else if (bat_info.capacity < 40) bat_color = BATTERY_WARN;
        
        wattron(stdscr, COLOR_PAIR(bat_color));
        mvprintw(5, col1_width + col2_width + 1, "Battery: %d%% [%s]", 
                bat_info.capacity, bat_info.status.c_str());
        wattroff(stdscr, COLOR_PAIR(bat_color));
        draw_bar(stdscr, 6, col1_width + col2_width + 1, col3_width - 2, 
                bat_info.capacity, bat_color);
    }

    // Process list
    int proc_y = height - 12;
    wattron(stdscr, COLOR_PAIR(PROCESS_HEADER));
    mvhline(proc_y, 0, ' ', width);
    mvprintw(proc_y, 1, " PID   USER     CPU%%  MEM%%  STATE  COMMAND");
    wattroff(stdscr, COLOR_PAIR(PROCESS_HEADER));

    for (size_t i = 0; i < processes.size() && i < 10; ++i) {
        const auto& p = processes[i];
        mvprintw(proc_y + i + 1, 1, "%5d %-8s %5.1f %5.1f   %-2s   %s",
                p.pid, p.user.c_str(), p.cpu, p.mem, 
                p.state.c_str(), p.name.c_str());
    }

    refresh();
}

int main() {
    signal(SIGWINCH, handle_resize);
    init_ncurses();

    while (true) {
        draw_ui();
        napms(1000);
        if (getch() == 'q') break;
    }

    endwin();
    return 0;
}
