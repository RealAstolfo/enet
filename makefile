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
# zlib, libmd, util-linux, i2pd-lib).  No -I./vendors: the vendored tree
# is gone; siblings/third-party headers arrive via CPATH from the Guix inputs.
INC = -I./include
LIB = -L.

# Third-party flags, spelled out (no pkg-config query): the Guix inputs put the
# headers (openssl, libmd, ...) on the compiler's include search path and the
# static archives (openssl/zlib "static" outputs, libmd-static,
# util-linux-static) on LIBRARY_PATH, so only the -l names are needed.  They
# are wrapped in -Wl,-Bstatic/-Bdynamic so the named libs are pulled in
# statically while glibc (NSS/DNS) stays dynamic.  No global -static.
SSL_LIBS  = -Wl,-Bstatic -lssl -lcrypto -Wl,-Bdynamic
ZLIB_LIBS = -Wl,-Bstatic -lz -Wl,-Bdynamic
MD_LIBS   = -Wl,-Bstatic -lmd -Wl,-Bdynamic
UUID_LIBS = -Wl,-Bstatic -luuid -Wl,-Bdynamic

# Helpers for the recipes below (`test`, `compdb`), which loop and write files
# with make's own functions rather than shell code.
comma := ,
define newline


endef

# Static C++/gcc runtimes per the mostly-static link policy.
STATIC_RT = -static-libstdc++ -static-libgcc

CFLAGS   = -march=native -O3 -g -Wall -Wextra -pedantic $(INC) $(SANFLAGS)
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
LIB_OBJS += mdns.o
# Boost.System is header-only since Boost 1.69 (we build against 1.83), so no
# libboost_system archive is needed -- the symbols i2pd uses are inline.  Guix
# ships boost shared-only anyway; keeping the link fully static this way avoids
# pulling a (nonexistent) libboost_system.a.
I2P_LIBS = -Wl,-Bstatic -l:libi2pdclient.a -l:libi2pd.a -l:libi2pdlang.a \
           -lboost_program_options -lboost_filesystem -Wl,-Bdynamic

dht.o:
	${CC} ${CFLAGS} -c src/dht.c -o $@

i2p.o:
	${CXX} ${CXXFLAGS} -c src/i2p.cpp -o $@

# mDNS/DNS-SD service discovery (mjansson/mdns header from Guix; pure sockets).
mdns.o:
	${CXX} ${CXXFLAGS} -c src/mdns_service.cpp -o $@

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
	${CXX} ${CXXFLAGS} -c builds/test/simple_https.cpp -o $@

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
	${CXX} ${CXXFLAGS} -c builds/test/simple_dht.cpp -o $@

dht-test: dht-test.o dht.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} ${MD_LIBS} -o $@

#########################################################################################
# I2P Client Testing
#########################################################################################

i2p-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_i2p.cpp -o $@

i2p-test: i2p-test.o i2p.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} ${I2P_LIBS} ${SSL_LIBS} ${ZLIB_LIBS} -o $@

#########################################################################################
# mDNS service-discovery test
#########################################################################################

mdns-service-test.o:
	${CXX} ${CXXFLAGS} -I./tests -c tests/mdns_test.cpp -o $@

mdns-test: mdns-service-test.o mdns.o
	${CXX} ${CXXFLAGS} $^ ${LDFLAGS} -o $@

#########################################################################################
# Test suite
#########################################################################################
# The header-only helpers (socket_io.hpp, tcp_listener.hpp) only compile when a
# translation unit includes them, so the tests are where they are actually
# built.  `make test` rebuilds every test from scratch and runs them, one
# recipe line per test, so make itself stops the run at the first failure.
# `make sanitize-address` does the same under ASan+UBSan (enet does not use
# mimalloc, so a plain -fsanitize=address,undefined suffices; the single
# compile+link command applies the flags to both stages).
TESTS = socket-io-test

socket-io-test: tests/socket_io_test.cpp include/socket_io.hpp include/tcp_listener.hpp include/tcp.hpp tests/check.hpp
	mkdir -p tests/bin
	${CXX} ${CXXFLAGS} -I./tests tests/socket_io_test.cpp -pthread -o tests/bin/$@

# A full rebuild is forced (via clean) so that switching sanitizer flags between
# `make test` and `make sanitize-address` always recompiles the binaries.
test:
	$(MAKE) clean
	$(MAKE) $(TESTS)
	$(foreach t,$(TESTS),@echo "== $(t)"$(newline)@$(TEST_RUNNER) ./tests/bin/$(t)$(newline))

sanitize-address:
	$(MAKE) test SANFLAGS="-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
	$(MAKE) clean

#########################################################################################

all: lib http-test https-test network-buffer-test dht-test i2p-test mdns-test

# Install: static archive to $(PREFIX)/lib, headers to $(PREFIX)/include.
install: lib
	mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	cp $(LIB_ARCHIVE) $(DESTDIR)$(PREFIX)/lib/
	cp -r include/* $(DESTDIR)$(PREFIX)/include/

# compile_commands.json for clangd, written by make itself with $(file ...)
# (it replaces `bear -- make all`): one entry per translation unit, with the
# compiler and flags of the rule that builds it.  json_str escapes a make
# string as a JSON string.
json_str     = "$(subst ",\",$(subst \,\\,$(1)))"
compdb_entry = {"directory": $(call json_str,$(CURDIR)), "file": $(call json_str,$(1)), "command": $(call json_str,$(2) -c $(1))}

COMPDB = $(call compdb_entry,src/dht.c,$(CC) $(CFLAGS)) \
         $(foreach s,src/i2p.cpp src/mdns_service.cpp $(wildcard builds/test/*.cpp), \
           $(call compdb_entry,$(s),$(CXX) $(CXXFLAGS))) \
         $(call compdb_entry,tests/mdns_test.cpp,$(CXX) $(CXXFLAGS) -I./tests) \
         $(call compdb_entry,tests/socket_io_test.cpp,$(CXX) $(CXXFLAGS) -I./tests -pthread)

compdb:
	$(file >compile_commands.json,[$(newline)  $(subst } {,}$(comma)$(newline)  {,$(strip $(COMPDB)))$(newline)])
	$(info compdb: wrote compile_commands.json)

clean:
	-rm -f http-test https-test i2p-test http_socks4_client http_socks4_server \
		network-buffer-test dht-test $(LIB_ARCHIVE) *.o
	-rm -rf tests/bin


# Position-independent code: required so each repo's static archive can be
# bundled into the eengine umbrella shared library (libeengine.so).
CFLAGS   += -fPIC
CXXFLAGS += -fPIC
.PHONY: all lib install compdb clean test sanitize-address
