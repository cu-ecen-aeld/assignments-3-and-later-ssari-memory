CC = gcc
CROSS_COMPILE ?=

CFLAGS = -Wall -Wextra -pthread -lgcc_s -lrt

SRC = aesdsocket.c

OUTPUT = aesdsocket

all: $(OUTPUT)

$(OUTPUT): $(SRC)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o $(OUTPUT) $(SRC)

clean:
	rm -f $(OUTPUT)