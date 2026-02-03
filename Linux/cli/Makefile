CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../src
LDFLAGS = -L../src
LIBS = -lproxybridge -lpthread

TARGET = proxybridge-cli
SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
