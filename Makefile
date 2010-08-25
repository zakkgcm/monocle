# monocle Makefile
# cheeseum 2010

# configure scripts are for little girls
include config.mk

SRC = monocle.c monocleview.c monoclethumbpane.c
OBJ = ${SRC:.c=.o}

all: options monocle

options: 
	@echo monocle options:
	@echo "prefix:  ${PREFIX}"
	@echo "CFLAGS:  ${CFLAGS}"
	@echo "LDFLAGS: ${LDFLAGS}"

%.o: %.c
	@echo ${CC} $<
	@${CC} -c ${CFLAGS} $< -o $@

monocle: ${OBJ}
	@echo ${CC} ${OBJ} -o $@
	@${CC} ${LDFLAGS} ${OBJ} -o $@

clean: 
	@echo cleaning up...
	@rm -vf ${OBJ}
	@echo all clean
