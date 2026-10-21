#pragma once

#include "net.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

#define TCP_RX_BUF_SIZE     4096
#define TCP_MAX_PENDING     8
#define TCP_MAX_SOCKETS     32
#define TCP_DEFAULT_WINDOW  2048
#define TCP_MSS             1460

typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} TCPState;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset; // (data_offset >> 4) * 4
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} TCPHeader;

typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_length;
} TCPPseudoHeader;

typedef struct TCPSocket TCPSocket;

struct TCPSocket {
    uint32_t    id;
    bool        used;
    TCPState    state;
    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;

    uint32_t    seq_num;
    uint32_t    ack_num;
    uint16_t    remote_window;

    // Receive circular stream buffer
    uint8_t     rx_buffer[TCP_RX_BUF_SIZE];
    size_t      rx_read_pos;
    size_t      rx_write_pos;
    size_t      rx_count;

    // Server backlog queue for accept()
    TCPSocket*  pending_connections[TCP_MAX_PENDING];
    size_t      pending_count;
    size_t      backlog;

    int         ref_count;
    int         error;
    bool        eof_reached;
};

void        TCP_Initialize(void);
TCPSocket*  TCP_SocketCreate(void);
int         TCP_Bind(TCPSocket* sock, uint32_t local_ip, uint16_t local_port);
int         TCP_Listen(TCPSocket* sock, int backlog);
int         TCP_Connect(TCPSocket* sock, uint32_t remote_ip, uint16_t remote_port);
TCPSocket*  TCP_Accept(TCPSocket* sock, uint32_t* client_ip, uint16_t* client_port);
int         TCP_Send(TCPSocket* sock, const void* data, size_t len);
int         TCP_Recv(TCPSocket* sock, void* buf, size_t len);
void        TCP_Close(TCPSocket* sock);
void        TCP_Handle(NetInterface* netif, uint32_t src_ip, uint32_t dst_ip, const void* packet, size_t len);
