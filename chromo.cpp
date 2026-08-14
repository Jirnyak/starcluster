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

    // Запекаем в поля кораблей статы, которые читает легаси-код. Закон
    // запекания — общий с пересадкой на новый корпус (shipApplyChromocoreFactors),
    // сюда он приходит с приростом ровно одного ядра.
    //
    // ⚠️ (§51) ПО ВСЕМУ ФЛОТУ, а не только по борту под штурвалом. Здесь стоял
    // один `agents[playerAgent].ship`, и это молча расслаивало флот: ядро,
    // добытое капитаном, доставалось только его нынешнему корпусу, а остальные
    // борта игрока навсегда оставались на том множителе, с которым были куплены.
    // Расхождение копилось невидимо — панель `SHIP SYSTEMS` показывает ОДИН
    // набор множителей на игрока (`TechState`), то есть обещала прокачку,
    // которой у половины флота не было. Ровно та же мысль уже записана в
    // `rebakeBakedBonuses`: хромоядра — собственность ИГРОКА, а не корпуса.
    const double f = 1.0 + step;
    for (size_t i = 0; i < agents.size(); ++i) {
        if (!agents[i].playerControlled) continue;
        Ship& ship = agents[i].ship;
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

// (§52) ЯДРО ИДЁТ ЗА ЗАНЯТИЕМ, А НЕ ЗА БРОСКОМ КОСТИ.
//
// Развилка стояла так: шесть источников исследований из семи выдавали
// СЛУЧАЙНЫЙ стат (1/7 на каждый), а единственный осознанный выбор — кузница —
// стоил около 7.56 млн Cr товара и появлялся заведомо позже, чем был нужен
// (замер §51: состояние за 60 рейсов — 5.3e5..1.45e6). Крючка не было: ядро
// нельзя было ни ждать, ни направить.
//
// Решено: стат следующего ядра определяет то, ЧЕМ игрок его заработал — «умеешь
// то, что практикуешь». Семь занятий закрывают ровно семь статов, и ни одно не
// осталось без своего:
//   разведка рынков -> интеллект      торговля        -> харизма
//   добыча и подбор -> материалы      бой в системе   -> тактика
//   сам полёт       -> кинематика     радиоисточники  -> сенсоры
//   аномалии        -> удача
// Аномалия достаётся удаче не по остаточному принципу: это единственное
// занятие в игре, где исход и есть чистое везение.
//
// Ни одного нового числа: выбирается ПРОСТО НАИБОЛЬШЕЕ накопленное. Ничьи и
// пустой список решаются в пользу прежнего поведения — если игрок с прошлого
// ядра не занимался ничем приписанным, стат по-прежнему берётся из `coreRng`,
// то есть пассивный путь остался ровно таким, каким был.
//
// ⚠️ ЦЕНА ОТМЕНЫ: убрать второй аргумент (он со значением по умолчанию, все 17
// точек вызова переживут), вектор `researchLean` и ветку argmax ниже; поле в
// сейве версии 21 останется мёртвым грузом.
void Game::addResearch(double amount, int stat) {
    if (amount <= 0.0) return;
    tech.research += amount * std::max(1.0, tech.intellect);
    if (researchLean.size() != size_t(TECH_STAT_COUNT)) {
        researchLean.assign(size_t(TECH_STAT_COUNT), 0.0);
    }
    if (stat >= 0 && stat < TECH_STAT_COUNT) researchLean[size_t(stat)] += amount;
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
        // Наибольшее накопленное занятие и решает. Ничья достаётся младшему
        // индексу — это не «случайность», а порядок, и он детерминирован.
        int best = -1;
        double bestLean = 0.0;
        for (int k = 0; k < TECH_STAT_COUNT; ++k) {
            if (researchLean[size_t(k)] > bestLean) { bestLean = researchLean[size_t(k)]; best = k; }
        }
        const int granted = best >= 0 ? best : randomer(coreRng, TECH_STAT_COUNT - 1);
        // Счётчик обнуляется ядром: следующее ядро смотрит только на то, чем
        // игрок занимался ПОСЛЕ этого. Иначе первые сто часов решали бы всё.
        researchLean.assign(size_t(TECH_STAT_COUNT), 0.0);
        grantChromocore(granted);
    }
}
