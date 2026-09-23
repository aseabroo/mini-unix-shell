CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS := -D_POSIX_C_SOURCE=200809L
TARGET := bin/minish
SRC := src/shell.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $(TARGET)

test: $(TARGET)
	sh tests/smoke.sh

clean:
	rm -rf bin
