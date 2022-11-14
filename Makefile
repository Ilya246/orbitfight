OBJS :=	main.o \
	ui.o \
	net.o \
	math.o \
	camera.o \
	strings.o \
	entities.o

LIBS :=	sfml-window \
	sfml-system \
	sfml-network \
	sfml-graphics

INCLUDES :=	-I./include

CXX		?= g++
CXXSTANDARD	?= c++20
CXXWARNS	?= -Wall -Wextra -pedantic
CXXFLAGS	?= -O3
override \
  CXXFLAGS	+= $(CXXWARNS) -std=$(CXXSTANDARD) $(INCLUDES)
LD		:= $(CXX)
LDFLAGS		?= -pthread
override \
  LDFLAGS	+= $(shell pkg-config --libs $(LIBS))
STRIP		?= strip
RM		?= rm -f

.PHONY: all
all: orbitfight

.PHONY: run
run: all
	@./orbitfight

.PHONY: clean
clean:
	$(RM) orbitfight
	@$(RM) $(OBJS) $(DEPS)

.PHONY: rebuild
rebuild: clean all | clean

.PHONY: analyze
analyze: CXXFLAGS += -g -Og -fanalyzer -Werror -Wno-inline -Wno-suggest-attribute={pure,noreturn}
analyze: rebuild

.PHONY: debug
debug: CXXFLAGS += -DDEBUG -g -Og
debug: analyze

.PHONY: release
release: CXXFLAGS += -DNDEBUG
release: DO_STRIP = 1
release: rebuild

#####

OBJS := $(addprefix build/, $(OBJS))

orbitfight: $(OBJS)
	@echo "  LD		$@"
	@$(LD) $(LDFLAGS) -o $@ $^
	@if [[ "$(DO_STRIP)" ]]; then echo "  STRIP		$@"; $(STRIP) $(STRIPFLAGS) $@; fi

DEPS := $(join $(dir $(OBJS)), $(addprefix ., $(notdir $(OBJS:.o=.d~))))
-include $(DEPS)

build/%.o: src/%.cpp
	@echo "  CXX		$@"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -MF $(join $(dir $@), $(addprefix ., $(notdir $(@:.o=.d~)))) -c -o $@ $<
