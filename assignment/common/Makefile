CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
BINDIR   := bin

all: $(BINDIR) $(BINDIR)/exchange_server

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/exchange_server: exchange.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
