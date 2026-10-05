CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -lpthread -O2

TARGET = planificador

SRCS = src/main.c src/parser.c src/dag.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
