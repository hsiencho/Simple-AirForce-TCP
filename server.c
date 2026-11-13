#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

/*
 * Security Auditing Log Function
 * Appends a timestamped record (with Client IP and event details) to audit.log
 * whenever a client connects, queries system info, attacks, times out, or sends a malicious packet.
 */
void write_audit_log(const char *client_ip, const char *event) {
    FILE *fp = fopen("audit.log", "a");
    if (fp == NULL) {
        return;
    }

    /* Format current system time */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    /* Write timestamp, Client IP, and event message */
    fprintf(fp, "[%s] [Client IP: %s] %s\n", time_str, client_ip, event);
    fclose(fp);
}

/*
 * Robustness Example 1: SIGCHLD Signal Handler
 * Reaps terminated child processes non-blockingly using waitpid() with WNOHANG,
 * preventing <defunct> zombie processes from accumulating in the process table.
 */
void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        /* Child process reaped */
    }
}

/*
 * Child process function to handle an individual player's Boss Battle session
 */
void handle_client(int client_fd, const char *client_ip) {
    PacketHeader hdr;
    char payload[MAX_BODY_SIZE + 1];
    char resp_buf[MAX_BODY_SIZE];
    char sys_str[256] = "Not scanned yet (Press 2 to scan radar)";
    char log_buf[256];

    /* Game state variables maintained on the Server (Server-Side Authority) */
    int boss_hp = 1000;       /* Initial Boss HP = 1000 */
    int double_charge = 0;    /* Double laser charge state: 0=OFF, 1=ON */

    /* Seed random number generator using child PID so each session has unique crits */
    srand(time(NULL) ^ getpid());

    /*
     * Robustness Example 3: Socket Receive Idle Timeout (15 seconds)
     * In normal mode, if a client stays idle for more than 15 seconds without sending commands,
     * recv() automatically returns an error so the child process can clean up and exit.
     */
    if (!g_unsafe_mode) {
        struct timeval tv;
        tv.tv_sec = 15;   /* 15-second idle timeout */
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    /* Main session loop for this client */
    while (1) {
        int ret = recv_pkt(client_fd, &hdr, payload);

        if (ret == -2) {
            /* Malicious or corrupted packet blocked by recv_pkt() */
            write_audit_log(client_ip, "[SECURITY ALERT] Malicious/corrupted packet blocked; connection terminated");
            break;
        } else if (ret < 0) {
            /* Check if disconnect was caused by the 15-second idle timeout */
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("[Timeout Protection] Client (%s, PID=%d) idle for >15s. Closing child process!\n",
                       client_ip, getpid());
                write_audit_log(client_ip, "[IDLE TIMEOUT] Client idle for >15s; server closed connection");
            } else {
                snprintf(log_buf, sizeof(log_buf), "Client (%s, PID=%d) disconnected", client_ip, getpid());
                log_msg(1, log_buf);
                write_audit_log(client_ip, "[DISCONNECT] Client left the game");
            }
            break;
        }

        /* Process command according to Opcode */
        if (hdr.opcode == OP_SYSINFO) {
            /*
             * Action 2: Query Linux system telemetry via BOTH:
             * 1. popen("free -m", "r") IPC Pipe (executes Linux 'free -m' command and reads stdout)
             * 2. sysinfo() and uname() Linux Kernel APIs (reads OS version, uptime, and load average)
             */
            struct sysinfo info;
            struct utsname uts;

            log_msg(1, "Received OP_SYSINFO: reading RAM via popen(\"free -m\") IPC and kernel APIs...");

            if (sysinfo(&info) == 0 && uname(&uts) == 0) {
                /* Fallback Free RAM from sysinfo() in MB */
                unsigned long free_ram_mb = (unsigned long)(info.freeram * info.mem_unit) / (1024 * 1024);
                /* Calculate 1-minute load average */
                double load_avg = info.loads[0] / 65536.0;

                /* Use popen() IPC to execute 'free -m' and read memory status through a pipe */
                FILE *pipe_fp = popen("free -m", "r");
                if (pipe_fp != NULL) {
                    char line[256];
                    unsigned long total_m = 0, used_m = 0, free_m = 0;
                    while (fgets(line, sizeof(line), pipe_fp) != NULL) {
                        /* Parse the 'Mem:' row from 'free -m' output */
                        if (sscanf(line, "Mem: %lu %lu %lu", &total_m, &used_m, &free_m) == 3) {
                            free_ram_mb = total_m - used_m; /* Available RAM in MB */
                        }
                    }
                    pclose(pipe_fp);
                }

                snprintf(sys_str, sizeof(sys_str),
                         "OS: %s %s | Uptime: %lds | Free RAM: %lu MB | Load: %.2f",
                         uts.sysname, uts.release, info.uptime, free_ram_mb, load_avg);

                /* Game mechanic linkage: if Server Free RAM > 500 MB, grant Double Laser Charge */
                if (free_ram_mb > 500) {
                    double_charge = 1;
                    snprintf(resp_buf, sizeof(resp_buf),
                             "HP:%d|CHARGE:%d|SYS:%s|MSG:Radar scan complete! Free RAM (%lu MB > 500 MB) sufficient -> [DOUBLE LASER CHARGED]!",
                             boss_hp, double_charge, sys_str, free_ram_mb);
                } else {
                    snprintf(resp_buf, sizeof(resp_buf),
                             "HP:%d|CHARGE:%d|SYS:%s|MSG:Radar scan complete! Current Free RAM: %lu MB.",
                             boss_hp, double_charge, sys_str, free_ram_mb);
                }

                write_audit_log(client_ip, "[SYSINFO] Client queried server system telemetry");
            } else {
                snprintf(resp_buf, sizeof(resp_buf),
                         "HP:%d|CHARGE:%d|SYS:%s|MSG:Failed to read system information!",
                         boss_hp, double_charge, sys_str);
            }

            /* Send encrypted response packet back to client */
            send_pkt(client_fd, OP_RESPONSE, resp_buf, strlen(resp_buf));

        } else if (hdr.opcode == OP_ATTACK) {
            /*
             * Action 1: Fire Laser at Boss
             * Security Design (Server-Side Authority): Damage is computed strictly on the server
             * using rand() so modified clients cannot cheat or one-shot the Boss.
             */
            log_msg(1, "Received OP_ATTACK: computing attack damage on server side...");

            /* Base damage 100 + random critical bonus (0 to 50) */
            int base_dmg = 100;
            int bonus_dmg = rand() % 51;
            int total_dmg = base_dmg + bonus_dmg;
            int used_charge = 0;

            /* Double total damage if Double Charge is active, then consume charge */
            if (double_charge) {
                total_dmg *= 2;
                double_charge = 0;
                used_charge = 1;
            }

            /* Subtract Boss HP (clamped at 0) */
            boss_hp -= total_dmg;
            if (boss_hp < 0) {
                boss_hp = 0;
            }

            /* Record attack event in audit.log */
            snprintf(log_buf, sizeof(log_buf), "[ATTACK] Player attacked Boss for %d dmg (Boss HP left: %d)",
                     total_dmg, boss_hp);
            write_audit_log(client_ip, log_buf);

            /* Build battle status message */
            if (boss_hp == 0) {
                snprintf(resp_buf, sizeof(resp_buf),
                         "HP:%d|CHARGE:%d|SYS:%s|MSG:BOOM! Dealt %d dmg and DESTROYED the Enemy Carrier Boss! Victory! (Boss HP Reset)",
                         boss_hp, double_charge, sys_str, total_dmg);
                /* Reset Boss HP to 1000 so the player can continue playing/testing */
                boss_hp = 1000;
            } else if (used_charge) {
                snprintf(resp_buf, sizeof(resp_buf),
                         "HP:%d|CHARGE:%d|SYS:%s|MSG:Fired OVERCHARGED 2X LASER! Dealt %d massive damage to Boss!",
                         boss_hp, double_charge, sys_str, total_dmg);
            } else {
                snprintf(resp_buf, sizeof(resp_buf),
                         "HP:%d|CHARGE:%d|SYS:%s|MSG:Cyan laser hit the Boss for %d damage!",
                         boss_hp, double_charge, sys_str, total_dmg);
            }

            send_pkt(client_fd, OP_RESPONSE, resp_buf, strlen(resp_buf));

        } else {
            /* Unknown Opcode handling */
            printf("[Warning] Received unknown Opcode=%d, Payload=%s\n", hdr.opcode, payload);
            snprintf(resp_buf, sizeof(resp_buf),
                     "HP:%d|CHARGE:%d|SYS:%s|MSG:Server received unknown command (Opcode=%d)",
                     boss_hp, double_charge, sys_str, hdr.opcode);
            send_pkt(client_fd, OP_RESPONSE, resp_buf, strlen(resp_buf));
        }
    }

    close(client_fd);
}

