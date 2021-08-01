all: woe libplugin-grep.so.1

woe: woe.c
	$(CC) -Wall -W -rdynamic woe.c -o woe -ldl


libplugin-grep.so.1: plugin_grep.c
	$(CC) -Wall -W -shared -fPIC -Wl,-soname,libplugin-grep.so.1	\
		-o libplugin-grep.so.1.0.0 plugin_grep.c woe.c
	ln -s libplugin-grep.so.1.0.0 libplugin-grep.so.1


.PHONY: clean install uninstall
clean: woe libplugin-grep.so.1
	rm woe libplugin-grep.so.1 libplugin-grep.so.1.0.0

install:
	sudo install libplugin-grep.so.1 /usr/lib64/
	sudo install libplugin-grep.so.1.0.0 /usr/lib64/

uninstall:
	sudo rm /usr/lib64/libplugin-grep.so.1
	sudo rm /usr/lib64/libplugin-grep.so.1.0.0

include target/slackware
