CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
BINDIR   := bin
TARGETS  := $(BINDIR)/exchange_server $(BINDIR)/cheese_client

all: $(BINDIR) $(TARGETS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/exchange_server: src/exchange.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BINDIR)/cheese_client: src/client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
