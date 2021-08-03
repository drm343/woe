INCLUDE=/usr/local/include/quickjs
CFLAGS=-I $(INCLUDE) -fPIC -DJS_SHARED_LIBRARY

JS_CC=qjsc


all: woe.app vt100.so
release: woe.release.app vt100.so


woe.app: woe.js
	$(JS_CC) -o $@ $<


woe.release.app: woe.js
	$(JS_CC) -flto -o $@ $<


vt100.so: vt100.pic.o
	$(CC) -shared -o $@ $<


vt100.pic.o: vt100.c
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean test
clean: woe.app vt100.so vt100.pic.o
	rm $?


include target/slackware
