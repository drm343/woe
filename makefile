woe: woe.c
	$(CC) -Wall -W woe.c -o woe -ldl

.PHONY: clean
clean: woe
	rm woe
