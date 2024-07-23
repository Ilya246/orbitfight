CXX ?= g++
STRIP := strip

STANDARD ?= c++20
CXXFLAGS ?= -O3 -Wall -Wextra -pedantic -g
CXXFLAGS += -std=$(STANDARD) -c -Iinclude
LDFLAGS ?= $(shell pkg-config --libs sfml-window sfml-graphics sfml-system sfml-network)

sources := $(shell find src -type f -name "*.cpp")
objects := $(sources:src/%.cpp=build/%.o)
depends := $(sources:src/%.cpp=build/%.d)

builddir = ./build

all: orbitfight

build/%.o: src/%.cpp
	@printf "CC\t%s\n" $@
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) -MMD -MP $< -o $@

-include $(depends)

orbitfight: $(objects)
	@printf "LD\t%s\n" $(builddir)/$@
	@$(CXX) $^ -o $(builddir)/$@ $(LDFLAGS)
	@cp assets $(builddir) -r

clean:
	rm build/*.o
	rm build/*.d

strip: all
	$(STRIP) $(builddir)/orbitfight

run: all
	@$(builddir)/orbitfight

.PHONY: all clean strip run
