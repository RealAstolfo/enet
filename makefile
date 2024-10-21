CC = gcc
CXX = g++
LD = ld
AR = ar
AS = as

INC = -I./include -I./vendors -I./vendors/exstd/include -I./vendors/i2pd/libi2pd
LIB =  -L. -L/usr/lib64 -L/usr/local/lib64

I2P = -L./vendors/i2pd -Wl,-Bstatic -li2pd -Wl,-Bdynamic -lssl -lcrypto -lz -lboost_system -lboost_program_options -lboost_filesystem
MD5 = `pkgconf --cflags --libs libmd`


CFLAGS = -march=native -O3 -g -Wall -Wextra -pedantic -fsanitize=address $(INC)
CXXFLAGS = -std=c++20 $(CFLAGS)
LDFLAGS = $(LIB) -O3

# Networking:
i2p.o:
	${CXX} ${CXXFLAGS} -c src/i2p.cpp -o $@

vendors/i2pd/libi2pd.a:
	make -C vendors/i2pd libi2pd.a

#########################################################################################

# HTTP Client Testing
#########################################################################################

http-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_http.cpp -o $@

http-test: http-test.o
	${CXX} ${CXXFLAGS} $^ -o $@

#########################################################################################

# HTTPS Client Testing
#########################################################################################

https-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_https.cpp -o $@

https-test: https-test.o
	${CXX} ${CXXFLAGS} -lssl $^ -o $@


#########################################################################################

# SOCKS4 Testing
#########################################################################################

http_socks4_client.o:
	${CXX} ${CXXFLAGS} -c builds/test/http_socks4_client.cpp -o $@

http_socks4_server.o:
	${CXX} ${CXXFLAGS} -c builds/test/http_socks4_server.cpp -o $@

http_socks4_client: http_socks4_client.o
	${CXX} ${CXXFLAGS} $^ -o $@

http_socks4_server: http_socks4_server.o
	${CXX} ${CXXFLAGS} $^ -o $@


#########################################################################################

# I2P Client Testing
#########################################################################################

i2p-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_i2p.cpp -o $@

i2p-test: i2p-test.o i2p.o vendors/i2pd/libi2pd.a
	${CXX} ${CXXFLAGS} $^ ${I2P} -o $@


#########################################################################################

# Network Buffer Testing
#########################################################################################
network-buffer-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/network_buffer_test.cpp -o $@

network-buffer-test: network-buffer-test.o
	${CXX} ${CXXFLAGS} $^ -o $@


#########################################################################################

# DHT Client Testing
#########################################################################################
dht.o:
	${CC} ${CFLAGS} -c src/dht.c -o $@

dht-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_dht.cpp -o $@

dht-test: dht-test.o dht.o
	${CXX} ${CXXFLAGS} $^ ${MD5} -o $@

all: http-test https-test i2p-test network-buffer-test dht-test

clean:
	-rm -f http-test https-test i2p-test network-buffer-test dht-test *.o
	make -C vendors/exstd clean
	make -C vendors/i2pd clean
