GCC=clang
CFLAGS=-Wall -s -Os
CLIBS=-lusb-1.0

project=tinyfpgasp
parser=cmdline
version=$(shell ./version.sh)

OBJECTS=$(project).o $(parser).o

all: $(project)

$(project).o: $(project).c $(parser).h
	$(GCC) -c $(CFLAGS) $<

$(parser).c: $(parser).ggo makefile
	gengetopt < $< --file-name=$(parser) # --unamed-opts

$(parser).h: $(parser).ggo makefile
	gengetopt < $< --file-name=$(parser) # --unamed-opts

$(project): $(OBJECTS) makefile
	$(GCC) $(CFLAGS) $(CLIBS) $(OBJECTS) -o $@

clean:
	rm -f $(project) $(OBJECTS) $(parser).o $(parser).c $(parser).h *~
