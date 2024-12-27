PREFIX = /usr/local

src = $(wildcard src/*.c)
obj = $(src:.c=.o)
dep = $(src:.c=.d)

somajor = 0
sominor = 1

liba = libmeshfile.a
ldname = libmeshfile.so
soname = $(ldname).$(somajor)
libso = $(ldname).$(somajor).$(sominor)
shared = -shared -Wl,-soname,$(soname)

libdir = lib

CFLAGS = -pedantic -Wall -g -fPIC -Iinclude -MMD


.PHONY: all
all: $(libso) $(liba) meshview/meshview

$(libso): $(obj)
	$(CC) -o $@ $(shared) $(LDFLAGS)

$(liba): $(obj)
	$(AR) rcs $@ $(obj)

meshview/meshview: meshview/meshview.c $(libso)
	$(MAKE) -C meshview

-include $(dep)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: cleandep
cleandep:
	rm -f $(dep)

.PHONY: install
install: $(liba)
	mkdir -p $(DESTDIR)$(PREFIX)/include
	mkdir -p $(DESTDIR)$(PREFIX)/$(libdir)
	cp include/meshfile.h $(DESTDIR)$(PREFIX)/include/meshfile.h
	cp $(liba) $(DESTDIR)$(PREFIX)/$(libdir)/$(liba)
	cp $(libso) $(DESTDIR)$(PREFIX)/$(libdir)/$(libso)
	ln -s $(libso) $(DESTDIR)$(PREFIX)/$(libdir)/$(soname)
	ln -s $(soname) $(DESTDIR)$(PREFIX)/$(libdir)/$(ldname)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/meshfile.h
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(liba)
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(libso)
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(soname)
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(ldname)


.PHONY: meshview
meshview: $(libso)
	$(MAKE) -C meshview

.PHONY: clean-meshview
clean-meshview: $(libso)
	$(MAKE) -C meshview clean
