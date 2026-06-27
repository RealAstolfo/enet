CC  = gcc
CXX = g++
LD  = ld
AR ?= gcc-ar
AS  = as

PREFIX  ?= /usr/local
DESTDIR ?=

NAME = enet
LIB_ARCHIVE = lib$(NAME).a

# Headers: this repo plus whatever Guix put on the search path (exstd, openssl,
# zlib, libmd, util-linux, optionally i2pd).  No -I./vendors: the vendored tree
# is gone; siblings/third-party headers arrive via CPATH from the Guix inputs.
INC = -I./include
LIB = -L.

# Third-party flags.  Compile flags come from pkg-config; link flags use static
# archives wrapped in -Wl,-Bstatic/-Bdynamic so the named libs are pulled in
# statically while glibc (NSS/DNS) stays dynamic.  No global -static.
SSL_CFLAGS  = $(shell pkg-config --cflags openssl)
SSL_LIBS    = -Wl,-Bstatic $(shell pkg-config --libs-only-l openssl) -Wl,-Bdynamic $(shell pkg-config --libs-only-L openssl)
ZLIB_CFLAGS = $(shell pkg-config --cflags zlib)
ZLIB_LIBS   = -Wl,-Bstatic $(shell pkg-config --libs-only-l zlib) -Wl,-Bdynamic $(shell pkg-config --libs-only-L zlib)
MD_CFLAGS   = $(shell pkg-config --cflags libmd)
MD_LIBS     = -Wl,-Bstatic $(shell pkg-config --libs-only-l libmd) -Wl,-Bdynamic $(shell pkg-config --libs-only-L libmd)
UUID_CFLAGS = $(shell pkg-config --cflags uuid)
UUID_LIBS   = -Wl,-Bstatic $(shell pkg-config --libs-only-l uuid) -Wl,-Bdynamic $(shell pkg-config --libs-only-L uuid)

# Static C++/gcc runtimes per the mostly-static link policy.
STATIC_RT = -static-libstdc++ -static-libgcc

CFLAGS   = -march=native -O3 -g -Wall -Wextra -pedantic $(INC)
CXXFLAGS = -std=c++20 $(CFLAGS)
LDFLAGS  = $(LIB) -O3 $(STATIC_RT)

#########################################################################################
# Library
#########################################################################################
# Core library objects.  http/https/network-buffer are header-only (no objects);
# the DHT is the one compiled core translation unit.
LIB_OBJS = dht.o

# I2P transport is a first-class feature: i2p.o is ALWAYS part of libenet.a.
# i2pd-lib (custom Guix package) provides libi2pd*.a + headers on the search
# path (CPATH/LIBRARY_PATH).  Its static archives link into enet executables;
# boost/openssl/zlib are i2pd's own deps and are also linked statically.
LIB_OBJS += i2p.o
# Boost.System is header-only since Boost 1.69 (we build against 1.83), so no
# libboost_system archive is needed -- the symbols i2pd uses are inline.  Guix
# ships boost shared-only anyway; keeping the link fully static this way avoids
# pulling a (nonexistent) libboost_system.a.
I2P_LIBS = -Wl,-Bstatic -l:libi2pdclient.a -l:libi2pd.a -l:libi2pdlang.a \
           -lboost_program_options -lboost_filesystem -Wl,-Bdynamic

dht.o:
	${CC} ${CFLAGS} ${MD_CFLAGS} -c src/dht.c -o $@

i2p.o:
	${CXX} ${CXXFLAGS} ${SSL_CFLAGS} -c src/i2p.cpp -o $@

$(LIB_ARCHIVE): $(LIB_OBJS)
	$(AR) rcs $@ $^

lib: $(LIB_ARCHIVE)

#########################################################################################
# HTTP Client Testing
#########################################################################################

http-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_http.cpp -o $@

http-test: http-test.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} -o $@

#########################################################################################
# HTTPS Client Testing
#########################################################################################

https-test.o:
	${CXX} ${CXXFLAGS} ${SSL_CFLAGS} -c builds/test/simple_https.cpp -o $@

https-test: https-test.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} ${SSL_LIBS} -o $@

#########################################################################################
# SOCKS4 Testing
#########################################################################################

http_socks4_client.o:
	${CXX} ${CXXFLAGS} -c builds/test/http_socks4_client.cpp -o $@

http_socks4_server.o:
	${CXX} ${CXXFLAGS} -c builds/test/http_socks4_server.cpp -o $@

http_socks4_client: http_socks4_client.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} -o $@

http_socks4_server: http_socks4_server.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} -o $@

#########################################################################################
# Network Buffer Testing
#########################################################################################

network-buffer-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/network_buffer_test.cpp -o $@

network-buffer-test: network-buffer-test.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} -o $@

#########################################################################################
# DHT Client Testing
#########################################################################################

dht-test.o:
	${CXX} ${CXXFLAGS} ${MD_CFLAGS} -c builds/test/simple_dht.cpp -o $@

dht-test: dht-test.o dht.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} ${MD_LIBS} -o $@

#########################################################################################
# I2P Client Testing (optional, requires I2P=1 and an i2pd that exports the lib)
#########################################################################################

i2p-test.o:
	${CXX} ${CXXFLAGS} ${SSL_CFLAGS} -c builds/test/simple_i2p.cpp -o $@

i2p-test: i2p-test.o i2p.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} ${I2P_LIBS} ${SSL_LIBS} ${ZLIB_LIBS} -o $@

#########################################################################################

all: lib http-test https-test network-buffer-test dht-test

# Install: static archive to $(PREFIX)/lib, headers to $(PREFIX)/include.
install: lib
	mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	cp $(LIB_ARCHIVE) $(DESTDIR)$(PREFIX)/lib/
	cp -r include/* $(DESTDIR)$(PREFIX)/include/

# Generate compile_commands.json (replaces clangd.sh).
clangd:
	bear -- make all

clean:
	-rm -f http-test https-test i2p-test http_socks4_client http_socks4_server \
		network-buffer-test dht-test $(LIB_ARCHIVE) *.o


# Position-independent code: required so each repo's static archive can be
# bundled into the eengine umbrella shared library (libeengine.so).
CFLAGS   += -fPIC
CXXFLAGS += -fPIC
.PHONY: all lib install clangd clean
