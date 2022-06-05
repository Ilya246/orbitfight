CXX ?= g++
STRIP := strip

STANDARD ?= c++20
CXXFLAGS ?= -O3 -Wall -Wextra -pedantic -g
override CXXFLAGS += -std=$(STANDARD) -c -Iheaders
LDFLAGS := $(shell pkg-config --libs sfml-window sfml-graphics sfml-system)

sources := $(shell find src -type f -name "*.cpp")
objects := $(sources:src/%.cpp=build/%.o)
depends := $(sources:src/%.cpp=build/%.d)

all: orbitfight

build/%.o: src/%.cpp
	@printf "CC\t%s\n" $@
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) -MMD -MP $< -o $@

-include $(depends)

orbitfight: $(objects)
	@printf "CCLD\t%s\n" $@
	@$(CXX) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf build

strip: all
	$(STRIP) orbitfight

run: all
	@./orbitfight

.PHONY: all clean strip run