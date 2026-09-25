/*
 * raw_udp_send.c - send one UDP datagram through a RAW socket.
 *
 * The kernel's UDP layer is bypassed completely: we build the IPv4 header
 * and the UDP header ourselves and hand the kernel three separate buffers
 * using vectored I/O (sendmsg + struct iovec):
 *
 *     iov[0] ---> struct ipv4_hdr   (20 bytes)
 *     iov[1] ---> struct udp_hdr    ( 8 bytes)
 *     iov[2] ---> payload           ( n bytes)
 *
 * The kernel "gathers" the segments into one contiguous packet. No memcpy
 * into a big packet buffer is needed - every protocol layer owns its own
 * buffer, which is exactly how real network stacks treat headers.
 *
 * usage: raw_udp_send <src-ip> <dst-ip> <src-port> <dst-port> <message>
 * needs: CAP_NET_RAW (root, or podman --cap-add=NET_RAW)
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
    if (argc != 6) {
        fprintf(stderr, "usage: %s <src-ip> <dst-ip> <src-port> <dst-port> <message>\n", argv[0]);
        return 1;
    }
    const char *msg = argv[5];
    size_t msg_len = strlen(msg);

    /*
     * 1. Raw socket. IPPROTO_RAW implies IP_HDRINCL: we supply the IP header.
     *    (With SOCK_RAW + IPPROTO_UDP the kernel would still build the IP
     *    header for us - we want to see everything, so IPPROTO_RAW.)
     */
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (fd < 0) {
        perror("socket(AF_INET, SOCK_RAW, IPPROTO_RAW)");
        return 1;
    }

    struct sockaddr_in dst = { .sin_family = AF_INET };
    struct ipv4_hdr ip = { 0 };
    struct udp_hdr udp = { 0 };

    if (inet_pton(AF_INET, argv[1], &ip.saddr) != 1 ||
        inet_pton(AF_INET, argv[2], &ip.daddr) != 1) {
        fprintf(stderr, "invalid IPv4 address\n");
        return 1;
    }
    dst.sin_addr.s_addr = ip.daddr;   /* kernel uses this for routing only */

    /* 2. UDP header - every 16-bit field converted to network byte order. */
    udp.source = htons((uint16_t)atoi(argv[3]));
    udp.dest   = htons((uint16_t)atoi(argv[4]));
    udp.len    = htons((uint16_t)(sizeof(udp) + msg_len));
    udp.check  = 0;                   /* must be 0 while computing */

    /* 3. IPv4 header. */
    ip.ver_ihl  = (4 << 4) | (sizeof(ip) / 4);    /* version 4, IHL 5 */
    ip.tos      = 0;
    ip.tot_len  = htons((uint16_t)(sizeof(ip) + sizeof(udp) + msg_len));
    ip.id       = htons(0x4242);
    ip.frag_off = htons(0x4000);                  /* DF bit, no fragments */
    ip.ttl      = 64;
    ip.protocol = IPPROTO_UDP;                    /* 17 */
    ip.check    = 0;  /* raw(7): Linux ALWAYS fills in the IP checksum */

    /*
     * 4. UDP checksum. It covers a pseudo header + UDP header + payload.
     *    Vectored I/O pays off again: the checksum routine walks an iovec
     *    array, so we never have to copy the pieces into one buffer.
     */
    struct pseudo_hdr ph = {
        .saddr = ip.saddr, .daddr = ip.daddr,
        .zero = 0, .protocol = IPPROTO_UDP, .udp_len = udp.len,
    };
    struct iovec csum_iov[] = {
        { &ph,         sizeof(ph)  },
        { &udp,        sizeof(udp) },
        { (void *)msg, msg_len     },
    };
    uint16_t c = inet_csum_iov(csum_iov, 3);
    udp.check = htons(c == 0 ? 0xffff : c);   /* RFC 768: 0 means "no checksum" */

    /* 5. The actual send: one iovec segment per protocol layer. */
    struct iovec iov[SEG_COUNT] = {
        [SEG_IP]      = { &ip,         sizeof(ip)  },
        [SEG_UDP]     = { &udp,        sizeof(udp) },
        [SEG_PAYLOAD] = { (void *)msg, msg_len     },
    };
    struct msghdr mh = {
        .msg_name    = &dst,
        .msg_namelen = sizeof(dst),
        .msg_iov     = iov,
        .msg_iovlen  = SEG_COUNT,
    };

    ssize_t n = sendmsg(fd, &mh, 0);
    if (n < 0) {
        perror("sendmsg");
        return 1;
    }

    printf("[raw-send] %s:%s -> %s:%s  %zd bytes on the wire "
           "(iov: ip=%zu udp=%zu payload=%zu)  udp.check=0x%04x\n",
           argv[1], argv[3], argv[2], argv[4], n,
           iov[SEG_IP].iov_len, iov[SEG_UDP].iov_len, iov[SEG_PAYLOAD].iov_len,
           ntohs(udp.check));
    close(fd);
    return 0;
}
