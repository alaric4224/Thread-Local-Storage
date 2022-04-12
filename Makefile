override CFLAGS := -Wall -Werror -std=gnu99 -pedantic -O0 -g -pthread $(CFLAGS)
override LDLIBS := -pthread $(LDLIBS)

tls.o: tls.c

check: tls.o
	$(CC) -pthread -g -Wall -Werror -std=gnu99 -pedantic -O0 -o test tls.o test.c

.PHONY: clean

clean:
	rm -f tls.o test
