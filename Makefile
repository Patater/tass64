CC = clang
OBJ = 64tass.o opcodes.o misc.o avl.o my_getopt.o eval.o error.o section.o encoding.o ternary.o file.o values.o variables.o mem.o isnprintf.o macro.o obj.o numobj.o floatobj.o addressobj.o codeobj.o strobj.o listobj.o boolobj.o sintobj.o uintobj.o
LIBS = -lm
LANG = C
REVISION := $(shell svnversion)
CFLAGS = -O2 -W -Wall -Wextra -Wwrite-strings -Wshadow -fstrict-aliasing -DREVISION="\" $(REVISION)\"" -g -Wstrict-aliasing=2
LDFLAGS = -g
CFLAGS += $(LDFLAGS)

.SILENT:

all: 64tass README

64tass: $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

64tass.o: 64tass.c opcodes.h misc.h inttypes.h eval.h values.h error.h \
 libtree.h obj.h section.h encoding.h file.h variables.h mem.h macro.h \
 listobj.h codeobj.h numobj.h strobj.h floatobj.h addressobj.h uintobj.h \
 sintobj.h boolobj.h
addressobj.o: addressobj.c obj.h inttypes.h values.h error.h libtree.h \
 addressobj.h boolobj.h strobj.h numobj.h sintobj.h uintobj.h
avl.o: avl.c libtree.h
boolobj.o: boolobj.c obj.h inttypes.h values.h error.h libtree.h \
 boolobj.h sintobj.h uintobj.h
codeobj.o: codeobj.c obj.h inttypes.h values.h error.h libtree.h \
 codeobj.h misc.h eval.h mem.h strobj.h numobj.h addressobj.h sintobj.h \
 uintobj.h listobj.h boolobj.h
encoding.o: encoding.c encoding.h libtree.h inttypes.h error.h ternary.h \
 misc.h
error.o: error.c error.h inttypes.h libtree.h misc.h values.h obj.h \
 file.h variables.h listobj.h
eval.o: eval.c eval.h values.h inttypes.h error.h libtree.h obj.h file.h \
 section.h encoding.h mem.h isnprintf.h macro.h variables.h misc.h \
 listobj.h numobj.h floatobj.h strobj.h boolobj.h codeobj.h addressobj.h \
 uintobj.h sintobj.h
file.o: file.c file.h inttypes.h libtree.h error.h values.h obj.h misc.h
floatobj.o: floatobj.c obj.h inttypes.h values.h error.h libtree.h \
 floatobj.h strobj.h numobj.h addressobj.h boolobj.h sintobj.h uintobj.h
isnprintf.o: isnprintf.c isnprintf.h inttypes.h misc.h error.h libtree.h \
 eval.h values.h obj.h numobj.h strobj.h
listobj.o: listobj.c obj.h inttypes.h values.h error.h libtree.h \
 listobj.h eval.h isnprintf.h boolobj.h
macro.o: macro.c macro.h inttypes.h misc.h error.h libtree.h file.h \
 eval.h values.h obj.h section.h variables.h listobj.h
mem.o: mem.c mem.h inttypes.h error.h libtree.h file.h misc.h
misc.o: misc.c misc.h inttypes.h opcodes.h getopt.h my_getopt.h error.h \
 libtree.h section.h encoding.h file.h eval.h values.h obj.h variables.h \
 ternary.h listobj.h codeobj.h
my_getopt.o: my_getopt.c my_getopt.h
numobj.o: numobj.c obj.h inttypes.h numobj.h eval.h values.h error.h \
 libtree.h strobj.h addressobj.h boolobj.h uintobj.h sintobj.h
obj.o: obj.c obj.h inttypes.h values.h error.h libtree.h variables.h \
 misc.h section.h listobj.h boolobj.h numobj.h uintobj.h sintobj.h \
 addressobj.h codeobj.h floatobj.h strobj.h
opcodes.o: opcodes.c opcodes.h
section.o: section.c section.h libtree.h inttypes.h error.h misc.h
sintobj.o: sintobj.c obj.h inttypes.h values.h error.h libtree.h \
 sintobj.h numobj.h strobj.h boolobj.h uintobj.h floatobj.h addressobj.h
strobj.o: strobj.c obj.h inttypes.h values.h error.h libtree.h strobj.h \
 eval.h misc.h isnprintf.h numobj.h uintobj.h boolobj.h sintobj.h
ternary.o: ternary.c ternary.h misc.h inttypes.h error.h libtree.h
uintobj.o: uintobj.c obj.h inttypes.h values.h error.h libtree.h \
 uintobj.h numobj.h strobj.h addressobj.h boolobj.h sintobj.h floatobj.h
values.o: values.c error.h inttypes.h libtree.h variables.h misc.h \
 values.h obj.h boolobj.h listobj.h strobj.h uintobj.h sintobj.h
variables.o: variables.c variables.h libtree.h inttypes.h error.h misc.h \
 values.h obj.h file.h listobj.h

README: README.html
	-w3m -dump -no-graph README.html | sed -e 's/\s\+$$//' >README

.PHONY: clean

clean:
	rm -f $(OBJ) 64tass *~
