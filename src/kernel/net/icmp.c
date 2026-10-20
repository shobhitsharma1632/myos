#include "icmp.h"
#include "ipv4.h"
#include <arch/i686/e1000.h>
#include <hal/task.h>
#include <memory.h>
#include <string.h>
#include <debug.h>

#define MODULE "ICMP"

static uint16_t g_PingSeq        = 0;
static uint16_t g_PingPendingSeq = 0;
static volatile bool g_PingReceived = false;
static uint32_t g_PingReplyTime  = 0;

void ICMP_Handle(NetInterface* netif, uint32_t src_ip, const void* packet, size_t len)
{
    if (!netif || !packet || len < sizeof(ICMPHeader))
        return;

    const ICMPHeader* req = (const ICMPHeader*)packet;

    if (net_checksum(packet, len) != 0)
    {
        log_warn(MODULE, "Invalid ICMP checksum, dropping packet.");
        return;
    }

    if (req->type == ICMP_TYPE_ECHO_REQUEST)
    {
        // Respond with Echo Reply
        uint8_t reply_buf[1500];
        if (len > sizeof(reply_buf)) return;

        memcpy(reply_buf, packet, len);
        ICMPHeader* reply = (ICMPHeader*)reply_buf;
        reply->type     = ICMP_TYPE_ECHO_REPLY;
        reply->code     = 0;
        reply->checksum = 0;
        reply->checksum = net_checksum(reply_buf, len);

        IPv4_Send(netif, src_ip, IPV4_PROTO_ICMP, reply_buf, len);
    }
    else if (req->type == ICMP_TYPE_ECHO_REPLY)
    {
        uint16_t seq = ntohs(req->sequence);
        if (seq == g_PingPendingSeq)
        {
            g_PingReplyTime = Task_GetTicks();
            g_PingReceived = true;
        }
    }
}

int32_t ICMP_Ping(NetInterface* netif, uint32_t target_ip, uint32_t timeout_ms)
{
    if (!netif) return -1;

    // Local loopback
    if (target_ip == netif->ip)
    {
        return 1;
    }

    uint8_t buffer[64];
    ICMPHeader* hdr = (ICMPHeader*)buffer;

    g_PingSeq++;
    g_PingPendingSeq = g_PingSeq;
    g_PingReceived = false;

    hdr->type     = ICMP_TYPE_ECHO_REQUEST;
    hdr->code     = 0;
    hdr->checksum = 0;
    hdr->id       = htons(0x1337);
    hdr->sequence = htons(g_PingSeq);

    const char* ping_data = "myOS ICMP Echo Request Probe Data";
    memcpy(buffer + sizeof(ICMPHeader), ping_data, 32);

    hdr->checksum = net_checksum(buffer, sizeof(ICMPHeader) + 32);

    uint32_t start_tick = Task_GetTicks();

    if (!IPv4_Send(netif, target_ip, IPV4_PROTO_ICMP, buffer, sizeof(ICMPHeader) + 32))
    {
        return -1;
    }

    uint32_t timeout_ticks = (timeout_ms + 9) / 10;
    if (timeout_ticks < 2) timeout_ticks = 2;

    while (!g_PingReceived)
    {
        E1000_Poll();
        if (g_PingReceived) break;

        uint32_t now = Task_GetTicks();
        if (now - start_tick >= timeout_ticks)
        {
            return -1; // Timeout
        }

        Task_Yield();
    }

    uint32_t elapsed_ticks = g_PingReplyTime - start_tick;
    uint32_t rtt_ms = elapsed_ticks * 10;
    if (rtt_ms == 0) rtt_ms = 1;

    return (int32_t)rtt_ms;
}
