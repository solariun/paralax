TARGET   := paralax
CXX      := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O3 -Iinclude
BUILDDIR := build

.PHONY: all clean test

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(BUILDDIR)/paralax.o $(BUILDDIR)/main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILDDIR)/paralax.o: src/paralax.cpp include/paralax.hpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/main.o: examples/desktop/main.cpp include/paralax.hpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(BUILDDIR)/paralax.o tests/paralax_test.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $(BUILDDIR)/test_runner tests/paralax_test.cpp $(BUILDDIR)/paralax.o
	$(BUILDDIR)/test_runner

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
