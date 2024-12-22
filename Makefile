src = $(wildcard src/*.c) test.c
obj = $(src:.c=.o)
dep = $(src:.c=.d)
bin = test

CFLAGS = -pedantic -Wall -g -Iinclude -MMD
LDFLAGS = -lGL -lGLU -lglut -limago -lm

$(bin): $(obj)
	$(CC) -o $@ $(obj) $(LDFLAGS)

-include $(dep)

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: cleandep
cleandep:
	rm -f $(dep)
