CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -g -Wall -Werror -pthread

target ?= aesdsocket

LDFLAGS ?= -pthread

$(target):

all: $(target)

default: all

clean:
	rm -rf *.o
	rm -f $(target)