/*
 * raw_udp_recv.c - receive UDP datagrams through a RAW socket.
 *
 * A SOCK_RAW/IPPROTO_UDP socket gets a copy of EVERY incoming UDP packet,
 * including the IPv4 header. We use recvmsg with three iovec segments so
 * the kernel "scatters" the packet straight into our header structs:
 *
 *     packet: [ IPv4 hdr | UDP hdr | payload ... ]
 *                 |          |         |
 *     iov[0] -----+          |         |   struct ipv4_hdr (20 bytes)
 *     iov[1] ----------------+         |   struct udp_hdr  ( 8 bytes)
 *     iov[2] --------------------------+   char payload[]
 *
 * Caveat for discussion: scatter works on fixed sizes. If a sender adds IP
 * options (IHL > 5) the UDP header lands partly in iov[0]'s neighbour -
 * we detect that and skip the packet.
 *
 * usage: raw_udp_recv <port> [count]
 * needs: CAP_NET_RAW
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "proto.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <port> [count]\n", argv[0]);
        return 1;
    }
    uint16_t port = (uint16_t)atoi(argv[1]);
    int count = argc > 2 ? atoi(argv[2]) : 1;

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (fd < 0) {
        perror("socket(AF_INET, SOCK_RAW, IPPROTO_UDP)");
        return 1;
    }
    printf("[raw-recv] sniffing all UDP, filtering dst port %u\n", port);
    fflush(stdout);

    while (count > 0) {
        struct ipv4_hdr ip;
        struct udp_hdr udp;
        char payload[2048];

        struct iovec iov[SEG_COUNT] = {
            [SEG_IP]      = { &ip,     sizeof(ip)          },
            [SEG_UDP]     = { &udp,    sizeof(udp)         },
            [SEG_PAYLOAD] = { payload, sizeof(payload) - 1 },
        };
        struct msghdr mh = { .msg_iov = iov, .msg_iovlen = SEG_COUNT };

        ssize_t n = recvmsg(fd, &mh, 0);
        if (n < 0) {
            perror("recvmsg");
            return 1;
        }
        if (n < (ssize_t)(sizeof(ip) + sizeof(udp)))
            continue;
        if ((ip.ver_ihl & 0x0f) != 5) {     /* IP options -> segments misaligned */
            fprintf(stderr, "[raw-recv] skipping packet with IP options\n");
            continue;
        }
        if (ntohs(udp.dest) != port)        /* kernel gives us ALL udp traffic */
            continue;

        size_t plen = (size_t)n - sizeof(ip) - sizeof(udp);
        payload[plen] = '\0';

        /* Verify the UDP checksum ourselves over pseudo hdr + the same iovecs. */
        struct pseudo_hdr ph = {
            .saddr = ip.saddr, .daddr = ip.daddr,
            .zero = 0, .protocol = IPPROTO_UDP, .udp_len = udp.len,
        };
        struct iovec civ[] = {
            { &ph, sizeof(ph) }, { &udp, sizeof(udp) }, { payload, plen },
        };
        int csum_ok = udp.check == 0 || inet_csum_iov(civ, 3) == 0;

        char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip.saddr, src, sizeof(src));
        inet_ntop(AF_INET, &ip.daddr, dst, sizeof(dst));

        printf("[raw-recv] %zd bytes scattered into iov[0..2]\n", n);
        printf("  iov[0] IPv4: ver=%u ihl=%u tot_len=%u id=0x%04x ttl=%u proto=%u "
               "check=0x%04x %s -> %s\n",
               ip.ver_ihl >> 4, ip.ver_ihl & 0x0f, ntohs(ip.tot_len), ntohs(ip.id),
               ip.ttl, ip.protocol, ntohs(ip.check), src, dst);
        printf("  iov[1] UDP : sport=%u dport=%u len=%u check=0x%04x (%s)\n",
               ntohs(udp.source), ntohs(udp.dest), ntohs(udp.len), ntohs(udp.check),
               csum_ok ? "valid" : "INVALID");
        printf("  iov[2] DATA: \"%s\" (%zu bytes)\n", payload, plen);
        fflush(stdout);
        count--;
    }
    close(fd);
    return 0;
}
