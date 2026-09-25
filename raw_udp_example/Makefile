CC      ?= cc
CFLAGS  ?= -Wall -Wextra -O2 -std=c11 -D_DEFAULT_SOURCE
BINS    := raw_udp_send raw_udp_recv

all: $(BINS)

%: %.c proto.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

.PHONY: all clean
