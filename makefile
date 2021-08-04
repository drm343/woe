INCLUDE=/usr/local/include/quickjs
CFLAGS=-I $(INCLUDE) -fPIC -DJS_SHARED_LIBRARY

JS_CC=qjsc

DEPENDECY=woe.js file_storage.js woe_menu.js


.PHONY: release


all: woe.app vt100.so
release: woe.release.app vt100.so


woe.app: $(DEPENDECY)
	$(JS_CC) -o $@ $<


woe.release.app: $(DEPENDECY)
	$(JS_CC) -flto -o woe.app $<


vt100.so: vt100.pic.o
	$(CC) -shared -o $@ $<


vt100.pic.o: vt100.c
	$(CC) $(CFLAGS) -c -o $@ $<


.PHONY: clean test
clean: woe.app vt100.so vt100.pic.o
	rm $?


include target/slackware
