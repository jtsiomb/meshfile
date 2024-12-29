PREFIX = /usr/local

obj = meshview.o

bin = meshview

CFLAGS = -g -I../include
LDFLAGS = -L.. -lmeshfile -lGL -lGLU -lglut -limago -lm

$(bin): $(obj) ../libmeshfile.a
	$(CC) -o $(bin) $(obj) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: install
install: $(bin)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(bin) $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(bin)
