all:	hname

hname:	hname.c
	$(CC) $(CFLAGS) hname.c $(LIBS) -o hname

install: hname
	-mv $(BINDIR)/hname $(BINDIR)/hname.old
	cp hname $(BINDIR)/hname
	-mv $(BINDIR)/restart_mud $(BINDIR)/restart_mud.old
	sed -e "s;MUD_LIB;$(MUD_LIB);" \
		-e "s;BINDIR;$(BINDIR);" restart_mud.sh > $(BINDIR)/restart_mud
	-chmod 775 $(BINDIR)/restart_mud

clean:
	-rm -f *.o hname core
