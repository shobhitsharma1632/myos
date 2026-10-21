#include "tcp.h"
#include "ipv4.h"
#include <hal/task.h>
#include <hal/heap.h>
#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>

#define MODULE "TCP"

static TCPSocket g_TCPSockets[TCP_MAX_SOCKETS];
static uint16_t  g_NextEphemeralPort = 49152;
static uint32_t  g_NextISN           = 100000;

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void* segment, size_t len)
{
    uint32_t sum = 0;

    // Pseudo-header sum:
    // src_ip (2 words)
    sum += (src_ip & 0xFFFF);
    sum += (src_ip >> 16);
    // dst_ip (2 words)
    sum += (dst_ip & 0xFFFF);
    sum += (dst_ip >> 16);
    // zero (8-bit) + protocol (8-bit)
    sum += htons((uint16_t)IPV4_PROTO_TCP);
    // tcp length (16-bit)
    sum += htons((uint16_t)len);

    // Segment payload sum:
    const uint8_t* p = (const uint8_t*)segment;
    while (len > 1)
    {
        uint16_t word = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        sum += word;
        p += 2;
        len -= 2;
    }

    if (len > 0)
    {
        sum += (uint16_t)p[0];
    }

    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

static uint8_t  s_tcp_tx_buffer[4][1536];
static int      s_tcp_tx_depth = 0;
static TCPSocket s_rst_sock;

static bool tcp_send_packet(NetInterface* netif, TCPSocket* sock, uint8_t flags, const void* payload, size_t payload_len)
{
    if (!netif || !sock) return false;

    if (sizeof(TCPHeader) + payload_len > 1500)
        return false;

    int depth = s_tcp_tx_depth++;
    uint8_t* buffer = s_tcp_tx_buffer[depth % 4];

    TCPHeader* hdr = (TCPHeader*)buffer;
    hdr->src_port       = htons(sock->local_port);
    hdr->dst_port       = htons(sock->remote_port);
    hdr->seq_num        = htonl(sock->seq_num);
    hdr->ack_num        = htonl(sock->ack_num);
    hdr->data_offset    = (sizeof(TCPHeader) / 4) << 4;
    hdr->flags          = flags;
    hdr->window_size    = htons(TCP_DEFAULT_WINDOW);
    hdr->checksum       = 0;
    hdr->urgent_pointer = 0;

    if (payload && payload_len > 0)
    {
        memcpy(buffer + sizeof(TCPHeader), payload, payload_len);
    }

    size_t segment_len = sizeof(TCPHeader) + payload_len;
    hdr->checksum = tcp_checksum(sock->local_ip, sock->remote_ip, buffer, segment_len);

    bool result = IPv4_Send(netif, sock->remote_ip, IPV4_PROTO_TCP, buffer, segment_len);
    s_tcp_tx_depth--;
    return result;
}

void TCP_Initialize(void)
{
    memset(g_TCPSockets, 0, sizeof(g_TCPSockets));
    for (size_t i = 0; i < TCP_MAX_SOCKETS; i++)
    {
        g_TCPSockets[i].id = i + 1;
        g_TCPSockets[i].used = false;
        g_TCPSockets[i].state = TCP_STATE_CLOSED;
    }
    log_info(MODULE, "TCP protocol stack initialized (%d socket pool).", TCP_MAX_SOCKETS);
}

TCPSocket* TCP_SocketCreate(void)
{
    for (size_t i = 0; i < TCP_MAX_SOCKETS; i++)
    {
        if (!g_TCPSockets[i].used)
        {
            TCPSocket* s = &g_TCPSockets[i];
            memset(s, 0, sizeof(TCPSocket));
            s->id = i + 1;
            s->used = true;
            s->state = TCP_STATE_CLOSED;
            s->local_ip = Net_GetInterface()->ip;
            s->backlog = 5;
            s->ref_count = 1;
            return s;
        }
    }
    log_warn(MODULE, "TCP_SocketCreate: Socket table exhausted!");
    return NULL;
}

int TCP_Bind(TCPSocket* sock, uint32_t local_ip, uint16_t local_port)
{
    if (!sock || !sock->used) return -1;

    if (local_port != 0)
    {
        for (size_t i = 0; i < TCP_MAX_SOCKETS; i++)
        {
            if (g_TCPSockets[i].used && &g_TCPSockets[i] != sock)
            {
                if (g_TCPSockets[i].local_port == local_port &&
                    (g_TCPSockets[i].state == TCP_STATE_LISTEN || g_TCPSockets[i].state == TCP_STATE_ESTABLISHED))
                {
                    log_warn(MODULE, "TCP_Bind: Port %u already in use!", local_port);
                    return -1;
                }
            }
        }
    }

    sock->local_ip = (local_ip != 0) ? local_ip : Net_GetInterface()->ip;
    sock->local_port = local_port;
    return 0;
}

int TCP_Listen(TCPSocket* sock, int backlog)
{
    if (!sock || !sock->used || sock->local_port == 0) return -1;

    sock->state = TCP_STATE_LISTEN;
    sock->backlog = (backlog > 0 && backlog <= TCP_MAX_PENDING) ? backlog : TCP_MAX_PENDING;
    sock->pending_count = 0;
    return 0;
}

int TCP_Connect(TCPSocket* sock, uint32_t remote_ip, uint16_t remote_port)
{
    if (!sock || !sock->used || remote_ip == 0 || remote_port == 0) return -1;

    NetInterface* netif = Net_GetInterface();
    if (!netif) return -1;

    // Allocate ephemeral port if unbound
    if (sock->local_port == 0)
    {
        sock->local_port = g_NextEphemeralPort++;
        if (g_NextEphemeralPort < 49152) g_NextEphemeralPort = 49152;
    }
    sock->local_ip = netif->ip;
    sock->remote_ip = remote_ip;
    sock->remote_port = remote_port;

    g_NextISN += 1000 + Task_GetTicks();
    sock->seq_num = g_NextISN;
    sock->ack_num = 0;
    sock->state = TCP_STATE_SYN_SENT;
    sock->rx_read_pos = 0;
    sock->rx_write_pos = 0;
    sock->rx_count = 0;
    sock->eof_reached = false;
    sock->error = 0;

    // Send SYN packet
    tcp_send_packet(netif, sock, TCP_FLAG_SYN, NULL, 0);
    sock->seq_num++; // SYN consumes 1 sequence number

    // Wait until connection established or timeout
    uint32_t start = Task_GetTicks();
    while (sock->state == TCP_STATE_SYN_SENT)
    {
        if (Task_GetTicks() - start > 300) // 3 seconds timeout
        {
            sock->state = TCP_STATE_CLOSED;
            sock->error = -1;
            log_warn(MODULE, "TCP_Connect: Connection timed out!");
            return -1;
        }
        Task_Yield();
    }

    if (sock->state == TCP_STATE_ESTABLISHED)
    {
        return 0;
    }

    return -1;
}

TCPSocket* TCP_Accept(TCPSocket* sock, uint32_t* client_ip, uint16_t* client_port)
{
    if (!sock || !sock->used || sock->state != TCP_STATE_LISTEN) return NULL;

    while (sock->pending_count == 0)
    {
        if (sock->state != TCP_STATE_LISTEN)
            return NULL;
        Task_Yield();
    }

    TCPSocket* client = sock->pending_connections[0];
    for (size_t i = 1; i < sock->pending_count; i++)
    {
        sock->pending_connections[i - 1] = sock->pending_connections[i];
    }
    sock->pending_count--;

    if (client_ip) *client_ip = client->remote_ip;
    if (client_port) *client_port = client->remote_port;

    return client;
}

int TCP_Send(TCPSocket* sock, const void* data, size_t len)
{
    if (!sock || !sock->used || !data || len == 0) return 0;
    if (sock->state != TCP_STATE_ESTABLISHED && sock->state != TCP_STATE_CLOSE_WAIT)
        return -1;

    NetInterface* netif = Net_GetInterface();
    if (!netif) return -1;

    const uint8_t* ptr = (const uint8_t*)data;
    size_t total_sent = 0;

    while (total_sent < len)
    {
        size_t chunk = len - total_sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        tcp_send_packet(netif, sock, TCP_FLAG_PSH | TCP_FLAG_ACK, ptr + total_sent, chunk);
        sock->seq_num += chunk;
        total_sent += chunk;
        Task_Yield();
    }

    return (int)total_sent;
}

int TCP_Recv(TCPSocket* sock, void* buf, size_t len)
{
    if (!sock || !sock->used || !buf || len == 0) return 0;

    uint8_t* dst = (uint8_t*)buf;

    // Wait until bytes are present or connection is closed
    while (sock->rx_count == 0)
    {
        if (sock->eof_reached || sock->state == TCP_STATE_CLOSED ||
            sock->state == TCP_STATE_CLOSE_WAIT || sock->state == TCP_STATE_TIME_WAIT)
        {
            return 0; // EOF
        }
        if (sock->error != 0)
        {
            return -1;
        }
        Task_Yield();
    }

    size_t to_read = len;
    if (to_read > sock->rx_count) to_read = sock->rx_count;

    for (size_t i = 0; i < to_read; i++)
    {
        dst[i] = sock->rx_buffer[sock->rx_read_pos];
        sock->rx_read_pos = (sock->rx_read_pos + 1) % TCP_RX_BUF_SIZE;
    }
    sock->rx_count -= to_read;

    return (int)to_read;
}

void TCP_Close(TCPSocket* sock)
{
    if (!sock || !sock->used) return;

    NetInterface* netif = Net_GetInterface();

    if (sock->state == TCP_STATE_ESTABLISHED)
    {
        sock->state = TCP_STATE_FIN_WAIT_1;
        tcp_send_packet(netif, sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        sock->seq_num++;
    }
    else if (sock->state == TCP_STATE_CLOSE_WAIT)
    {
        sock->state = TCP_STATE_LAST_ACK;
        tcp_send_packet(netif, sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        sock->seq_num++;
    }
    else if (sock->state == TCP_STATE_LISTEN)
    {
        sock->state = TCP_STATE_CLOSED;
    }

    sock->ref_count--;
    if (sock->ref_count <= 0)
    {
        sock->used = false;
        sock->state = TCP_STATE_CLOSED;
    }
}

void TCP_Handle(NetInterface* netif, uint32_t src_ip, uint32_t dst_ip, const void* packet, size_t len)
{
    if (!netif || !packet || len < sizeof(TCPHeader))
        return;

    const TCPHeader* hdr = (const TCPHeader*)packet;
    size_t header_len = (hdr->data_offset >> 4) * 4;
    if (header_len < sizeof(TCPHeader) || len < header_len)
        return;

    // Verify checksum
    if (tcp_checksum(src_ip, dst_ip, packet, len) != 0)
    {
        log_warn(MODULE, "TCP Checksum mismatch, discarding packet.");
        return;
    }

    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint32_t seq      = ntohl(hdr->seq_num);
    uint32_t ack      = ntohl(hdr->ack_num);
    uint8_t  flags    = hdr->flags;

    const uint8_t* payload = (const uint8_t*)packet + header_len;
    size_t payload_len = len - header_len;

    // 1. Find matching active socket
    TCPSocket* sock = NULL;
    for (size_t i = 0; i < TCP_MAX_SOCKETS; i++)
    {
        if (g_TCPSockets[i].used && g_TCPSockets[i].state != TCP_STATE_LISTEN)
        {
            if (g_TCPSockets[i].local_port == dst_port &&
                g_TCPSockets[i].remote_port == src_port &&
                (g_TCPSockets[i].remote_ip == src_ip || g_TCPSockets[i].remote_ip == 0))
            {
                sock = &g_TCPSockets[i];
                break;
            }
        }
    }

    // 2. If not found and SYN flag set, check for listening socket
    if (!sock && (flags & TCP_FLAG_SYN))
    {
        for (size_t i = 0; i < TCP_MAX_SOCKETS; i++)
        {
            if (g_TCPSockets[i].used && g_TCPSockets[i].state == TCP_STATE_LISTEN &&
                g_TCPSockets[i].local_port == dst_port)
            {
                TCPSocket* srv = &g_TCPSockets[i];
                if (srv->pending_count < srv->backlog)
                {
                    TCPSocket* child = TCP_SocketCreate();
                    if (child)
                    {
                        child->local_ip = dst_ip;
                        child->local_port = dst_port;
                        child->remote_ip = src_ip;
                        child->remote_port = src_port;
                        child->ack_num = seq + 1;
                        child->seq_num = (Task_GetTicks() * 2000) + 54321;
                        child->state = TCP_STATE_SYN_RECEIVED;

                        // Send SYN+ACK
                        tcp_send_packet(netif, child, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                        child->seq_num++;
                    }
                }
                return;
            }
        }
    }

    if (!sock)
    {
        // No socket found; send RST if not incoming RST
        if (!(flags & TCP_FLAG_RST))
        {
            memset(&s_rst_sock, 0, sizeof(s_rst_sock));
            s_rst_sock.local_ip = dst_ip;
            s_rst_sock.local_port = dst_port;
            s_rst_sock.remote_ip = src_ip;
            s_rst_sock.remote_port = src_port;
            s_rst_sock.seq_num = ack;
            s_rst_sock.ack_num = seq + (payload_len > 0 ? payload_len : 1);
            tcp_send_packet(netif, &s_rst_sock, TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0);
        }
        return;
    }

    // 3. Process according to current state
    switch (sock->state)
    {
        case TCP_STATE_SYN_SENT:
        {
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK))
            {
                sock->ack_num = seq + 1;
                sock->state = TCP_STATE_ESTABLISHED;
                tcp_send_packet(netif, sock, TCP_FLAG_ACK, NULL, 0);
            }
            else if (flags & TCP_FLAG_RST)
            {
                sock->state = TCP_STATE_CLOSED;
                sock->error = -1;
            }
            break;
        }

        case TCP_STATE_SYN_RECEIVED:
        {
            if (flags & TCP_FLAG_ACK)
            {
                sock->state = TCP_STATE_ESTABLISHED;
                // Enqueue to parent listening socket
                for (size_t i = 0; i < TCP_MAX_SOCKETS; i++)
                {
                    if (g_TCPSockets[i].used && g_TCPSockets[i].state == TCP_STATE_LISTEN &&
                        g_TCPSockets[i].local_port == sock->local_port)
                    {
                        if (g_TCPSockets[i].pending_count < TCP_MAX_PENDING)
                        {
                            g_TCPSockets[i].pending_connections[g_TCPSockets[i].pending_count++] = sock;
                        }
                        break;
                    }
                }
            }
            break;
        }

        case TCP_STATE_ESTABLISHED:
        {
            if (flags & TCP_FLAG_RST)
            {
                sock->state = TCP_STATE_CLOSED;
                sock->error = -1;
                sock->eof_reached = true;
                return;
            }

            if (payload_len > 0)
            {
                for (size_t i = 0; i < payload_len; i++)
                {
                    if (sock->rx_count < TCP_RX_BUF_SIZE)
                    {
                        sock->rx_buffer[sock->rx_write_pos] = payload[i];
                        sock->rx_write_pos = (sock->rx_write_pos + 1) % TCP_RX_BUF_SIZE;
                        sock->rx_count++;
                    }
                }
                sock->ack_num = seq + payload_len;
                tcp_send_packet(netif, sock, TCP_FLAG_ACK, NULL, 0);
            }

            if (flags & TCP_FLAG_FIN)
            {
                sock->ack_num = seq + payload_len + 1;
                sock->eof_reached = true;
                sock->state = TCP_STATE_CLOSE_WAIT;
                tcp_send_packet(netif, sock, TCP_FLAG_ACK, NULL, 0);
            }
            break;
        }

        case TCP_STATE_FIN_WAIT_1:
        {
            if (flags & TCP_FLAG_FIN)
            {
                sock->ack_num = seq + 1;
                sock->eof_reached = true;
                tcp_send_packet(netif, sock, TCP_FLAG_ACK, NULL, 0);
                sock->state = TCP_STATE_TIME_WAIT;
            }
            else if (flags & TCP_FLAG_ACK)
            {
                sock->state = TCP_STATE_FIN_WAIT_2;
            }
            break;
        }

        case TCP_STATE_FIN_WAIT_2:
        {
            if (flags & TCP_FLAG_FIN)
            {
                sock->ack_num = seq + 1;
                sock->eof_reached = true;
                tcp_send_packet(netif, sock, TCP_FLAG_ACK, NULL, 0);
                sock->state = TCP_STATE_CLOSED;
            }
            break;
        }

        case TCP_STATE_CLOSE_WAIT:
        {
            // Waiting for local application to call TCP_Close
            break;
        }

        case TCP_STATE_LAST_ACK:
        {
            if (flags & TCP_FLAG_ACK)
            {
                sock->state = TCP_STATE_CLOSED;
            }
            break;
        }

        default:
            break;
    }
}
