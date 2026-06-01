BINS = somebar

PREFIX ?= /usr/local
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -g

PROTOCOL_HEADERS = xdg-shell-protocol.h xdg-output-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h
PROTOCOL_SOURCES = xdg-shell-protocol.c xdg-output-unstable-v1-protocol.c wlr-layer-shell-unstable-v1-protocol.c
PROTOCOL_OBJS = xdg-shell-protocol.o xdg-output-unstable-v1-protocol.o wlr-layer-shell-unstable-v1-protocol.o

all: $(BINS)

config.h:
	cp config.def.h $@

clean:
	$(RM) $(BINS) $(addsuffix .o,$(BINS)) $(PROTOCOL_OBJS) $(PROTOCOL_HEADERS) $(PROTOCOL_SOURCES) config.h

uninstall:
	$(RM) $(PREFIX)/bin/$(BINS)

install: all
	install -D -t $(PREFIX)/bin $(BINS)

WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.o: xdg-shell-protocol.h

xdg-output-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
xdg-output-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@
xdg-output-unstable-v1-protocol.o: xdg-output-unstable-v1-protocol.h

wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.o: wlr-layer-shell-unstable-v1-protocol.h

somebar.o: somebar.c utf8.h config.h xdg-shell-protocol.h xdg-output-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

somebar: $(PROTOCOL_OBJS) somebar.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

somebar: CFLAGS+=$(shell pkg-config --cflags wayland-client wayland-cursor fcft pixman-1)
somebar: LDLIBS+=$(shell pkg-config --libs wayland-client wayland-cursor fcft pixman-1) -lrt -lcjson

.PHONY: all clean install uninstall
