CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -D_GNU_SOURCE
LDFLAGS = -shared
LIBS = -lpthread -lnetfilter_queue -lnfnetlink

TARGET = libproxybridge.so
SOURCES = ProxyBridge.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/lib/
	ldconfig

.PHONY: all clean install
