/*
 * proto.h - shared helpers for the raw UDP proof of concept.
 *
 * We deliberately do NOT use <netinet/ip.h> / <netinet/udp.h> so students
 * see every single field of the headers they are building by hand.
 * All multi-byte fields are stored in NETWORK byte order (big endian).
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <stddef.h>
#include <sys/uio.h>

/* RFC 791 - IPv4 header without options (20 bytes). */
struct ipv4_hdr {
    uint8_t  ver_ihl;   /* high nibble: version (4), low nibble: IHL in 32-bit words (5) */
    uint8_t  tos;       /* type of service / DSCP+ECN */
    uint16_t tot_len;   /* whole datagram length: IP header + UDP header + payload */
    uint16_t id;        /* identification (used for fragmentation) */
    uint16_t frag_off;  /* flags (3 bit) + fragment offset (13 bit) */
    uint8_t  ttl;       /* time to live */
    uint8_t  protocol;  /* 17 = UDP */
    uint16_t check;     /* header checksum (IP header only) */
    uint32_t saddr;     /* source address */
    uint32_t daddr;     /* destination address */
} __attribute__((packed));

/* RFC 768 - UDP header (8 bytes). */
struct udp_hdr {
    uint16_t source;    /* source port */
    uint16_t dest;      /* destination port */
    uint16_t len;       /* UDP header + payload */
    uint16_t check;     /* checksum over pseudo header + UDP header + payload */
} __attribute__((packed));

/* Pseudo header that is prepended (only for the checksum) - RFC 768. */
struct pseudo_hdr {
    uint32_t saddr;
    uint32_t daddr;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t udp_len;
} __attribute__((packed));

/* The three iovec segments of a datagram - used by sender AND receiver. */
enum { SEG_IP = 0, SEG_UDP = 1, SEG_PAYLOAD = 2, SEG_COUNT = 3 };

/*
 * RFC 1071 internet checksum over a *vector* of buffers.
 * The one's complement sum is computed over 16-bit words; because a word
 * may straddle two iovec segments, we track the byte position parity.
 */
static inline uint16_t inet_csum_iov(const struct iovec *iov, int iovcnt)
{
    uint32_t sum = 0;
    size_t pos = 0;                         /* global byte position */

    for (int i = 0; i < iovcnt; i++) {
        const uint8_t *p = iov[i].iov_base;
        for (size_t j = 0; j < iov[i].iov_len; j++, pos++)
            sum += (pos & 1) ? p[j] : (uint32_t)p[j] << 8;
    }
    while (sum >> 16)                       /* fold carries back in */
        sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;                  /* host order result */
}

#endif
