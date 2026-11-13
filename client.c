#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

/* Save original terminal attributes so they can be restored on exit */
struct termios orig_termios;

/* Restore terminal to normal canonical/echo mode and show cursor when exiting */
void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("\033[?25h\n"); /* Show cursor */
}

/* Put terminal into non-canonical, no-echo mode for instant single-key input and smooth refresh */
void init_terminal(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);

    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO); /* Disable line buffering and key echo */
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    printf("\033[2J\033[?25l"); /* Clear screen once and hide cursor to prevent flicker */
}

/*
 * Draw the split-screen dynamic battle interface
 * Left Panel (Cols 1..46) : Animated Air Combat (scrolling stars, moving Boss, laser beam)
 * Right Panel (Col 48+)   : Server System Telemetry Radar (updated when pressing '2')
 * Uses ANSI cursor positioning (\033[row;colH) so both panels stay perfectly aligned.
 */
void draw_split_screen(int frame, int boss_hp, int double_charge, int has_scanned,
                       const char *sys_info, const char *last_msg, int laser_frames, int is_double_laser) {
    int i;
    int bars = boss_hp / 100;
    /* Make the Boss gently sway left and right across frames (0, 1, 2, 1) */
    int boss_offset = (frame % 4 == 3) ? 1 : (frame % 4);
    const char *pad = (boss_offset == 0) ? "        " : (boss_offset == 1 ? "          " : "            ");

    if (bars < 0) bars = 0;
    if (bars > 10) bars = 10;

    /* Move cursor to top-left (1,1) to overwrite frame without flickering */
    printf("\033[H");

    /* ==================== Top Header Bar ==================== */
    printf("====================================================================================================\n");
    printf("  \033[1;36m[ LEFT: LIVE AIR COMBAT ARENA ]\033[0m             |  \033[1;33m[ RIGHT: SERVER SYSTEM TELEMETRY (PRESS 2) ]\033[0m\n");
    printf("====================================================================================================\n");

    /* ==================== Left Panel: Live Combat Arena (Rows 4..15) ==================== */
    /* Row 4: Boss HP Bar */
    printf("  \033[1;31m[ENEMY CARRIER BOSS]\033[0m HP:[");
    for (i = 0; i < 10; i++) {
        printf(i < bars ? "#" : ".");
    }
    printf("] %4d/1000  \n", boss_hp);

    /* Rows 5..6: Boss Aircraft (sways horizontally with frame) */
    printf("  %s\\======[O]======/                  \n", pad);
    printf("  %s   \\===[V]===/                     \n", pad);

    /* Rows 7..11: Scrolling Starfield & Laser Beam */
    for (i = 0; i < 5; i++) {
        int star_pos = ((frame + i * 3) % 5 == 0);
        char left_star  = star_pos ? '*' : ' ';
        char right_star = ((frame + i * 2) % 4 == 0) ? '.' : ' ';

        if (laser_frames > 0) {
            /* Show yellow 2X laser or cyan normal laser when player presses '1' */
            if (is_double_laser) {
                printf("      %c          \033[1;33m|||||\033[0m             %c       \n", left_star, right_star);
            } else {
                printf("      %c           \033[1;36m|||\033[0m              %c       \n", left_star, right_star);
            }
        } else {
            printf("      %c                              %c       \n", left_star, right_star);
        }
    }

    /* Rows 12..14: Player Fighter Jet */
    printf("                  /A\\                        \n");
    printf("                /=====\\                      \n");
    printf("     \033[1;32m[FIGHTER JET]\033[0m (ATK: 100 | 2X CHARGE: %s)\n",
           double_charge ? "\033[1;33mON \033[0m" : "OFF");
    printf("----------------------------------------------------------------------------------------------------\n");

    /* ==================== Right Panel: Server System Radar at Col 47 ==================== */
    char os_val[64] = "-", up_val[64] = "-", ram_val[64] = "-", load_val[64] = "-";
    if (has_scanned) {
        sscanf(sys_info, "OS: %63[^|]| Uptime: %63[^|]| Free RAM: %63[^|]| Load: %63[^\n]",
               os_val, up_val, ram_val, load_val);
    }

    printf("\033[4;47H|  \033[1;32m[ Server Live Telemetry Data ]\033[0m                ");
    if (!has_scanned) {
        printf("\033[5;47H|  Status         : \033[1;30mNot Scanned (Press '2' to scan)\033[0m   ");
        printf("\033[6;47H|  OS Version     : ---                              ");
        printf("\033[7;47H|  Server Uptime  : ---                              ");
        printf("\033[8;47H|  Free RAM       : ---                              ");
        printf("\033[9;47H|  System Load    : ---                              ");
    } else {
        printf("\033[5;47H|  Status         : \033[1;36mRADAR LINKED (OP_SYSINFO)\033[0m         ");
        printf("\033[6;47H|  OS Version     : %-28s     ", os_val);
        printf("\033[7;47H|  Server Uptime  : %-28s     ", up_val);
        printf("\033[8;47H|  Free RAM       : \033[1;33m%-28s\033[0m     ", ram_val);
        printf("\033[9;47H|  System Load    : %-28s     ", load_val);
    }

    printf("\033[10;47H|  --------------------------------------------------");
    printf("\033[11;47H|  \033[1;35m[ Tactical Energy Linkage ]\033[0m                       ");
    printf("\033[12;47H|  2X Laser Boost : %s          ",
           double_charge ? "\033[1;33m* READY (2X DMG ACTIVE) *\033[0m" : "INACTIVE (Needs >500MB RAM)");
    printf("\033[13;47H|  --------------------------------------------------");
    printf("\033[14;47H|  Refresh Frame  : Frame #%-8d                   ", frame);

    /* ==================== Bottom Action Menu & Battle Log (Rows 16..20) ==================== */
    printf("\033[16;1H\033[2K[Battle Log] %s\n", last_msg);
    printf("\033[2K----------------------------------------------------------------------------------------------------\n");
    printf("\033[2KControls (Press key directly, no Enter needed):\n");
    printf("\033[2K  \033[1;36m[1]\033[0m Fire Laser at Boss (OP_ATTACK)        \033[1;33m[2]\033[0m Scan Server System Info -> Right Panel (OP_SYSINFO)\n");
    printf("\033[2K  \033[1;31m[3]\033[0m Send Malicious Packet (Security Test) \033[1;37m[0]\033[0m Return to Base / Quit\n");
    fflush(stdout);
}

