include config.mk
include version.mk

.SUFFIXES: .o .c

BIN = xitems
OBJ = $(BIN:=.o)
SRC = $(BIN:=.c)
MAN = $(BIN:=.1)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(PCCFLAGS) -o $@ $(OBJ) $(LDFLAGS)

$(OBJ): config.mk

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	install -m 644 $(MAN) $(DESTDIR)$(MANPREFIX)/man1

uninstall:
	cd $(DESTDIR)$(PREFIX)/bin && rm -f $(BIN)
	cd $(DESTDIR)$(MANPREFIX)/man1 && rm -f $(MAN)

clean:
	-rm -rf $(BIN) $(OBJ) xitems$(V) *.tar.gz *.core

dist: clean
	mkdir xitems$(V)
	cp $(SRC) $(MAN) README COPYING Makefile config.mk version.mk xitems$(V)
	tar cf - xitems$(V) | gzip >xitems$(V).tar.gz
	rm -rf xitems$(V)

tags: $(SRC)
	ctags $(SRC)

.PHONY: all install uninstall clean dist
