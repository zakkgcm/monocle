# monocle Makefile
# cheeseum 2011

# configure scripts are for little girls
include config.mk

SRC = monocle.c monocleview.c monoclethumbpane.c\
      utils/md5.c
HEAD = $(wildcard *.h)
OBJ = ${SRC:.c=.o}
W32OBJ = $(SRC:.c=-w32.o)

.PHONY: install

all: options monocle

options: 
	@echo monocle options:
	@echo "prefix:  ${PREFIX}"
	@echo "CFLAGS:  ${CFLAGS}"
	@echo "LDFLAGS: ${LDFLAGS}"

%.o: %.c ${HEAD}
	@echo ${CC} $<
	@${CC} -c ${CFLAGS} $< -o $@

%-w32.o: %.c ${HEAD}
	@echo ${W32CC} $<
	@${W32CC} -c ${W32CFLAGS} $< -o $@

monocle: options  ${OBJ}
	@echo ${CC} ${OBJ} -o $@
	@${CC} ${LDFLAGS} ${OBJ} -o $@

monocle.exe: ${W32OBJ}
	@echo ${W32CC} ${W32OBJ} -o $@
	@${W32CC} ${W32OBJ} ${W32LDFLAGS} -o $@
	@echo "stripping binary (don't peek)"
	strip -s $@

install: monocle
	@echo "installing monocle to ${INSTALLDIR}"
	[ -d "${INSTALLDIR}/bin" ] || install -d -m755 ${INSTALLDIR}/bin
	install -d ${INSTALLDIR}/share/monocle/
	install -m644 Itisamystery.gif ${INSTALLDIR}/share/monocle/
	install -m755 monocle ${INSTALLDIR}/bin
	strip -s ${INSTALLDIR}/bin/monocle
	
uninstall: monocle
	@echo "uninstalling monocle from ${INSTALLDIR}"
	rm -vf ${INSTALLDIR}/bin/monocle

clean: 
	@echo cleaning up...
	@rm -vf ${OBJ}
	@rm -vf ${W32OBJ}
	@rm -vf monocle
	@rm -vf monocle.exe
	@echo all clean
