CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
COMMON   := ../common
BINDIR   := bin

all: $(BINDIR) $(BINDIR)/cheese_client

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/cheese_client: src/client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
