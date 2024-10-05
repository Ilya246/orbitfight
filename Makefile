CXX ?= g++
XXD ?= xxd
STRIP := strip

STANDARD ?= c++20
CXXFLAGS ?= -O3 -Wall -Wextra -pedantic -g
CXXFLAGS += -std=$(STANDARD) -c -Iinclude
LDFLAGS ?= $(shell pkg-config --libs sfml-window sfml-graphics sfml-system sfml-network)

sources := $(shell find src -type f -name "*.cpp")
objects := $(sources:src/%.cpp=build/%.o)
depends := $(sources:src/%.cpp=build/%.d)
assets_o := $(shell find assets -type f)
assets := $(assets_o:assets/%.ttf=include/%.asset)

builddir ?= build
assetdir ?= assets

all: orbitfight

$(builddir)/%.o: src/%.cpp $(assets)
	@printf "CC\t%s\n" $@
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) -MMD -MP $< -o $@

-include $(depends)

orbitfight: $(objects)
	@printf "LD\t%s\n" $(builddir)/$@
	@$(CXX) $^ -o $(builddir)/$@ $(LDFLAGS)

include/%.asset: $(assetdir)/%.*
	@printf "XXD\t%s\n" $@
	@$(XXD) -i $< $@

clean:
	rm build/*.o
	rm build/*.d
	rm include/*.asset

strip: all
	$(STRIP) $(builddir)/orbitfight

run: all
	@$(builddir)/orbitfight

.PHONY: all clean strip run
