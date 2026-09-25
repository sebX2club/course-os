# UDP über einen Raw Socket – Proof of Concept

Dieses Beispiel zeigt, wie ein UDP-Datagramm **ohne den UDP-Stack des Kernels** verschickt wird.
Wir bauen IPv4-Header und UDP-Header selbst, berechnen die Prüfsumme selbst und übergeben dem
Kernel die Teile mit **Vectored I/O** (`sendmsg`/`recvmsg` mit `struct iovec`) – ein Segment pro
Protokollschicht.

Auf der Gegenseite lauscht ein ganz normaler UDP-Socket (`nc`). Kommt die Nachricht dort an,
waren unsere handgebauten Header korrekt.

## Aufbau

```
 Container "sender" (10.89.42.20)              Container "receiver" (10.89.42.10)
 ┌──────────────────────────────┐              ┌───────────────────────────────────┐
 │ raw_udp_send                 │              │ nc -u -l -p 9999                  │
 │   sendmsg(iov[3])            │── podman ──▶ │   normaler UDP-Socket             │
 │   SOCK_RAW / IPPROTO_RAW     │   Netzwerk   │ raw_udp_recv 9999                 │
 └──────────────────────────────┘   "rawudp"   │   recvmsg(iov[3])                 │
                                               │   SOCK_RAW / IPPROTO_UDP          │
                                               └───────────────────────────────────┘
```

| Datei            | Inhalt                                                                          |
|------------------|---------------------------------------------------------------------------------|
| `proto.h`        | IPv4-, UDP- und Pseudo-Header als `struct`, Prüfsumme über ein iovec-Array      |
| `raw_udp_send.c` | Baut die Header und sendet mit `sendmsg` in drei Segmenten                      |
| `raw_udp_recv.c` | Empfängt mit `recvmsg` direkt in die Header-Structs, prüft die UDP-Prüfsumme    |
| `Containerfile`  | Kompiliert beide Programme in einem Alpine-Image                                |
| `demo.sh`        | Baut das Image, legt das Netzwerk an, startet Empfänger und Sender              |

## Voraussetzungen

- Podman (unter macOS/Windows mit laufender `podman machine`)
- Raw Sockets sind Linux-spezifisch implementiert – der Code läuft **im Container**, nicht direkt
  auf macOS.

## Ausführen

```sh
./demo.sh                        # Standardnachricht
./demo.sh "eigene Nachricht"
```

Erwartete Ausgabe:

```
[raw-send] 10.89.42.20:40000 -> 10.89.42.10:9999  62 bytes on the wire (iov: ip=20 udp=8 payload=34)  udp.check=0xb49b
== receiver output
[raw-recv] sniffing all UDP, filtering dst port 9999
[raw-recv] 62 bytes scattered into iov[0..2]
  iov[0] IPv4: ver=4 ihl=5 tot_len=62 id=0x4242 ttl=64 proto=17 check=0x8f9d 10.89.42.20 -> 10.89.42.10
  iov[1] UDP : sport=40000 dport=9999 len=42 check=0xb49b (valid)
  iov[2] DATA: "Hello from a hand-built UDP packet" (34 bytes)
[nc -u -l -p 9999] received: Hello from a hand-built UDP packet
```

Aufräumen des Netzwerks danach: `podman network rm rawudp`

## Was passiert im Detail

### 1. Raw Socket öffnen

```c
socket(AF_INET, SOCK_RAW, IPPROTO_RAW);   /* Sender   */
socket(AF_INET, SOCK_RAW, IPPROTO_UDP);   /* Empfänger */
```

- `IPPROTO_RAW` impliziert `IP_HDRINCL`: Wir liefern den **IP-Header selbst** mit.
- Ein `SOCK_RAW`/`IPPROTO_UDP`-Socket bekommt eine Kopie **jedes** eingehenden UDP-Pakets
  inklusive IP-Header – er ist an keinen Port gebunden.
- Raw Sockets brauchen die Capability `CAP_NET_RAW`, daher `--cap-add=NET_RAW` in `demo.sh`.

### 2. Header bauen

Alle 16-/32-Bit-Felder stehen im Paket in **Network Byte Order** (Big Endian) → `htons()`/`ntohs()`.

