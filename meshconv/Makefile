PREFIX = /usr/local

obj = meshconv.o
bin = meshconv

CFLAGS = -I../include
LDFLAGS = ../libmeshfile.a

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
