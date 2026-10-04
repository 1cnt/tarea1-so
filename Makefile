CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lpthread

TARGET = planificador
SRCS = src/parser.c src/dag.c src/main.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean