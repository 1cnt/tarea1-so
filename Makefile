CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -lpthread -O2

TARGET = planificador

SRCS = main.c parser.c dag.c
OBJS = $(SRCS:.c=.o)
HEADERS = parser.h dag.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
