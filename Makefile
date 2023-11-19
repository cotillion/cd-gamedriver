.DEFAULT_GOAL := all

# Set MUD_LIB to the directory which contains the mud data. Was formerly
# defined in config.h !
MUD_LIB = /home/mud/genesis/lib

# Set BINDIR to the directory where you want to install the executables.
BINDIR = /home/mud/genesis/bin

# Specify the interface source files on the ISRC line, and the corresponding
# object files on the IOBJ line.
# NB. interface.c should be edited accordingly.
ISRC= clib/efun.c clib/stdobject.c clib/gl_language.c
IOBJ= clib/efun.o clib/stdobject.o clib/gl_language.o

#Enable warnings from the compiler, if wanted.
WARN=-Wall -Wformat=2

#
# Enable run time debugging. It will use more time and space.
# When the flag is changed, be sure to recompile everything.
# Simply comment out this line if not wanted.
# If you change anything in the source, you are strongly encouraged to have
# DEBUG defined.
# If you will not change anything, you are still strongly to have it defined,
# as long as the game is not bug free.
DEBUG=
#DEBUG=-DDEBUG
#
#

SYS_CFLAGS=-pipe
SYS_OPT=-std=gnu99 -O3 -fgnu89-inline -ggdb
SYS_LIBS=-lcrypt
CC=gcc

CFLAGS=  $(SYS_CFLAGS) $(SYS_OPT) $(WARN) $(DEBUG)

#
# Add extra libraries here.
#
LIBS= -lm $(SYS_LIBS)
MFLAGS = "BINDIR=$(BINDIR)" "MUD_LIB=$(MUD_LIB)"

CFLAGS += $(shell pkg-config --cflags json-c)
LIBS += $(shell pkg-config --libs json-c)

.PHONY: all
all: driver

install.utils:
	make -C util install

.PHONY: utils
utils:
	make -C util

tags:	TAGS

# This line is needed on some machines.
MAKE=make

SRC=array.c backend.c call_out.c comm1.c debug.c ed.c hash.c \
    hname.c interface.c interpret.c lex.c main.c mapping.c mstring.c \
    mudstat.c ndesc.c net.c nqueue.c object.c otable.c parse.c port.c regexp.c \
    random.c signals.c simulate.c simul_efun.c sprintf.c super_snoop.c \
    tcpsvc.c telnet.c udpsvc.c wildmat.c stdmalloc.c json.c siphash.c memory.c \
    efun/math.c efun/string.c

CSRC=${SRC} lang.c

HEADERS=comm.h config.h exec.h incralloc.h inline_eqs.h inline_svalue.h \
	instrs.h interface.h interpret.h mapping.h master.h master.t \
	mstring.h mudstat.h net.h object.h patchlevel.h regexp.h sent.h \
	backend.h bibopmalloc.h json.h random.h siphash.h memory.h efun/math.h \
	efun/string.h

OBJ=array.o backend.o call_out.o comm1.o debug.o ed.o hash.o \
    hname.o interface.o interpret.o lex.o main.o mapping.o mstring.o \
    mudstat.o ndesc.o net.o nqueue.o object.o otable.o parse.o port.o regexp.o \
    random.o signals.o simulate.o simul_efun.o sprintf.o super_snoop.o \
    tcpsvc.o telnet.o udpsvc.o wildmat.o stdmalloc.o json.o siphash.o memory.o \
    efun/math.o efun/string.o

MPATH=-DMUD_LIB=\"$(MUD_LIB)\"
BINPATH=-DBINDIR=\"$(BINDIR)\"

.c.o:
	$(CC) $(CFLAGS) $(MPATH) $(BINPATH) -o $@ -c $<

driver: $(OBJ) lang.o ${IOBJ}
	$(CC) $(CFLAGS) $(OBJ) lang.o ${IOBJ} -o driver $(LIBS)

func_spec.i: func_spec.c
	$(CC) -E $(CFLAGS) func_spec.c > func_spec.i

master.t master.h: master.n genfkntab
	./genfkntab m master <master.n

make_func.c: make_func.y
	bison -y -o make_func.c make_func.y

genfkntab: genfkntab.o
	$(CC) genfkntab.o -o genfkntab

make_func: make_func.o
	$(CC) make_func.o -o make_func

make_table: make_table.o
	$(CC) make_table.o -o make_table

efun_table.h: lang.h make_table
	./make_table lang.h efun_table.h

lang.y efun_defs.c: func_spec.i make_func prelang.y postlang.y config.h
	./make_func > tmp_efun_defs
	sed -e "s/,,/,T_STRING|T_NUMBER|T_POINTER|T_OBJECT|T_MAPPING|T_FLOAT|T_FUNCTION,/" tmp_efun_defs > efun_defs.c
	rm tmp_efun_defs

lang.c lang.h: lang.y
	bison -y -o lang.c -d lang.y

.PHONY: install
install:
	-mv $(BINDIR)/driver $(BINDIR)/driver.old
	-cp driver $(BINDIR)/driver

tags: $(SRC)
	ctags prelang.y postlang.y $(SRC)

TAGS: $(SRC) $(HEADERS)
	etags -t prelang.y postlang.y $(SRC) $(HEADERS)

.PHONY: depend
depend: Makefile.depend

Makefile.depend: $(SRC) ${ISRC} make_func.c master.h lang.h efun_table.h prelang.y
	$(CC) -MM ${SRC} ${ISRC} make_func.c > Makefile.depend
	$(CC) -MM -MT lang.o -x c prelang.y| sed 's/prelang\.y/lang.c/' >>Makefile.depend

include Makefile.depend

.PHONY: check
check:
	sh regress.sh

.PHONY: clean
clean:
	-rm -f *.o lang.h lang.c lexical.c mon.out *.ln tags
	-rm -f parse core TAGS
	-rm -f config.status lpmud.log
	-rm -f clib/*.o efun/*.o
	-rm -f driver driver.old
	-rm -f efun_defs.c efun_table.h
	-rm -f lang.y
	-rm -f make_func make_func.c make_table func_spec.i
	-rm -f master.h master.t genfkntab
	(cd util ; echo "Cleaning in util." ; $(MAKE) clean)