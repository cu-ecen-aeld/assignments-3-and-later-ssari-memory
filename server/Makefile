CC := gcc
CFLAGS := -ggdb -Wall 
LDFLAGS := 
EXECUTABLE := aesdsocket

ifdef CROSS_COMPILE
	CC := $(CROSS_COMPILE)$(CC)
endif

all: $(EXECUTABLE)

$(EXECUTABLE): aesdsocket.c
	$(CC) $(CFLAGS) -o $(EXECUTABLE)

clean:
	rm -f $(EXECUTABLE)