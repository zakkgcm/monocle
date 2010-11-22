# monocle Makefile
# cheeseum 2010

# configure scripts are for little girls
include config.mk

SRC = monocle.c monocleview.c monoclethumbpane.c\
      utils/md5.c
OBJ = ${SRC:.c=.o}
W32OBJ = $(SRC:.c=-w32.o)

.PHONY: install

all: options monocle

options: 
	@echo "*** There are a few warnings but they are known and superficial, things should still work ***"
	@echo monocle options:
	@echo "prefix:  ${PREFIX}"
	@echo "CFLAGS:  ${CFLAGS}"
	@echo "LDFLAGS: ${LDFLAGS}"

%.o: %.c
	@echo ${CC} $<
	@${CC} -c ${CFLAGS} $< -o $@

#%-w32.o: %.c
#	@echo ${W32CC} $<
#	@${W32CC} -c ${W32CFLAGS} $< -o $@

monocle: options ${OBJ}
	@echo ${CC} ${OBJ} -o $@
	@${CC} ${LDFLAGS} ${OBJ} -o $@

#monocle.exe: ${W32OBJ}
#	@echo "*** WARNING: The resulting monocle.exe binary is defunct as of now, it will not run properly. ***"
#	@echo ${W32CC} ${W32OBJ} -o $@
#	@${W32CC} ${W32OBJ} ${W32LDFLAGS} -o $@

install: monocle
	@echo "installing monocle to ${INSTALLDIR}"
	[ -d "${INSTALLDIR}/bin" ] || install -d -m755 ${INSTALLDIR}/bin
	install -d ${INSTALLDIR}/share/monocle/
	install -m644 Itisamystery.gif ${INSTALLDIR}/share/monocle/
	install -m755 monocle ${INSTALLDIR}/bin
	
uninstall: monocle
	@echo "uninstalling monocle from ${INSTALLDIR}"
	rm -vf ${INSTALLDIR}/bin/monocle

clean: 
	@echo cleaning up...
	@rm -vf ${OBJ}
	@rm -vf ${W32OBJ}
	@rm -vf monocle
	@echo all clean
