CC = gcc
CFLAGS = -Wall -Wextra -O3 -I../src \
	-fstack-protector-strong \
	-D_FORTIFY_SOURCE=2 \
	-fPIE \
	-Wformat -Wformat-security \
	-Werror=format-security \
	-fno-strict-overflow \
	-fno-delete-null-pointer-checks \
	-fwrapv
LDFLAGS = -L../src -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -s \
	-Wl,-rpath,/usr/local/lib
LIBS = -lproxybridge -lpthread

TARGET = ProxyBridge
SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "Stripping debug symbols..."
	strip --strip-all $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
