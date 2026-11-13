# Simple-AirForce-TCP

**Multi-Process TCP Boss Battle & Linux System Telemetry Server**  
*Network and System Programming — Midterm Project (POSIX C / Linux)*

---

## Overview

**Simple-AirForce-TCP** is a multi-process TCP Client–Server application written in C on Linux. It combines a real-time **4 FPS split-screen terminal interface** (`select()` + `termios`) with **cross-process Linux system telemetry** (`popen("free -m", "r")` IPC pipe + `sysinfo()` / `uname()` kernel syscalls) and **comparative robustness testing**.

### Key Technical Features
1. **Multi-Process Architecture (`fork()`, No Threads):** The server spawns an isolated child process per client connection via `fork()`.
2. **10+ Concurrent Clients Proof:** Includes `test_10_clients.sh` to launch 12 simultaneous background clients (`./client --auto`), verifiable via `pstree -p` and `ps -ef`.
3. **Dynamic Shared Library (`libcommon.so`):** Encapsulates the 8-byte framed binary protocol (`Magic=0x90`), 16-bit checksum, symmetric XOR encryption (`key=0x5A`), and two-level debug logger (`-DDEBUG_COMPILE` & `-d`). Both `server` and `client` dynamically link against `./libcommon.so`.
4. **System Information Query (`OP_SYSINFO`):** Queries OS release, uptime, load average, and available RAM via both `popen("free -m", "r")` IPC pipe and `sysinfo()` / `uname()`. When server Free RAM > 500 MB, the client fighter jet receives a **2X Overcharged Laser Boost**.
5. **Three Robustness Mechanisms (`./server` vs. `./server --unsafe`):**
   - **Case 1 (Zombie Process Prevention):** Asynchronous `SIGCHLD` handler with `waitpid(-1, NULL, WNOHANG)` prevents `<defunct>` zombie processes.
   - **Case 2 (Malicious Packet Defense):** Validates Magic Byte (`0x90`), payload length (`<= 1024`), and checksum before processing; blocks forged oversized packets (`Length=999999`).
   - **Case 3 (Idle Timeout & Security Audit Log):** Enforces a 15-second socket receive timeout (`SO_RCVTIMEO`) and records timestamped events in `audit.log`.

---

## Project Structure

```text
Simple-AirForce-TCP/
├── Makefile             # Builds libcommon.so, server, and client (supports 'make', 'make debug', 'make clean')
├── common.h             # 8-byte packed PacketHeader definition, opcodes, and shared library declarations
├── common.c             # Shared library implementation (framing, checksum, XOR 0x5A cipher, two-level debug log)
├── server.c             # Multi-process TCP server (fork, SIGCHLD reaper, SO_RCVTIMEO, popen/sysinfo, audit.log)
├── client.c             # Split-screen 4 FPS terminal client (select + termios) and --auto concurrency mode
├── test_10_clients.sh   # Automated script spawning 12 concurrent clients for 20 seconds
└── README.md            # Project documentation and execution guide
```

---

## How to Build & Run

### 1. Build the Project
```bash
# Standard build
make clean && make

# Or build with Compile-Time Debug enabled (-DDEBUG_COMPILE)
make debug

# Verify dynamic shared library linking (libcommon.so)
ldd ./server
ldd ./client
```

### 2. Run Interactive Split-Screen Mode
Open two terminals:
* **Terminal 1 (Server):**
  ```bash
  ./server
  # Or enable Runtime Debug (-d):
  ./server -d
  ```
* **Terminal 2 (Client):**
  ```bash
  ./client
  ```
  * Press **`[2]`** to scan server system telemetry (`OP_SYSINFO`) into the Right Panel.
  * Press **`[1]`** to fire the laser at the Boss (`OP_ATTACK`).
  * Press **`[3]`** to inject a forged malicious packet (`Length=999999, Checksum=0xDEAD`).
  * Press **`[0]`** to quit.

### 3. Run 12-Client Concurrency Test
With `./server` running in Terminal 1, run in Terminal 2:
```bash
./test_10_clients.sh
pstree -p $(pgrep -o server)
ps -ef | grep "./server"
```

### 4. Capture Packets with `tcpdump` / Wireshark
```bash
(sleep 2; ./client --auto >/dev/null 2>&1) &
sudo tcpdump -i lo port 8888 -c 10 -U -w - > dump.pcap
```

### 5. Run the 3 Robustness Comparative Tests (`--unsafe` vs. Normal)
* **Test 1 — Zombie Process (`<defunct>`):**
  ```bash
  # Unsafe mode (leaves <defunct> zombie in 'ps aux | grep server'):
  ./server --unsafe
  # Normal mode (0 zombies):
  ./server
  ```
* **Test 2 — Malicious Packet (`Length=999999`):**
  Run `./client` and press **`[3]`** against `./server` (blocked with `[SECURITY ALERT]`) vs. `./server --unsafe` (crashes with `Aborted`).
* **Test 3 — 15s Idle Timeout (`SO_RCVTIMEO`) & `audit.log`:**
  Connect with `./client` and remain idle for 15 seconds to trigger `[Timeout Protection]`, then check `cat audit.log`.
