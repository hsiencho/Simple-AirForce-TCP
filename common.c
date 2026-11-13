#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include "common.h"

/* Global flags: runtime debug and unsafe mode are disabled by default */
int g_debug_runtime = 0;
int g_unsafe_mode = 0;

/*
 * Two-level logging function
 * Controls whether runtime debug messages are printed based on the -d flag
 */
void log_msg(int is_debug, const char *msg) {
    /* Skip runtime debug messages if -d was not passed at startup */
    if (is_debug && !g_debug_runtime) {
        return;
    }

    if (is_debug) {
        printf("[Runtime-Debug] %s\n", msg);
    } else {
        printf("[System-Info] %s\n", msg);
    }
}

/*
 * Checksum calculation function
 * Adds every byte of the payload as an unsigned integer
 */
uint16_t calc_checksum(const char *data, uint32_t len) {
    uint16_t sum = 0;
    uint32_t i;

    for (i = 0; i < len; i++) {
        sum += (uint8_t)data[i];
    }
    return sum;
}

/*
 * XOR encryption/decryption function
 * Uses 0x5A as the key to XOR each byte in the buffer.
 * Prevents plaintext eavesdropping in Wireshark/tcpdump.
 */
void xor_cipher(char *data, uint32_t len) {
    uint32_t i;

    for (i = 0; i < len; i++) {
        data[i] ^= 0x5A;
    }
}

/*
 * Helper function: send_all
 * Ensures all 'len' bytes are written to the TCP socket even if send() is interrupted (EINTR)
 * or performs a partial write.
 */
static int send_all(int fd, const void *buf, uint32_t len) {
    uint32_t total = 0;
    const char *p = (const char *)buf;

    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (uint32_t)n;
    }
    return 0;
}

/*
 * Helper function: recv_all
 * Ensures all 'len' bytes are read from the TCP stream (handles TCP packet fragmentation / partial reads).
 */
static int recv_all(int fd, void *buf, uint32_t len) {
    uint32_t total = 0;
    char *p = (char *)buf;

    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) {
            return -1; /* 0 = EOF/disconnect, <0 = error or SO_RCVTIMEO timeout */
        }
        total += (uint32_t)n;
    }
    return 0;
}

/*
 * Safe packet sending function
 * Assembles the 8-byte header, computes checksum, XOR-encrypts the payload, and sends via socket
 */
int send_pkt(int fd, uint8_t opcode, const char *payload, uint32_t len) {
    PacketHeader hdr;
    char enc_buf[MAX_BODY_SIZE];

    /* Check if payload length exceeds maximum allowed size */
    if (len > MAX_BODY_SIZE) {
        return -1;
    }

    /* Fill in the 8-byte packet header */
    hdr.magic = MAGIC_BYTE;
    hdr.opcode = opcode;
    hdr.length = len;

    /* If there is a payload, calculate checksum on plaintext and XOR-encrypt a copy */
    if (len > 0 && payload != NULL) {
        hdr.checksum = calc_checksum(payload, len);
        memcpy(enc_buf, payload, len);
        xor_cipher(enc_buf, len);
        log_msg(1, "XOR-encrypted payload (key=0x5A) and computed Checksum before send()");
    } else {
        hdr.checksum = 0;
        log_msg(1, "Prepared empty-body command packet (Length=0) for send()");
    }

    /* Compile-time debug: only compiled when built with 'make debug' (-DDEBUG_COMPILE) */
#ifdef DEBUG_COMPILE
    printf("[Compile-Debug] Sending Packet -> Magic: 0x%02X, Opcode: %d, Checksum: %u, Length: %u\n",
           hdr.magic, hdr.opcode, hdr.checksum, hdr.length);
#endif

    /* Send the fixed 8-byte header first */
    if (send_all(fd, &hdr, sizeof(PacketHeader)) < 0) {
        return -1;
    }

    /* Send the encrypted payload if present */
    if (len > 0) {
        if (send_all(fd, enc_buf, len) < 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Safe packet receiving function
 * Reads the header and payload, performing security validation unless --unsafe is enabled
 */
int recv_pkt(int fd, PacketHeader *hdr, char *payload) {
    /* Step 1: Read the fixed 8-byte packet header */
    if (recv_all(fd, hdr, sizeof(PacketHeader)) < 0) {
        /* Disconnect, error, or socket receive timeout */
        return -1;
    }

    /* Compile-time debug: print received header details if compiled with -DDEBUG_COMPILE */
#ifdef DEBUG_COMPILE
    printf("[Compile-Debug] Received Header -> Magic: 0x%02X, Opcode: %d, Checksum: %u, Length: %u\n",
           hdr->magic, hdr->opcode, hdr->checksum, hdr->length);
#endif

    /*
     * Robustness Example 2: Malicious / Corrupted Packet Inspection
     * In normal mode (!g_unsafe_mode), strictly verify Magic Byte and Length.
     * In unsafe mode (--unsafe), skip validation to demonstrate crash vulnerability.
     */
    if (!g_unsafe_mode) {
        /* Check 1: Verify Magic Byte is 0x90 */
        if (hdr->magic != MAGIC_BYTE) {
            printf("[SECURITY ALERT] Invalid packet blocked! Wrong Magic Byte (got 0x%02X, expected 0x90)\n",
                   hdr->magic);
            return -2;
        }
        /* Check 2: Verify length does not exceed 1024 bytes to prevent buffer overflow */
        if (hdr->length > MAX_BODY_SIZE) {
            printf("[SECURITY ALERT] Oversized packet blocked! Length=%u exceeds max limit %d!\n",
                   hdr->length, MAX_BODY_SIZE);
            return -2;
        }
    } else {
        /*
         * Unsafe mode (--unsafe): intentionally skip validation
         * When a malicious oversized packet arrives, abort to demonstrate an unprotected server crash
         */
        if (hdr->length > MAX_BODY_SIZE) {
            printf("[UNSAFE WARNING] Unchecked packet length (Length=%u), buffer overflow crash triggered!\n",
                   hdr->length);
            abort();
        }
    }

    /* Step 2: Read payload bytes if length > 0 */
    if (hdr->length > 0) {
        if (recv_all(fd, payload, hdr->length) < 0) {
            return -1;
        }

        /* Decrypt the payload back to plaintext using XOR */
        xor_cipher(payload, hdr->length);
        payload[hdr->length] = '\0'; /* Null-terminate string */

        /* In normal mode, verify payload integrity using Checksum */
        if (!g_unsafe_mode) {
            uint16_t actual_sum = calc_checksum(payload, hdr->length);
            if (actual_sum != hdr->checksum) {
                printf("[SECURITY ALERT] Checksum mismatch! (Header=%u, Computed=%u). Packet tampering detected!\n",
                       hdr->checksum, actual_sum);
                return -2;
            }
        }
    } else {
        payload[0] = '\0';
    }

    return 0;
}
