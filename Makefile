SHELL = /bin/sh
CC = gcc
OBJ = 64tass.o opcodes.o misc.o avl.o my_getopt.o eval.o error.o section.o encoding.o ternary.o file.o values.o variables.o mem.o isnprintf.o macro.o obj.o floatobj.o addressobj.o codeobj.o strobj.o listobj.o boolobj.o bytesobj.o intobj.o bitsobj.o functionobj.o instruction.o unicode.o unicodedata.o listing.o registerobj.o dictobj.o namespaceobj.o
LIBS = -lm
LANG = C
REVISION := $(shell svnversion | grep "^[1-9]" || echo "883?")
CFLAGS = -pipe -O2 -W -Wall -Wextra -Wwrite-strings -Wshadow -fstrict-aliasing -DREVISION="\"$(REVISION)\"" -g -Wstrict-aliasing=2 -Werror=missing-prototypes
LDFLAGS = -g
CFLAGS += $(LDFLAGS)
TARGET = 64tass
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

.SILENT:

all: $(TARGET) README

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LIBS)


README: README.html
	-sed -e 's/&larr;/<-/g;s/&hellip;/.../g;s/&lowast;/*/g;s/&minus;/-/g;s/&ndash;/-/g;' README.html | w3m -T text/html -dump -no-graph | sed -e 's/\s\+$$//' >README

64tass.o: 64tass.c 64tass.h inttypes.h opcodes.h error.h libtree.h obj.h \
 values.h misc.h eval.h section.h mem.h encoding.h file.h variables.h \
 macro.h instruction.h unicode.h unicodedata.h listing.h listobj.h \
 codeobj.h strobj.h floatobj.h addressobj.h boolobj.h bytesobj.h intobj.h \
 bitsobj.h functionobj.h namespaceobj.h
addressobj.o: addressobj.c obj.h inttypes.h values.h addressobj.h error.h \
 libtree.h boolobj.h strobj.h intobj.h
avl.o: avl.c libtree.h
bitsobj.o: bitsobj.c bitsobj.h obj.h inttypes.h values.h eval.h unicode.h \
 unicodedata.h encoding.h libtree.h boolobj.h floatobj.h codeobj.h \
 error.h strobj.h bytesobj.h intobj.h listobj.h
boolobj.o: boolobj.c obj.h inttypes.h values.h boolobj.h floatobj.h \
 strobj.h error.h libtree.h bytesobj.h bitsobj.h intobj.h
bytesobj.o: bytesobj.c bytesobj.h obj.h inttypes.h values.h eval.h \
 unicode.h unicodedata.h encoding.h libtree.h boolobj.h floatobj.h \
 codeobj.h intobj.h strobj.h bitsobj.h listobj.h error.h
codeobj.o: codeobj.c codeobj.h obj.h inttypes.h values.h eval.h mem.h \
 64tass.h opcodes.h section.h libtree.h variables.h error.h boolobj.h \
 floatobj.h namespaceobj.h listobj.h intobj.h bitsobj.h bytesobj.h
dictobj.o: dictobj.c dictobj.h obj.h inttypes.h values.h libtree.h eval.h \
 error.h intobj.h listobj.h strobj.h boolobj.h
encoding.o: encoding.c encoding.h libtree.h inttypes.h error.h obj.h \
 values.h ternary.h misc.h 64tass.h opcodes.h unicode.h unicodedata.h \
 strobj.h bytesobj.h
error.o: error.c error.h inttypes.h libtree.h obj.h values.h misc.h \
 file.h variables.h 64tass.h opcodes.h macro.h strobj.h unicode.h \
 unicodedata.h addressobj.h registerobj.h namespaceobj.h
eval.o: eval.c eval.h obj.h inttypes.h values.h file.h libtree.h \
 section.h mem.h encoding.h macro.h variables.h 64tass.h opcodes.h misc.h \
 unicode.h unicodedata.h listing.h error.h floatobj.h boolobj.h intobj.h \
 bitsobj.h strobj.h codeobj.h bytesobj.h addressobj.h listobj.h dictobj.h \
 registerobj.h namespaceobj.h
file.o: file.c file.h inttypes.h libtree.h misc.h 64tass.h opcodes.h \
 unicode.h unicodedata.h error.h obj.h values.h strobj.h
floatobj.o: floatobj.c obj.h inttypes.h values.h floatobj.h error.h \
 libtree.h boolobj.h codeobj.h strobj.h bytesobj.h intobj.h bitsobj.h
