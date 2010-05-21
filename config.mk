# Makefile config for monocle
# cheeseum 2010

PROGNAME = monocle
VERSION  = 0.2

PREFIX  = /usr

CC		= cc

GTK_CFLAGS  = $(shell pkg-config --cflags gtk+-2.0)
GTK_LDFLAGS = $(shell pkg-config --libs gtk+-2.0)
CFLAGS	= -O3 -march=native -ggdb -ggdb3 -lgthread-2.0 -pedantic -Wall -DVERSION=\"${VERSION}\"  -DPROGNAME=\"${PROGNAME}\" ${GTK_CFLAGS}
LDFLAGS = -pthread $(GTK_LDFLAGS)
