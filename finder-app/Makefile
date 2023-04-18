CC := gcc
CFLAGS := -Wall 
LDFLAGS := 
EXECUTABLE := writer

ifdef CROSS_COMPILE
	CC := $(CROSS_COMPILE)$(CC)
endif

all: $(EXECUTABLE)

$(EXECUTABLE): writer.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(EXECUTABLE)