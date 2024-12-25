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

libdir = lib
bin = test

CFLAGS = -pedantic -Wall -g -fPIC -Iinclude -MMD
LDFLAGS = -limago
test_LDFLAGS = $(liba) -lGL -lGLU -lglut -limago -lm

.PHONY: all
all: $(libso) $(liba) $(bin)

$(libso): $(obj)
	$(CC) -o $@ -shared -Wl,-soname,$(soname) $(LDFLAGS)

$(liba): $(obj)
	$(AR) rcs $@ $(obj)

$(bin): test.o $(liba)
	$(CC) -o $@ test.o $(test_LDFLAGS)

-include $(dep)

.PHONY: clean
clean:
	rm -f $(obj) test.o $(bin)

.PHONY: cleandep
cleandep:
	rm -f $(dep) test.d

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
