CC=$(CROSS_COMPILE)gcc
CFLAGS=-g -Wall -Werror
LDFLAGS=

OBJ=aesdsocket.o

all: aesdsocket

default: aesdsocket

aesdsocket: $(OBJ)
	$(CC) $(CFLAGS) -o aesdsocket $(OBJ) $(LDFLAGS)

%o: %c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f aesdsocket.o
	rm -f aesdsocket