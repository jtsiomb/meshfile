PREFIX = /usr/local

obj = meshconv.o
bin = meshconv

CFLAGS = $(warn) $(opt) $(dbg) -I../include $(dep)
LDFLAGS = ../libmeshfile.a -lm

include ../config.mk

$(bin): $(obj) ../libmeshfile.a
	$(CC) -o $@ $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(bin) $(obj)

.PHONY: install
install: $(bin)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(bin) $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(bin)
