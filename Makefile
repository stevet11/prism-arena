CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

# Use a system SDL3 (libsdl3-dev) if present, else the bundled headers in
# third_party/ linked against the SDL3 runtime library already on the system.
ifeq ($(shell pkg-config --exists sdl3 && echo yes),yes)
  SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
  SDL_LIBS   := $(shell pkg-config --libs sdl3)
else
  SDL_CFLAGS := -Ithird_party/SDL3/include
  SDL_LIBS   := -Lthird_party/SDL3/lib -lSDL3
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
