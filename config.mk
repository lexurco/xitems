PREFIX ?= /usr/local
MANPREFIX ?= /usr/local/man

CFLAGS = -std=c99 -Wall -pedantic
CPPFLAGS = -I/usr/X11R6/include
LDFLAGS = -L/usr/X11R6/lib -lX11

# debug
#CFLAGS = -std=c99 -Wall -pedantic -Wextra -O0 -g3
