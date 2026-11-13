# Simple-AirForce Makefile
# Builds the shared dynamic library (libcommon.so), server, and client

CC = gcc
# -Wall enables warnings; -Wl,-rpath,. allows binaries to load libcommon.so from current directory
CFLAGS = -Wall -g
LDFLAGS = -L. -lcommon -Wl,-rpath,.

# Default target: build normal mode and make the test script executable
all: libcommon.so server client
	chmod +x test_10_clients.sh

# Compile-time debug target: adds -DDEBUG_COMPILE to print detailed packet headers
debug: CFLAGS += -DDEBUG_COMPILE
debug: clean all

# Compile common.c into a shared library (libcommon.so)
# -fPIC generates position-independent code; -shared creates a dynamic library
libcommon.so: common.c common.h
	$(CC) $(CFLAGS) -fPIC -shared common.c -o libcommon.so

# Compile server and link against libcommon.so
server: server.c common.h libcommon.so
	$(CC) $(CFLAGS) server.c -o server $(LDFLAGS)

# Compile client and link against libcommon.so
client: client.c common.h libcommon.so
	$(CC) $(CFLAGS) client.c -o client $(LDFLAGS)

# Remove compiled binaries, shared library, and audit log
clean:
	rm -f *.o *.so server client audit.log
