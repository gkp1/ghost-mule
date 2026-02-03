CC = gcc
CFLAGS = -Wall -Wextra -O3 -fPIC -D_GNU_SOURCE \
	-fstack-protector-strong \
	-D_FORTIFY_SOURCE=2 \
	-fPIE \
	-Wformat -Wformat-security \
	-Werror=format-security \
	-fno-strict-overflow \
	-fno-delete-null-pointer-checks \
	-fwrapv
LDFLAGS = -shared -Wl,-z,relro,-z,now -Wl,-z,noexecstack -s
LIBS = -lpthread -lnetfilter_queue -lnfnetlink

TARGET = libproxybridge.so
SOURCES = ProxyBridge.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Stripping debug symbols..."
	strip --strip-unneeded $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/lib/
	ldconfig

.PHONY: all clean install
