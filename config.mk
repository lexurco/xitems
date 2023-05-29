PREFIX ?= /usr/local
MANPREFIX ?= /usr/local/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = $(X11INC)/freetype2

CFLAGS = -std=c99 -Wall -pedantic
CPPFLAGS = -I$(X11INC) -I$(FREETYPEINC)
LDFLAGS = -L$(X11LIB) -lX11 $(FREETYPELIBS)

# debug
#CFLAGS = -std=c99 -Wall -pedantic -Wextra -O0 -g3
