WATCOM ?= /home/awe/openwatcom-1.9
WCL := $(WATCOM)/binl/wcl

all: linux98.exe

linux98.exe: elfboot.c
	WATCOM=$(WATCOM) PATH=$(WATCOM)/binl:$$PATH \
	INCLUDE=$(WATCOM)/h $(WCL) -q -l=dos -3 -ms -fe=$@ $<

clean:
	rm -f linux98.exe *.o *.obj *.err

.PHONY: all clean
