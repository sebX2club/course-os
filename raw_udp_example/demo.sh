#!/usr/bin/env bash
# Raw-socket UDP demo with podman.
#
#   rawudp-sender (10.89.42.20)                  rawudp-receiver (10.89.42.10)
#   raw_udp_send --sendmsg(iov[3])--> network --> nc -u -l     (plain UDP socket)
#                                             \-> raw_udp_recv (SOCK_RAW, recvmsg(iov[3]))
#
# nc is the correctness proof: the kernel drops UDP datagrams with a bad
# checksum/length before they reach a normal socket.
set -euo pipefail
cd "$(dirname "$0")"

IMG=localhost/raw-udp-poc
NET=rawudp
RX_IP=10.89.42.10
TX_IP=10.89.42.20
PORT=9999
MSG="${1:-Hello from a hand-built UDP packet}"

cleanup() { podman rm -f rawudp-receiver >/dev/null 2>&1 || true; }
trap cleanup EXIT

echo "== build image"
podman build -q -t "$IMG" . >/dev/null

echo "== create network $NET (10.89.42.0/24)"
podman network exists "$NET" || podman network create --subnet 10.89.42.0/24 "$NET" >/dev/null

echo "== start receiver container ($RX_IP): nc -u -l + raw socket"
cleanup
# nc keeps listening forever, so stop it once raw_udp_recv has seen the packet.
podman run -d --name rawudp-receiver --network "$NET" --ip "$RX_IP" \
    --cap-add=NET_RAW "$IMG" \
    sh -c "nc -u -l -p $PORT > /tmp/nc.out 2>/dev/null & NC=\$!
           raw_udp_recv $PORT; sleep 1; kill \$NC
           echo \"[nc -u -l -p $PORT] received: \$(cat /tmp/nc.out)\"" >/dev/null
sleep 1

echo "== send from sender container ($TX_IP) via raw socket"
podman run --rm --network "$NET" --ip "$TX_IP" --cap-add=NET_RAW "$IMG" \
    raw_udp_send "$TX_IP" "$RX_IP" 40000 "$PORT" "$MSG"

podman wait rawudp-receiver >/dev/null
echo "== receiver output"
podman logs rawudp-receiver
