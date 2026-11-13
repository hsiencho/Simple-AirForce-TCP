#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

/* Default TCP port listened by the server */
#define SERVER_PORT 8888

/* Packet security magic byte and maximum payload size */
#define MAGIC_BYTE 0x90       /* Every valid packet must start with 0x90 */
#define MAX_BODY_SIZE 1024    /* Maximum payload length to prevent buffer overflow */

/* Packet Operation Codes (Opcodes) */
#define OP_SYSINFO  1         /* 1 = Client requests server system info (Radar scan) */
#define OP_ATTACK   2         /* 2 = Client fires laser at the Boss */
#define OP_RESPONSE 3         /* 3 = Server sends response result back to client */

/*
 * Custom 8-Byte Security Packet Header (PacketHeader)
 * __attribute__((packed)) prevents compiler struct padding so that
 * the header is guaranteed to be exactly 8 bytes over the network.
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic;     /* [1B] Magic identifier (must be 0x90) */
    uint8_t  opcode;    /* [1B] Operation code: 1=SYSINFO, 2=ATTACK, 3=RESPONSE */
    uint16_t checksum;  /* [2B] Simple byte-sum checksum of the payload to detect tampering */
    uint32_t length;    /* [4B] Payload byte length to prevent out-of-bounds reads */
} PacketHeader;

/* Global runtime flags (defined in common.c) */
extern int g_debug_runtime;   /* Runtime debug flag: 1 = enabled (-d), 0 = disabled */
extern int g_unsafe_mode;     /* Unsafe mode flag: 1 = disable protections (--unsafe) for robustness testing */

/* ==================== Shared Utility Functions (implemented in common.c) ==================== */

/*
 * Two-level logging function
 * is_debug: if 1, this is a runtime debug message and only prints when g_debug_runtime == 1;
 *           if 0, this is a general system message and always prints.
 * msg: message string to print
 */
void log_msg(int is_debug, const char *msg);

/*
 * Checksum calculation function
 * Sums all bytes in the payload and returns a 16-bit unsigned integer
 */
uint16_t calc_checksum(const char *data, uint32_t len);

/*
 * Lightweight XOR cipher function
 * XORs each byte of data with key 0x5A.
 * Calling it once encrypts plaintext; calling it again restores the original plaintext.
 */
void xor_cipher(char *data, uint32_t len);

/*
 * Safe packet sending function
 * Builds the 8-byte header, computes the checksum, encrypts payload with XOR, and sends it.
 * Returns: 0 on success, -1 on failure
 */
int send_pkt(int fd, uint8_t opcode, const char *payload, uint32_t len);

/*
 * Safe packet receiving function
 * Reads the 8-byte header, verifies magic byte and payload length,
 * reads and XOR-decrypts the payload, and verifies the checksum.
 * Returns: 0 on success, -1 on disconnect/error, -2 if blocked by security checks
 */
int recv_pkt(int fd, PacketHeader *hdr, char *payload);

#endif
