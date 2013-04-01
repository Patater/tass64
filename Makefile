CC = gcc
OBJ = 64tass.o opcodes.o misc.o avl.o my_getopt.o eval.o error.o section.o encoding.o ternary.o file.o values.o variables.o mem.o isnprintf.o macro.o
LANG = C
REVISION := $(shell svnversion)
CFLAGS = -O2 -W -Wall -Wextra -Wwrite-strings -Wshadow -fstrict-aliasing -DREVISION="\" $(REVISION)\"" -g -Wstrict-aliasing=2
LDFLAGS = -g -lm
CFLAGS += $(LDFLAGS)

.SILENT:

all: 64tass README

64tass: $(OBJ)

64tass.o: 64tass.c opcodes.h misc.h inttypes.h eval.h values.h error.h \
 section.h libtree.h encoding.h file.h variables.h mem.h macro.h
avl.o: avl.c libtree.h
encoding.o: encoding.c encoding.h libtree.h inttypes.h error.h ternary.h \
 misc.h
error.o: error.c error.h inttypes.h misc.h values.h
eval.o: eval.c eval.h values.h inttypes.h error.h file.h libtree.h \
 section.h encoding.h mem.h isnprintf.h macro.h variables.h misc.h
file.o: file.c file.h inttypes.h libtree.h error.h values.h misc.h
isnprintf.o: isnprintf.c isnprintf.h inttypes.h misc.h error.h eval.h \
 values.h
macro.o: macro.c macro.h inttypes.h misc.h error.h file.h libtree.h \
 eval.h values.h section.h variables.h
mem.o: mem.c mem.h inttypes.h error.h file.h libtree.h misc.h
misc.o: misc.c misc.h inttypes.h opcodes.h getopt.h my_getopt.h error.h \
 section.h libtree.h encoding.h file.h eval.h values.h variables.h
my_getopt.o: my_getopt.c my_getopt.h
opcodes.o: opcodes.c opcodes.h
section.o: section.c section.h libtree.h inttypes.h error.h misc.h
ternary.o: ternary.c ternary.h misc.h inttypes.h
values.o: values.c error.h inttypes.h variables.h libtree.h misc.h \
 values.h
variables.o: variables.c variables.h libtree.h inttypes.h error.h misc.h \
 values.h

README: README.html
	-w3m -dump -no-graph README.html | sed -e 's/\s\+$$//' >README

.PHONY: clean

clean:
	rm -f $(OBJ) 64tass *~
