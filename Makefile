CC=gcc
CFLAGS=-Wall -Wextra -g

TARGET=shell

SRC=src/main.c

all:
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
