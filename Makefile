CC      = gcc
CFLAGS  = -Wall -Wextra -std=c17
LDLIBS  = -lpthread
SRC     = $(wildcard src/*.c)

planificador: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f planificador

.PHONY: clean