int main(int argc, char *argv[]) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int opt = 1;
    int i;

    /* Parse command-line arguments: -d (Runtime Debug) and --unsafe (Disable Robustness Protections) */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            g_debug_runtime = 1;
        } else if (strcmp(argv[i], "--unsafe") == 0) {
            g_unsafe_mode = 1;
        }
    }

    printf("============================================================\n");
    printf("   Simple-AirForce: Terminal Boss Battle & Telemetry Server\n");
    printf("============================================================\n");
    printf("[Startup] Port: %d | Runtime Debug(-d): %s | Protection Mode: %s\n",
           SERVER_PORT,
           g_debug_runtime ? "ON" : "OFF",
           g_unsafe_mode ? "DISABLED (--unsafe mode)" : "ENABLED (Normal Safe Mode)");

    /*
     * Robustness Example 1: Signal Handling Setup
     * In normal mode:
     * 1. Register SIGCHLD handler to automatically reap terminated child processes (zero zombies)
     * 2. Ignore SIGPIPE so unexpected client disconnects do not crash the server on send()
     */
    if (!g_unsafe_mode) {
        signal(SIGCHLD, sigchld_handler);
        signal(SIGPIPE, SIG_IGN);
        log_msg(1, "Registered SIGCHLD zombie reaper and SIGPIPE protection");
    } else {
        printf("[WARNING] Running in --unsafe mode: No SIGCHLD reaper, No Timeout, No Packet Validation!\n");
    }

    /* 1. Create TCP listening socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket() failed");
        exit(1);
    }

    /* Set SO_REUSEADDR to allow immediate port reuse on server restart */
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 2. Bind socket to IP and Port */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind() failed");
        close(server_fd);
        exit(1);
    }

    /* 3. Listen for incoming connections (backlog = 20 to support 10+ concurrent clients) */
    if (listen(server_fd, 20) < 0) {
        perror("listen() failed");
        close(server_fd);
        exit(1);
    }

    log_msg(0, "Server initialized and waiting for fighter jet client connections...");

    /* 4. Main accept() loop: fork() a new child process for each connected client */
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            /* Retry accept() if interrupted by SIGCHLD signal (EINTR) */
            if (errno == EINTR) {
                continue;
            }
            perror("accept() failed");
            continue;
        }

        /* Convert client IP address to string */
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        /* Log connection event to audit.log */
        write_audit_log(client_ip, "[CONNECT] New player connected");

        /* Use fork() to spawn a child process for this client (no threads used) */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork() failed");
            close(client_fd);
        } else if (pid == 0) {
            /* Child process: close listening socket and serve the client */
            close(server_fd);
            printf("[Connected] Spawned child process (PID=%d) for Client IP: %s\n", getpid(), client_ip);
            handle_client(client_fd, client_ip);
            exit(0);
        } else {
            /* Parent process: close client socket and continue accepting new clients */
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}
