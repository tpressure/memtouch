.PHONY: all
all: memtouch.cpp
	g++ memtouch.cpp -O2 -Wall -std=c++20 -Iinclude/ -o memtouch

.PHONY: install
install: all
	install -d $(DESTDIR)/bin
	install -m 755 memtouch $(DESTDIR)/bin

.PHONY: clean
clean:
	@rm -rf memtouch
