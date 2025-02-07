include ../config.mk

obj = meshview.o

bin = meshview

CFLAGS = $(warn) $(opt) $(dbg) -I../include -I$(PREFIX)/include $(sysincdirs)
LDFLAGS = -L.. -L$(PREFIX)/$(libdir) -lmeshfile -lglut -lX11 -lXmu -lGL -lGLU -limago -lm

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
