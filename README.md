# xtop - Lightweight System Monitor for Linux  

xtop is a powerful yet lightweight terminal-based system monitor for Linux. It provides real-time insights into CPU, memory, disk, network usage, and top processes with a clean and interactive interface using `ncurses`.  

## Features  

- **Real-time System Monitoring**  
  - CPU usage percentage  
  - Memory usage statistics  
  - Disk usage overview  
  - Network traffic (upload/download)  
  - CPU temperature monitoring (if available)  

- **Process Management**  
  - Displays top processes sorted by CPU usage  
  - Shows process ID (PID), CPU, and memory consumption  

- **Minimal and Efficient**  
  - Low resource consumption  
  - Works in any terminal with `ncurses` support  

## Installation  

You can install xtop using the provided Debian package:  

```bash
wget https://github.com/CalestialAshley35/xtop/raw/b129153c2c00036b8db785570155b9d4183c3c54/xtop-package.deb
sudo apt install ./xtop-package.deb
``` 
Usage

Run the following command in your terminal:
```bash
xtop
```

**Compilation (For Developers)**

If you want to build xtop from source, ensure you have g++ and ncurses installed, then compile it using:

g++ -std=c++11 -o xtop xtop.cpp -lncurses

Example Output
```
system: Linux 5.15.0-60-generic
CPU: 23%  Memory: 56%
Disk: 30GB/100GB
Net: ↑350KB ↓500KB
Temp: 48.5°C

Top Processes:
firefox            PID: 3021  CPU: 15.2%  Mem: 120.3MB
chrome             PID: 4895  CPU: 12.8%  Mem: 98.1MB
code              PID: 6721  CPU: 9.7%   Mem: 150.0MB
```

## Notes
- xtop is designed for Linux-based systems and reads data from /proc and /sys.
- Disk usage is retrieved from /data/data/com.termux/files/home (this can be changed if needed).
- The program updates statistics every second.

## License
This project is licensed under the MIT License. See the LICENSE file for details.
