obj = $(src:.c=.o)
dep = $(src:.c=.d)

somajor = 0
sominor = 1

liba = libmeshfile.a
#ldname = libmeshfile.so
#soname = $(ldname).$(somajor)
#libso = $(ldname).$(somajor).$(sominor)
#shared = -shared -Wl,-soname,$(soname)

CFLAGS = $(warn) $(opt) $(dbg) $(pic) -Iinclude $(depgen) $(CFLAGS_cfg)
LDFLAGS = $(LDFLAGS_cfg)

include config.mk

.PHONY: all
all: $(libso) $(liba)

$(libso): $(obj)
	$(CC) -o $@ $(shared) $(obj) $(LDFLAGS)

$(liba): $(obj)
	$(AR) rcs $@ $(obj)

.c.o:
	$(CC) -o $@ $(CFLAGS) -c $<

meshview/meshview: meshview/meshview.c $(libso)
	$(MAKE) -C meshview

meshconv/meshconv: meshconv/meshconv.c $(libso)
	$(MAKE) -C meshconv

config.mk: configure
	./configure

.PHONY: config
config: config.mk

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
	[ -n "$(soname)" ] && \
		rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(soname) && \
		rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(ldname) && \
		ln -s $(libso) $(DESTDIR)$(PREFIX)/$(libdir)/$(soname) && \
		ln -s $(soname) $(DESTDIR)$(PREFIX)/$(libdir)/$(ldname) || true

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/meshfile.h
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(liba)
	rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(libso)
	[ -n "$(soname)" ] && \
		rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(soname) && \
		rm -f $(DESTDIR)$(PREFIX)/$(libdir)/$(ldname) || true


.PHONY: tools
tools: $(libso) $(liba) meshview meshconv

.PHONY: clean-all
clean-all: clean clean-meshview clean-meshconv

.PHONY: install-all
install-all: install install-meshview install-meshconv

.PHONY: meshview
meshview: $(libso)
	cd meshview && $(MAKE)

.PHONY: clean-meshview
clean-meshview: $(libso)
	cd meshview && $(MAKE) clean

.PHONY: install-meshview
install-meshview: meshview/meshview
	cd meshview && $(MAKE) install


.PHONY: meshconv
meshconv: $(libso)
	cd meshconv && $(MAKE)

.PHONY: clean-meshconv
clean-meshconv: $(libso)
	cd meshconv && $(MAKE) clean

.PHONY: install-meshconv
install-meshconv: meshconv/meshconv
	cd meshconv && $(MAKE) install