functionobj.o: functionobj.c isnprintf.h inttypes.h functionobj.h obj.h \
 values.h eval.h variables.h libtree.h floatobj.h strobj.h error.h \
 listobj.h intobj.h boolobj.h
instruction.o: instruction.c instruction.h inttypes.h opcodes.h obj.h \
 values.h 64tass.h misc.h section.h libtree.h mem.h file.h listing.h \
 error.h addressobj.h listobj.h registerobj.h codeobj.h
intobj.o: intobj.c obj.h inttypes.h values.h intobj.h unicode.h \
 unicodedata.h encoding.h libtree.h error.h boolobj.h floatobj.h \
 codeobj.h strobj.h bytesobj.h bitsobj.h
isnprintf.o: isnprintf.c isnprintf.h inttypes.h unicode.h unicodedata.h \
 eval.h obj.h values.h floatobj.h strobj.h intobj.h error.h libtree.h
listing.o: listing.c listing.h inttypes.h file.h libtree.h error.h obj.h \
 values.h 64tass.h opcodes.h unicode.h unicodedata.h misc.h section.h \
 mem.h instruction.h
listobj.o: listobj.c listobj.h obj.h inttypes.h values.h eval.h boolobj.h \
 error.h libtree.h codeobj.h strobj.h intobj.h
macro.o: macro.c macro.h inttypes.h misc.h file.h libtree.h eval.h obj.h \
 values.h section.h mem.h variables.h 64tass.h opcodes.h listing.h \
 error.h listobj.h namespaceobj.h
mem.o: mem.c mem.h inttypes.h error.h libtree.h obj.h values.h file.h \
 misc.h 64tass.h opcodes.h listing.h
misc.o: misc.c misc.h inttypes.h 64tass.h opcodes.h getopt.h my_getopt.h \
 section.h libtree.h mem.h encoding.h file.h eval.h obj.h values.h \
 variables.h ternary.h unicode.h unicodedata.h error.h codeobj.h
my_getopt.o: my_getopt.c my_getopt.h unicode.h inttypes.h unicodedata.h
namespaceobj.o: namespaceobj.c namespaceobj.h obj.h inttypes.h values.h \
 libtree.h eval.h intobj.h listobj.h error.h strobj.h variables.h
obj.o: obj.c variables.h libtree.h inttypes.h obj.h values.h misc.h \
 section.h mem.h 64tass.h opcodes.h eval.h error.h boolobj.h floatobj.h \
 strobj.h macro.h intobj.h listobj.h namespaceobj.h addressobj.h \
 codeobj.h registerobj.h bytesobj.h bitsobj.h functionobj.h dictobj.h
opcodes.o: opcodes.c opcodes.h
registerobj.o: registerobj.c obj.h inttypes.h values.h registerobj.h \
 error.h libtree.h boolobj.h strobj.h intobj.h
section.o: section.c unicode.h inttypes.h unicodedata.h section.h \
 libtree.h mem.h error.h obj.h values.h misc.h 64tass.h opcodes.h \
 intobj.h
strobj.o: strobj.c strobj.h obj.h inttypes.h values.h eval.h misc.h \
 unicode.h unicodedata.h error.h libtree.h boolobj.h floatobj.h \
 bytesobj.h intobj.h bitsobj.h listobj.h
ternary.o: ternary.c ternary.h unicode.h inttypes.h unicodedata.h error.h \
 libtree.h obj.h values.h
unicodedata.o: unicodedata.c unicodedata.h
unicode.o: unicode.c unicode.h inttypes.h unicodedata.h error.h libtree.h \
 obj.h values.h
values.o: values.c values.h inttypes.h unicode.h unicodedata.h error.h \
 libtree.h obj.h strobj.h variables.h
variables.o: variables.c unicode.h inttypes.h unicodedata.h variables.h \
 libtree.h obj.h values.h misc.h 64tass.h opcodes.h file.h boolobj.h \
 floatobj.h error.h namespaceobj.h strobj.h codeobj.h registerobj.h \
 functionobj.h listobj.h intobj.h bytesobj.h bitsobj.h dictobj.h \
 addressobj.h

.PHONY: all clean distclean install install-strip uninstall

clean:
	-rm -f $(OBJ)

distclean: clean
	-rm -f $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(BINDIR)/$(TARGET)

install-strip: $(TARGET)
	install -D -s $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	-rm $(BINDIR)/$(TARGET)
