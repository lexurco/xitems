include version.mk

BIN = xitems
OBJ = $(BIN:=.o)
SRC = $(BIN:=.c)
MAN = $(BIN:=.1)

PREFIX ?= $(DESTDIR)/usr/local
MANPREFIX ?= $(PREFIX)/man
X11BASE ?= /usr/X11R6
X11INC ?= $(X11BASE)/include
X11LIB ?= $(X11BASE)/lib
FREETYPEINC ?= $(X11INC)/freetype2
FREETYPELIBS ?= -lfontconfig -lXft

INCS = -I$(X11INC) -I$(FREETYPEINC)
LIBS = -L$(X11LIB) -lX11 $(FREETYPELIBS)

bindir = $(PREFIX)/bin
man1dir = $(MANPREFIX)/man1

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LIBS) $(LDFLAGS)

.c.o:
	$(CC) -std=c99 -pedantic $(INCS) $(CFLAGS) $(CPPFLAGS) -c $<

install: all
	mkdir -p $(bindir)
	install -m 755 $(BIN) $(bindir)
	mkdir -p $(man1dir)
	install -m 644 $(MAN) $(man1dir)

uninstall:
	cd $(bindir) && rm -f $(BIN)
	cd $(man1dir) && rm -f $(MAN)

clean:
	-rm -rf $(BIN) $(OBJ) xitems$(V) *.tar.gz *.core

dist: clean
	mkdir xitems$(V)
	cp $(SRC) $(MAN) README COPYING CHANGES Makefile version.mk \
	    xitems$(V)
	tar cf - xitems$(V) | gzip >xitems$(V).tar.gz
	rm -rf xitems$(V)

tags: $(SRC)
	ctags $(SRC)

.PHONY: all install uninstall clean dist
