#include "game.h"
#include "ship.h"
#include <algorithm>
#include <string>

// Хромокоры: получение ядер и накопление исследований (design.md §2).
// Множители >= 1.0. Материалы/кинематика/тактика "запекаются" в поля корабля
// игрока (чтобы легаси-код их видел); остальные читаются в точке использования.

namespace {
// ⚠️ ЕДИНСТВЕННЫЙ словарь названий статов. У одного и того же стата их было
// ТРИ: кузница экзотики писала на кнопке `KIN`, панель игрока — `SPEED`, а
// новость о полученном ядре — `KINEMATICS`. Игрок выбирает стат в кузнице
// осознанно (§31: кузница даёт ядро ВЫБРАННОГО стата, а не рулетку), и выбирать
// он должен из тех же слов, которыми игра потом отчитывается. Слова взяты
// короткие: их держит и кнопка кузницы (79 px), и колонка панели (78 px).
const char* STAT_NAMES[TECH_STAT_COUNT] = {
    "MIND", "CHARM", "FRAME", "TACTICS", "SPEED", "SENSOR", "LUCK"
};
double& statRef(TechState& t, int stat) {
    switch (stat) {
        case TECH_INTELLECT: return t.intellect;
        case TECH_CHARISMA: return t.charisma;
        case TECH_MATERIALS: return t.materials;
        case TECH_TACTICS: return t.tactics;
        case TECH_KINEMATICS: return t.kinematics;
        case TECH_SENSORS: return t.sensors;
        default: return t.luck;
    }
}
}

const char* chromocoreStatLabel(int stat) {
    if (stat < 0 || stat >= TECH_STAT_COUNT) return "CORE";
    return STAT_NAMES[stat];
}

void Game::grantChromocore(int stat) {
    if (stat < 0 || stat >= TECH_STAT_COUNT) stat = TECH_INTELLECT;
    tech.cores += 1;
    const double step = 0.06;
    double& s = statRef(tech, stat);
    s *= (1.0 + step);

    // Запекаем в поля корабля игрока статы, которые читает легаси-код. Закон
    // запекания — общий с пересадкой на новый корпус (shipApplyChromocoreFactors),
    // сюда он приходит с приростом ровно одного ядра.
    if (playerAgent >= 0 && playerAgent < int(agents.size())) {
        Ship& ship = agents[playerAgent].ship;
        const double f = 1.0 + step;
        shipApplyChromocoreFactors(ship,
                                   stat == TECH_MATERIALS ? f : 1.0,
                                   stat == TECH_TACTICS ? f : 1.0,
                                   stat == TECH_KINEMATICS ? f : 1.0);
        if (stat == TECH_MATERIALS) ship.hullHP = ship.maxHullHP;
    }

    const std::string label = chromocoreStatLabel(stat);
    lastEvent = "CHROMOCORE +" + label;
    pushNews("Chromocore attuned: " + label + " model refined", 4);
}

void Game::rebakePlayerBakedBonuses() {
    rebakeBakedBonuses(playerAgent);
}

// ⚠️ ПО НОМЕРУ БОРТА, а не «у капитана». Переоснастка живёт НА КОРПУСЕ (§31.4),
// значит стирает её `shipApplyClass` у любого борта, а не только у того, за
// штурвалом которого игрок. `buyShip` звал перезапекание только при
// `agentIndex == playerAgent`: борт флота с тремя слоями нейтрониума, купивший
// новый корпус, терял броню 365 -> 20, а `platingLayers` оставался 3 — и
// загрузка это закрепляла. Хромоядра — тоже собственность ИГРОКА, поэтому
// перезапекаем только его борта.
void Game::rebakeBakedBonuses(int agentIndex) {
    if (agentIndex < 0 || agentIndex >= int(agents.size())) return;
    if (!agents[size_t(agentIndex)].playerControlled) return;
    Ship& ship = agents[size_t(agentIndex)].ship;
    shipApplyChromocoreFactors(ship, tech.materials, tech.tactics, tech.kinematics);
    // Хайтек-переоснастка (§31.4) запекается тем же порядком и по той же
    // причине: ёмкость ячейки, броня, корпус и масса — это ОТРАЖЕНИЕ ступеней
    // `containmentLevel`/`platingLayers`, и новый корпус это отражение стирает.
    // Сами ступени живут на корабле, потому и переживают пересадку игрока.
    ship.containment = double(ship.containmentLevel) * CONTAINMENT_STEP_UNITS;
    if (ship.platingLayers > 0) {
        ship.armor += PLATING_ARMOR_PER_LAYER * ship.platingLayers;
        ship.maxHullHP += PLATING_HULL_PER_LAYER * ship.platingLayers;
        ship.dryMass += PLATING_MASS_PER_LAYER * ship.platingLayers;
    }
    ship.hullHP = std::min(ship.hullHP, ship.maxHullHP);
    // Ячейка могла ужаться (капсула после гибели) — лишнее вещество выпадает
    // из удержания. Аннигилировать его на борту было бы честнее физически, но
    // игрока это убивало бы без предупреждения; считаем, что его сбрасывают.
    double held = 0.0;
    for (int k = 0; k < EX_COUNT; ++k) held += ship.exotic[k];
    if (held > ship.containment && held > 0.0) {
        const double keep = ship.containment / held;
        for (int k = 0; k < EX_COUNT; ++k) ship.exotic[k] *= keep;
    }
}

void Game::addResearch(double amount) {
    if (amount <= 0.0) return;
    tech.research += amount * std::max(1.0, tech.intellect);
    for (;;) {
        const double threshold = 100.0 + tech.cores * 40.0;
        if (tech.research < threshold) break;
        tech.research -= threshold;
        // §2.3: `addResearch` зовут ОБА слоя — макро (торговля, разведка,
        // аномалии) и локальный режим (убийства, добыча, радио). Тянуть здесь
        // из глобального `rng` значило бы, что бой в микромире сдвигает поток
        // всей макро-симуляции: тот же сид давал бы разные миры в зависимости
        // от того, летал ли игрок вручную. Поэтому выбор стата берётся из
        // СВОЕГО потока, засеянного сидом мира и НОМЕРОМ ядра. Это так же
        // детерминировано (тот же сид -> та же последовательность ядер),
        // ничего не стоит в сейве (номер ядра уже там) и не трогает `rng`.
        std::mt19937 coreRng(static_cast<unsigned int>(seed) * 2654435761u +
                             static_cast<unsigned int>(tech.cores) * 40503u + 7919u);
        const int stat = randomer(coreRng, TECH_STAT_COUNT - 1);
        grantChromocore(stat);
    }
}
