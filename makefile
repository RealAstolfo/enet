CC = clang
CXX = clang++
LD = ld.lld
AR = llvm-ar
AS = llvm-as

INC = -I./include -I./vendors/exstd/include
LIB =  -L. -L/usr/lib64 -L/usr/local/lib64

SSL = -lssl -lcrypto

# -target native-windows
CFLAGS = $(ZTARGET) -march=native -O3 -g -Wall -Wextra -Wno-missing-field-initializers -pedantic $(INC)
CXXFLAGS = -std=c++20 $(CFLAGS)
LDFLAGS = $(LIB) -O3

# Networking:
#i2p.o:
#	${CXX} ${CXXFLAGS} -c src/i2p.cpp -o $@

#########################################################################################

# HTTP Client Testing
#########################################################################################

http-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_http.cpp -o $@

http-test: http-test.o
	${CXX} ${CXXFLAGS} ${LIB} $^ -o $@

#########################################################################################

# HTTPS Client Testing
#########################################################################################

https-test.o:
	${CXX} ${CXXFLAGS} -c builds/test/simple_https.cpp -o $@

https-test: https-test.o
	${CXX} ${CXXFLAGS} ${SSL} $^ -o $@

#########################################################################################

# I2P Client Testing
#########################################################################################

#i2p-test.o:
#	${CXX} ${CXXFLAGS} -c builds/test/simple_i2p.cpp -o $@

#i2p-test: i2p-test.o i2p.o
#	${CXX} ${CXXFLAGS} ${SSL} $^ ${I2P} -o $@

all: https-test http-test

clean:
	-rm -f http-test https-test i2p-test
	-rm -f *.o
