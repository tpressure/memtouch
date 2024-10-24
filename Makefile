all: memtouch.cpp
	g++ memtouch.cpp -O2 -Wall -std=c++20 -Iinclude/ -o memtouch

clean:
	@rm -rf memtouch
