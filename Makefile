CXX=c++
CXXFLAGS=-std=c++17 -Os -flto -fno-exceptions -fno-rtti -march=native -Wall -Wextra -Wpedantic -Wconversion
LDFLAGS=`pkg-config fmt libgvc --libs` -flto

SRC = main.cpp
OBJ = ${SRC:.cpp=.o}

all: options rtd

options:
	@echo rtd build options:
	@echo "CXXFLAGS = ${CXXFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CXX      = ${CXX}"

.cpp.o:
	${CXX} -c ${CXXFLAGS} $<

${OBJ}: numtypes.hpp

rtd: ${OBJ}
	${CXX} -o $@ ${OBJ} ${LDFLAGS}

format:
	clang-format --verbose -i *.cpp *.hpp

clean:
	rm -f rtd ${OBJ}

.PHONY: all options clean format