```
IPv4 (20 Byte): ver/ihl | tos | tot_len | id | flags/frag | ttl | proto=17 | check | saddr | daddr
UDP  ( 8 Byte): sport | dport | len | check
```

Laut `man 7 raw` füllt Linux bei `IP_HDRINCL` einige Felder selbst:
IP-Prüfsumme und `tot_len` **immer**, `saddr` und `id`, wenn sie 0 sind.
Die **UDP-Prüfsumme** dagegen ist unsere Aufgabe.

### 3. UDP-Prüfsumme

Die Prüfsumme (RFC 768 / RFC 1071) läuft über einen **Pseudo-Header** (Quell-IP, Ziel-IP, Protokoll,
UDP-Länge), den UDP-Header und die Payload. Der Pseudo-Header wird nie verschickt – er bindet die
Prüfsumme nur an die IP-Adressen.

`inet_csum_iov()` in `proto.h` rechnet direkt über ein iovec-Array, die Teile werden also nie in
einen gemeinsamen Puffer kopiert. Ein 16-Bit-Wort kann dabei über eine Segmentgrenze reichen;
deshalb zählt `pos` die Byte-Position über alle Segmente hinweg.

### 4. Vectored I/O: Gather beim Senden, Scatter beim Empfangen

```c
struct iovec iov[3] = {
    { &ip,  sizeof(ip)  },    /* iov[0]: IPv4-Header */
    { &udp, sizeof(udp) },    /* iov[1]: UDP-Header  */
    { msg,  msg_len     },    /* iov[2]: Payload     */
};
sendmsg(fd, &mh, 0);          /* Kernel setzt die Teile zu einem Paket zusammen */
```

Jede Schicht besitzt ihren eigenen Puffer – so arbeiten auch echte Netzwerk-Stacks mit Headern.
Beim Empfang verteilt `recvmsg` das Paket in dieselben drei Segmente: Die Header landen direkt in
den Structs, ohne Parsen per Pointer-Arithmetik.

**Grenze:** Scatter funktioniert nur mit festen Größen. Hat ein Paket IP-Optionen (IHL > 5), beginnt
der UDP-Header nicht bei Byte 20 – `raw_udp_recv` überspringt solche Pakete.

### 5. Was der Empfänger selbst prüfen muss

Beim normalen UDP-Socket erledigt der Kernel Portzuordnung, Längen- und Prüfsummenprüfung.
Beim Raw Socket müssen wir das selbst tun:

- Paket kürzer als 28 Byte → verwerfen (sonst läuft die Längenberechnung ins Negative)
- IP-Optionen vorhanden → verwerfen (siehe oben)
- falscher Zielport → verwerfen
- UDP-Prüfsumme nachrechnen

### 6. Warum `nc` der eigentliche Beweis ist

Der UDP-Stack des Kernels verwirft Datagramme mit falscher Prüfsumme oder Länge **still**, bevor sie
einen normalen Socket erreichen. Der Raw-Empfänger sieht auch kaputte Pakete – `nc` nicht.

## Übungsaufgaben

1. **Prüfsumme kaputt machen:** Setze in `raw_udp_send.c` `udp.check` auf einen falschen Wert.
   Was zeigt `raw_udp_recv`, was zeigt `nc`? Warum?
2. **Prüfsumme weglassen:** Setze `udp.check = 0`. Was sagt RFC 768 dazu, und kommt das Paket an?
3. **`writev` statt `sendmsg`:** Verbinde den Raw Socket mit `connect()` und sende mit `writev()`.
   Was ändert sich, was bleibt gleich?
4. **Kernel baut den IP-Header:** Öffne den Sender mit `IPPROTO_UDP` statt `IPPROTO_RAW` und ohne
   `IP_HDRINCL`. Welches iovec-Segment fällt weg?
5. **Mitschneiden:** Starte im Empfänger-Container `tcpdump -X -i eth0 udp port 9999`
   (`apk add tcpdump`) und vergleiche die Bytes mit der Ausgabe von `raw_udp_recv`.

## Weiterführend

- `man 7 raw`, `man 7 ip`, `man 7 udp`, `man 2 sendmsg`, `man 2 readv`
- RFC 768 (UDP), RFC 791 (IPv4), RFC 1071 (Internet Checksum)
