woe: libp_grep.so woe.c
	$(CC) -Wall -W -rdynamic woe.c -o woe -ldl


libp_grep.so: plugin_grep.c
	$(CC) -Wall -W -shared -fPIC -o libp_grep.so plugin_grep.c woe.c


.PHONY: clean
clean: woe
	rm woe libp_grep.so
