CXX=c++
CXXFLAGS=-std=c++20 -Os -flto -fno-exceptions -fno-rtti -march=native -Wall -Wextra -Wpedantic -Wconversion
LDFLAGS=`pkg-config libgvc --libs` -flto

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

rtd: ${OBJ}
	${CXX} -o $@ ${OBJ} ${LDFLAGS}

tests: rtd
	mkdir output 2>/dev/null ; \
	rm -f output/* ; \
	for filename in tests/*; do \
			./rtd -e "$$(cat "$$filename")" >graph.dot && dot -Tsvg graph.dot >output/"$$(basename "$$filename")".svg ; \
	done

clean:
	rm -rf rtd ${OBJ} graph.dot graph.svg output

.PHONY: all options svg tests clean
