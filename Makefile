CXX=g++
CXXFLAGS=-std=c++20 -Os -fno-exceptions -fno-rtti -march=native -Wall -Wextra -Wpedantic
LIBS=-lfmt
OBJS=main.o

default: regex-to-dfa

regex-to-dfa: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o regex-to-dfa $(LIBS)

main.o: main.cpp
	$(CXX) -c $(CXXFLAGS) main.cpp

clean:
	rm -rf *.o regex-to-dfa
