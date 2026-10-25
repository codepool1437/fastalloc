CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS  ?= -pthread

all: test bench_bin

build:
	mkdir -p build

build/test_fastalloc: build src/fastalloc.cpp tests/test_fastalloc.cpp
	$(CXX) $(CXXFLAGS) src/fastalloc.cpp tests/test_fastalloc.cpp -o $@ $(LDFLAGS)

build/bench: build src/fastalloc.cpp bench/bench.cpp
	$(CXX) $(CXXFLAGS) src/fastalloc.cpp bench/bench.cpp -o $@ $(LDFLAGS)

test: build/test_fastalloc
	./build/test_fastalloc

bench_bin: build/bench

bench: build/bench
	./build/bench

# Thread sanitizer run to check for data races
tsan:
	$(CXX) $(CXXFLAGS) -O1 -g -fsanitize=thread src/fastalloc.cpp tests/test_fastalloc.cpp -o build/test_tsan $(LDFLAGS) && ./build/test_tsan

clean:
	rm -rf build

.PHONY: all test bench bench_bin tsan clean
