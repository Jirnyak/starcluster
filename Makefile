CXX ?= g++
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

SOURCES = main.cpp game.cpp cluster.cpp resource.cpp market.cpp ship.cpp agent.cpp colony.cpp faction.cpp ui.cpp mining.cpp combat.cpp spaceevents.cpp anomaly.cpp modules.cpp chromo.cpp render2d.cpp localgen.cpp localsim.cpp localdraw.cpp

# Всё, кроме main.cpp (для линковки альтернативных точек входа — soak-теста).
LIBSOURCES = $(filter-out main.cpp,$(SOURCES))

SANFLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer

all: game

game: $(SOURCES)
	$(CXX) $(SOURCES) -O3 -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o game

# Санитайзер-сборка игры. Проверка: `make asan && ./game_asan --smoke && ./game_asan --localsmoke`.
asan: $(SOURCES)
	$(CXX) $(SOURCES) $(SANFLAGS) -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o game_asan

# Детерминированный soak-тест локального режима под ASan/UBSan (гоняет тысячи кадров,
# проверяет инварианты и покрывает радио/бой/смерть/луны). Собирает и сразу запускает.
soak: soak_test.cpp $(LIBSOURCES)
	$(CXX) soak_test.cpp $(LIBSOURCES) $(SANFLAGS) -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o soak_asan
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ./soak_asan

clean:
	rm -rf game game_asan soak_asan *.dSYM
