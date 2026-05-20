CXX ?= g++
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

SOURCES = main.cpp game.cpp cluster.cpp resource.cpp market.cpp ship.cpp agent.cpp colony.cpp faction.cpp ui.cpp

all: game

game: $(SOURCES)
	$(CXX) $(SOURCES) -O3 -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o game

clean:
	rm -f game
