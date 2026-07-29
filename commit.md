# Triple Build Instructions (Mac, Windows, Linux)

Для создания полноценного мультиплатформенного релиза («тройного билда») для MyIndie, необходимо выполнить следующие шаги в корневой папке проекта:

## 1. Сборка для Mac (macOS)
Сначала компилируем нативный бинарник игры для Mac:
```bash
make clean && make -j4 game
```
Затем собираем из готового шаблона `test_app` образ диска (DMG):
- Скопировать `game` в `test_app/Contents/MacOS/Starcluster`
- Привязать локальные библиотеки SDL2 через `install_name_tool` к бандлу `test_app`
- Упаковать бандл:
```bash
hdiutil create -volname Starcluster -srcfolder test_app -ov -format UDZO Starcluster.dmg
```

## 2. Сборка для Windows (через кросс-компилятор MinGW)
Компилируем `starcluster.exe` с использованием локальных библиотек `mingw_dev_lib`:
```bash
x86_64-w64-mingw32-g++ main.cpp game.cpp cluster.cpp resource.cpp market.cpp ship.cpp agent.cpp colony.cpp faction.cpp ui.cpp mining.cpp combat.cpp spaceevents.cpp anomaly.cpp modules.cpp chromo.cpp render2d.cpp localgen.cpp localsim.cpp localdraw.cpp -O3 -std=c++11 -I mingw_dev_lib/include/SDL2 -L mingw_dev_lib/lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -o starcluster.exe
```
Для работы Windows-версии в архиве также должна лежать библиотека `SDL2.dll`.

## 3. Сборка для Linux
Поскольку на Маке нет локального кросс-компилятора `x86_64-linux-gnu-g++`, бинарник `starcluster-linux` берется из эталонного архива:
`/Users/jirnyak/Downloads/starcluster_1782511656478/starcluster-linux`
(Либо компилируется вручную на Linux-системе/в Docker).

## 4. Итоговый архив
Все 4 файла складываются в одну папку и запаковываются в ZIP-архив для публикации на MyIndie:
1. `Starcluster.dmg`
2. `starcluster.exe`
3. `SDL2.dll`
4. `starcluster-linux`
