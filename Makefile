CXX ?= g++
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs) -lSDL2_mixer

SOURCES = drive.cpp exotic.cpp main.cpp shell.cpp game.cpp cluster.cpp resource.cpp market.cpp econ.cpp ship.cpp agent.cpp colony.cpp faction.cpp ui.cpp mining.cpp combat.cpp spaceevents.cpp anomaly.cpp modules.cpp chromo.cpp render2d.cpp localgen.cpp localsim.cpp localdraw.cpp stb_image.cpp i18n.cpp

# Всё, кроме точки входа (для линковки альтернативных main — soak/uiclick/balance).
# shell.cpp тоже исключён: оболочка опирается на assetPath() из main.cpp и в
# headless-харнесах не нужна.
LIBSOURCES = $(filter-out main.cpp shell.cpp,$(SOURCES))

SANFLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer

# Предупреждения были выключены, поэтому «сборка без предупреждений» ничего не
# значила: с -Wall их обнаруживалось 18. Держим их включёнными постоянно.
WARNFLAGS = -Wall -Wextra -Wno-unused-parameter

all: game

game: $(SOURCES)
	$(CXX) $(SOURCES) -O3 -std=c++11 $(WARNFLAGS) $(SDL_CFLAGS) $(SDL_LIBS) -o game

# Балансовый стенд экономики: матрица способностей, опорные цены, волна
# замещения, пространственный разброс цен. Без SDL — только числа.
econ: econ_test.cpp econ.cpp market.cpp resource.cpp
	$(CXX) econ_test.cpp econ.cpp market.cpp resource.cpp -O2 -std=c++11 -o econ_test
	./econ_test

# Регрессия на маршрутизацию кликов между перекрывающимися окнами (GO/TRADE/CARGO
# верхнего окна не должны съедаться нижним). Без окна — только логика попаданий.
uiclick: ui_click_test.cpp $(LIBSOURCES)
	$(CXX) ui_click_test.cpp $(LIBSOURCES) $(SANFLAGS) -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o ui_click_test
	SDL_VIDEODRIVER=dummy ASAN_OPTIONS=detect_leaks=0 ./ui_click_test

# Регрессия БАЛАНСА: проскальзывание, честность биржевой сводки, проходимость
# квоты, непрерывность лестницы цен, окупаемость трюма и разведки. Лестница
# проверок покрывала память и инварианты, но не экономику — а все крупные
# ошибки оказались именно там (см. balance_test.cpp).
balance: balance_test.cpp $(LIBSOURCES)
	$(CXX) balance_test.cpp $(LIBSOURCES) -O2 -std=c++11 $(WARNFLAGS) $(SDL_CFLAGS) $(SDL_LIBS) -o balance_test
	SDL_VIDEODRIVER=dummy ./balance_test

# Санитайзер-сборка игры. Проверка: `make asan && ./game_asan --smoke && ./game_asan --localsmoke`.
asan: $(SOURCES)
	$(CXX) $(SOURCES) $(SANFLAGS) -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o game_asan

# Детерминированный soak-тест локального режима под ASan/UBSan (гоняет тысячи кадров,
# проверяет инварианты и покрывает радио/бой/смерть/луны). Собирает и сразу запускает.
soak: soak_test.cpp $(LIBSOURCES)
	$(CXX) soak_test.cpp $(LIBSOURCES) $(SANFLAGS) -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o soak_asan
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ./soak_asan

# Скриншот-харнес локального режима: рендерит кадры микромира в BMP через ПРОГРАММНЫЙ
# рендерер SDL (без окна), затем конвертит в PNG через macOS `sips`. Камера — тем же
# buildLocalCamera, что и игра. Артефакты (shot_test, *.bmp, *.png) не коммитятся.
shots: shot_test.cpp $(LIBSOURCES)
	$(CXX) shot_test.cpp $(LIBSOURCES) -O2 -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o shot_test
	SDL_VIDEODRIVER=dummy ./shot_test
	@for f in shot_*.bmp; do sips -s format png "$$f" --out "$${f%.bmp}.png" >/dev/null 2>&1 && echo "png: $${f%.bmp}.png"; done

# Скриншот-харнес под ASan/UBSan: покрывает попиксельные renderStarPlasma/renderBodySphere
# (sub-rect SDL_LockTexture, композит колец/сферы, расширенный bbox). Собирает и прогоняет все
# сценарии headless. Обязателен при правках localdraw.cpp. Артефакты не коммитятся.
shot_asan: shot_test.cpp $(LIBSOURCES)
	$(CXX) shot_test.cpp $(LIBSOURCES) $(SANFLAGS) -std=c++11 $(SDL_CFLAGS) $(SDL_LIBS) -o shot_test_asan
	SDL_VIDEODRIVER=dummy ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ./shot_test_asan

# Скриншот-харнес интерфейса: HUD и каждое окно в BMP через программный рендерер.
# Нужен для глазной проверки локализации — русский текст длиннее английского, и
# переполнение панели видно только на картинке. Артефакты не коммитятся.
uishots: ui_shot_test.cpp $(LIBSOURCES)
	$(CXX) ui_shot_test.cpp $(LIBSOURCES) -O2 -std=c++11 $(WARNFLAGS) $(SDL_CFLAGS) $(SDL_LIBS) -o ui_shot_test
	SDL_VIDEODRIVER=dummy ./ui_shot_test ru
	@for f in uishot_*.bmp; do sips -s format png "$$f" --out "$${f%.bmp}.png" >/dev/null 2>&1 && echo "png: $${f%.bmp}.png"; done

clean:
	rm -rf game game_asan soak_asan ui_click_test balance_test shot_test shot_test_asan ui_shot_test *.dSYM shot_*.bmp shot_*.png uishot_*.bmp uishot_*.png
