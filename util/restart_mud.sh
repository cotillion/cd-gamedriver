#!/bin/sh
#
# Restart Lpmud a lot of times, in case of a crash.
#
host=`hostname | sed -e 's/\([^.]*\)\..*/\1/'`
ulimit -d 56320
ulimit -c 20480
umask 002
cd MUD_LIB
rm -f syslog/lplog[0-9]*
rm -f syslog/last_restart
rm -f syslog/{gmon,gprof}.out.*
while true; do
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50; do
	touch syslog/lplog$i
	echo -n "Restart $i. " >> syslog/lplog$i
	touch syslog/last_restart
	echo "Restart $i." >> syslog/last_restart
	mv -f lplog syslog/lplogprev
	ln syslog/lplog$i lplog
	date >> syslog/lplog$i
	rm -f LP_SWAP.4.${host}.* >> syslog/lplog$i
	BINDIR/driver >>syslog/lplog$i 2>&1
	status=$?
	if [ $status = 0 ]; then
	    cp syslog/KEEPERSAVE.o syslog/backup/KEEPERSAVE_BACKUP.o.$i
	    if [ -f gmon.out ]; then
		gprof BINDIR/driver > syslog/gprof.out.$i
		mv -f gmon.out syslog/gmon.out.$i
	    fi
	fi
	echo "Exit status: " $status >>syslog/lplog$i
    done
done
