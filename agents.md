# Starcluster: Core Ideas

* Local globular cluster: thousands of stars, distances < 1 light year
* Cluster radius: 10–100 light years
* Physical hard sci-fi: no FTL, no jumps, travel between stars takes years, acceleration/deceleration
* Max speed ~0.5c, typical ships ~0.1c
* Trade sim: local prices, no global prices, demand mechanics
* Table of elements: hydrogen, helium, lithium, etc.
* Supply/demand market
* Each colony needs resources as function of population and role (production/army/government/shipyard, etc.)
* Resource spread: semi-realistic, procedural, as in real globular clusters (old/young stars, nebulas, random noise)
* Faction system: star can be occupied by faction or free
* Agents: each faction has ships, also free merchants; they trade via stars (expandable war mechanics: fleets fight and take over systems)
* All local and relativistic, taxes, tariffs, etc.
* Player is not unique: same trade ship, can upgrade ship, found colony (expensive), late game own faction, hire workers/traders
* Universal, elegant, minimal, modular, expandable, minimal code, maximal functionality

# План слияния и миграции Starcluster

## Цель
Объединить симуляционную (экономика, агенты, кластер) и графическую (SDL2, визуализация) части в единый рабочий проект на C++ с SDL2.

## Основные шаги

1. **Очистка cluster.cpp**
  - Оставить только реализацию ClusterStar и Cluster (генерация звёзд, без SDL, без main, без render_text, без старых классов star).
  - Удалить весь SDL-код, main, функции, enum, randomer и т.д.

2. **SDL2 и рендеринг только в main.cpp**
  - Весь код, связанный с SDL2, окном, рендерингом, обработкой событий — только в main.cpp.
  - В main.cpp использовать данные из game.cluster.stars и game.agents для отрисовки.

3. **Единая точка входа**
  - main.cpp: инициализация SDL2, создание Game, игровой цикл, вызов game.update(), отрисовка.
  - Вся логика симуляции — через Game и связанные классы.

4. **Удаление дубликатов**
  - Проверить, что нет дублирующихся определений классов (star, ClusterStar, main, render_text и т.д.)
  - Удалить или переименовать старые файлы, если они мешают сборке.

5. **Проверка Makefile**
  - Убедиться, что Makefile собирает только нужные файлы (main.cpp, game.cpp, cluster.cpp, resource.cpp, colony.cpp, faction.cpp, ship.cpp, agent.cpp, market.cpp).

6. **Тестовая сборка**
  - После очистки и слияния — собрать проект, убедиться в отсутствии ошибок.
  - Проверить запуск: окно SDL2, отображение звёзд и кораблей, симуляция работает.

## Результат
- Единый рабочий проект Starcluster с SDL2-графикой и симуляцией, без конфликтов и дубликатов.
- Вся логика симуляции и рендеринга разделена по файлам: main.cpp (SDL2, рендер), cluster.cpp (кластер), game.cpp (логика), остальные — по сущностям.
# Agents (Агенты)

В игре Starcluster агенты — это автономные субъекты, действующие в рамках симуляции. Каждый агент может быть кораблём, торговцем, военным, представителем фракции или свободным игроком. Система агентов модульная и расширяемая.

## Основные типы агентов

- **Торговец (Trader):**
  - Перевозит ресурсы между звёздами и колониями.
  - Ориентируется на локальные цены, спрос и предложение.
  - Может быть независимым или принадлежать фракции.

- **Военный (Military):**
  - Выполняет задачи по защите, нападению, патрулированию.
  - Может участвовать в захвате систем и войнах фракций.

- **Колонист (Colonist):**
  - Перевозит людей и оборудование для основания новых колоний.
  - Может быть нанят игроком или фракцией.

- **Флот (Fleet):**
  - Группа кораблей, действующих совместно (например, военный флот).

- **Свободный агент (Free Agent):**
  - Не принадлежит ни одной фракции, действует по своим интересам.

- **Игрок (Player):**
  - Управляет кораблём, может торговать, апгрейдить корабль, основывать колонии, создавать фракцию.

## Ключевые свойства агента
- Тип (роль)
- Владение (фракция или свободный)
- Корабль (или флот)
- Текущая задача/маршрут
- Запасы/груз
- Финансы
- Стратегия поведения (логика)

## Принципы
- Все агенты действуют по единым правилам симуляции.
- Игрок — не уникален, а один из агентов.
- Агенты могут взаимодействовать: торговать, воевать, заключать союзы.
- Система легко расширяется новыми типами агентов и логикой поведения.

## Примеры расширения
- Наёмники, шпионы, дипломаты, исследователи.
- Автоматизация торговли, войны, дипломатии.

---

Документ предназначен для проектирования и расширения системы агентов в Starcluster. Все механики должны быть минимальными, универсальными и модульными.