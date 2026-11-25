CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

# SDL3 comes from Conan (`conan install . --build=missing -of build/conan`)
# or, failing that, a system SDL3 (libsdl3-dev).
CONAN_PC := build/conan/build/Release/generators
ifneq ($(wildcard $(CONAN_PC)/sdl.pc),)
  SDL_CFLAGS := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --cflags sdl)
  SDL_LIBS   := $(shell PKG_CONFIG_PATH=$(CONAN_PC) pkg-config --libs --static sdl)
else ifeq ($(shell pkg-config --exists sdl3 && echo yes),yes)
  SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
  SDL_LIBS   := $(shell pkg-config --libs sdl3)
else
  $(error SDL3 not found: run `conan install . --build=missing -of build/conan` (see README))
endif

SRC := src/main.cpp src/audio.cpp
HDR := $(wildcard src/*.hpp)

prism: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(SRC) -o $@ $(SDL_LIBS)

run: prism
	./prism

clean:
	rm -f prism

.PHONY: run clean