/*
 * Parse the formatted status string returned by the Server
 * Format: "HP:%d|CHARGE:%d|SYS:%s|MSG:%s"
 */
void parse_server_response(const char *resp, int *boss_hp, int *double_charge, char *sys_info, char *last_msg) {
    const char *sys_ptr = strstr(resp, "|SYS:");
    const char *msg_ptr = strstr(resp, "|MSG:");

    sscanf(resp, "HP:%d|CHARGE:%d", boss_hp, double_charge);

    if (sys_ptr != NULL && msg_ptr != NULL && msg_ptr > sys_ptr) {
        size_t sys_len = (size_t)(msg_ptr - (sys_ptr + 5));
        if (sys_len >= 255) sys_len = 255;
        memcpy(sys_info, sys_ptr + 5, sys_len);
        sys_info[sys_len] = '\0';

        strncpy(last_msg, msg_ptr + 5, 255);
        last_msg[255] = '\0';
    }
}

int main(int argc, char *argv[]) {
    int sock_fd;
    struct sockaddr_in server_addr;
    PacketHeader hdr;
    char payload[MAX_BODY_SIZE + 1];
    int auto_mode = 0;
    int i;

    /* UI state variables */
    int boss_hp = 1000;
    int double_charge = 0;
    int has_scanned = 0;      /* 1 after player presses '2' to scan system info */
    int frame = 0;            /* Screen refresh frame counter */
    int laser_frames = 0;     /* Remaining frames to display laser beam animation */
    int is_double_laser = 0;  /* Whether the current laser animation is a 2X overcharged beam */
    char sys_info[256] = "";
    char last_msg[256] = "Fighter jet airborne! Press [2] to scan server radar or [1] to fire laser!";

    /* Parse command-line arguments: --auto (for concurrency test script) and -d (Runtime Debug) */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0) {
            auto_mode = 1;
        } else if (strcmp(argv[i], "-d") == 0) {
            g_debug_runtime = 1;
        }
    }

    /* 1. Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket() failed");
        return 1;
    }

    /* 2. Configure server address (127.0.0.1:8888) */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    /* 3. Connect to server */
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect() failed (Please make sure ./server is running)");
        close(sock_fd);
        return 1;
    }

    /*
     * Automated Concurrency Test Mode (./client --auto)
     * Sends a request every 2 seconds for 10 iterations (20 seconds total) to prove 10+ concurrent clients
     */
    if (auto_mode) {
        printf("[Auto-Client PID=%d] Connected to server; running 20-second automated test...\n", getpid());
        for (i = 0; i < 10; i++) {
            if (i % 2 == 0) {
                send_pkt(sock_fd, OP_SYSINFO, NULL, 0);
            } else {
                send_pkt(sock_fd, OP_ATTACK, NULL, 0);
            }

            if (recv_pkt(sock_fd, &hdr, payload) < 0) {
                printf("[Auto-Client PID=%d] Disconnected from server\n", getpid());
                break;
            }
            sleep(2);
        }
        printf("[Auto-Client PID=%d] 20-second concurrency test completed cleanly.\n", getpid());
        close(sock_fd);
        return 0;
    }

    /* ==================== Interactive Split-Screen Live Refresh Mode ==================== */
    init_terminal();

    while (1) {
        fd_set read_fds;
        struct timeval tv;

        /* Render split-screen interface on every frame */
        draw_split_screen(frame, boss_hp, double_charge, has_scanned, sys_info, last_msg, laser_frames, is_double_laser);
        frame++;
        if (laser_frames > 0) {
            laser_frames--;
        }

        /*
         * Use select() to monitor both keyboard input (STDIN_FILENO) and server socket (sock_fd)
         * with a 0.25-second timeout so the screen refreshes continuously at 4 FPS.
         */
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock_fd, &read_fds);

        tv.tv_sec = 0;
        tv.tv_usec = 250000; /* Refresh every 250ms (4 FPS) */

        int max_fd = (sock_fd > STDIN_FILENO) ? sock_fd : STDIN_FILENO;
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

        if (ready < 0) {
            break;
        }

        /* Check if server closed the connection (e.g. 15s Idle Timeout triggered on server) */
        if (FD_ISSET(sock_fd, &read_fds)) {
            if (recv_pkt(sock_fd, &hdr, payload) <= 0) {
                printf("\n\033[1;31m[Disconnected] Server closed connection (15s Idle Timeout or Security Block)!\033[0m\n");
                break;
            }
        }

        /* Check if player pressed a key */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) <= 0) {
                break;
            }

            if (ch == '0' || ch == 'q' || ch == 'Q') {
                /* [0] Quit */
                printf("\n\033[1;32mReturning to base. Thanks for playing!\033[0m\n");
                break;

            } else if (ch == '1') {
                /* [1] Fire laser at Boss (OP_ATTACK) */
                is_double_laser = double_charge;
                if (send_pkt(sock_fd, OP_ATTACK, "FIRE", 4) < 0 ||
                    recv_pkt(sock_fd, &hdr, payload) < 0) {
                    printf("\n\033[1;31m[Disconnected] Failed to send/receive attack packet!\033[0m\n");
                    break;
                }
                parse_server_response(payload, &boss_hp, &double_charge, sys_info, last_msg);
                laser_frames = 3; /* Show laser beam animation for 3 frames */

            } else if (ch == '2') {
                /* [2] Scan server system telemetry and show in Right Panel (OP_SYSINFO) */
                if (send_pkt(sock_fd, OP_SYSINFO, "RADAR", 5) < 0 ||
                    recv_pkt(sock_fd, &hdr, payload) < 0) {
                    printf("\n\033[1;31m[Disconnected] Failed to retrieve system telemetry from server!\033[0m\n");
                    break;
                }
                parse_server_response(payload, &boss_hp, &double_charge, sys_info, last_msg);
                has_scanned = 1; /* Enable Right Panel telemetry display */

            } else if (ch == '3') {
                /* [3] Send forged malicious packet (demonstrate Security & Robustness protection) */
                PacketHeader bad_hdr;
                bad_hdr.magic = MAGIC_BYTE;
                bad_hdr.opcode = OP_ATTACK;
                bad_hdr.checksum = 0xDEAD;
                bad_hdr.length = 999999;

                snprintf(last_msg, sizeof(last_msg), "Sending forged malicious packet (Length=999999, Checksum=0xDEAD)...");
                draw_split_screen(frame, boss_hp, double_charge, has_scanned, sys_info, last_msg, 0, 0);
                send(sock_fd, &bad_hdr, sizeof(PacketHeader), 0);

                if (recv_pkt(sock_fd, &hdr, payload) < 0) {
                    printf("\n\033[1;31m[Security Test] Server intercepted the malicious packet and closed this connection!\033[0m\n");
                    break;
                }
            }
        }
    }

    close(sock_fd);
    return 0;
}
