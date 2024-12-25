PREFIX = /usr/local

src = $(wildcard src/*.c)
obj = $(src:.c=.o)
dep = $(src:.c=.d)

liba = libmeshfile.a
libdir = lib
bin = test

CFLAGS = -pedantic -Wall -g -Iinclude -MMD
test_LDFLAGS = $(liba) -lGL -lGLU -lglut -limago -lm

.PHONY: all
all: $(liba) $(bin)

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

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/meshfile.h
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(liba)
