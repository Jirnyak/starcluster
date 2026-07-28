#include "game.h"
#include <algorithm>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Динамические рыночные события (plans_5).
// Данные-ориентированный код: события живут в Game::marketEvents, эффект — это
// транзиентный множитель Market::eventMul[idx]. Каждый тик множители затронутых
// рынков сбрасываются к 1.0 и пересобираются из активных событий, поэтому эффекты
// не накапливаются между тиками и не требуется обход всех 10000 рынков.
// ---------------------------------------------------------------------------

namespace {

// Kind — "обвал вниз" (перепроизводство / эпидемия) против "скачок вверх".
bool meIsCrash(MarketEventKind k) {
    return k == MarketEventKind::ProductionGlut || k == MarketEventKind::Plague;
}

const char* meKindLabel(MarketEventKind k) {
    switch (k) {
        case MarketEventKind::TechBoom:       return "TechBoom";
        case MarketEventKind::WarDemand:       return "WarDemand";
        case MarketEventKind::Famine:          return "Famine";
        case MarketEventKind::ProductionGlut:  return "ProductionGlut";
        case MarketEventKind::ResourceStrike:  return "ResourceStrike";
        case MarketEventKind::Plague:          return "Plague";
    }
    return "MarketEvent";
}

// Множитель, применяемый к цене элемента активным событием.
double meFactor(const MarketEvent& ev) {
    if (meIsCrash(ev.kind)) {
        double f = 1.0 / (ev.magnitude > 0.0 ? ev.magnitude : 1.0);
        return f < 0.2 ? 0.2 : f;   // явный, но ограниченный ход вниз
    }
    return ev.magnitude;            // ход вверх
}

// Добавить idx в список с лёгким де-дупом (стек внутри одного события нежелателен).
void mePushUnique(std::vector<int>& dst, int idx) {
    if (std::find(dst.begin(), dst.end(), idx) == dst.end())
        dst.push_back(idx);
}

} // namespace

// ---------------------------------------------------------------------------
void Game::spawnMarketEvent() {
    const int marketCount = int(markets.size());
    const int starCount = int(cluster.stars.size());
    if (marketCount <= 0) return;

    // Выбрать населённую звезду/рынок: до ~12 попыток.
    int s = -1;
    for (int attempt = 0; attempt < 12; ++attempt) {
        int cand = randomer(rng, marketCount - 1);
        if (cand >= 0 && cand < starCount && cand < marketCount &&
            cluster.stars[cand].population > 0.0 &&
            !markets[cand].prices.empty()) {
            s = cand;
            break;
        }
    }
    if (s < 0) return;

    MarketEvent ev;
    ev.star = s;
    ev.kind = MarketEventKind(randomer(rng, 5));
    ev.startTime = time;
    ev.endTime = time + (6.0 + randomer(rng, 12));   // 6..18 лет
    ev.announced = false;

    if (meIsCrash(ev.kind))
        ev.magnitude = 2.0 + (randomer(rng, 100) / 100.0) * 1.5;   // 2.0..3.5 (вниз как 1/mag)
    else
        ev.magnitude = 2.0 + (randomer(rng, 100) / 100.0) * 3.0;   // 2.0..5.0 (вверх)

    const std::vector<ElementDefinition>& defs = elementDefinitions();
    const int n = int(defs.size());

    // Сколько элементов затронуто.
    int count = (ev.kind == MarketEventKind::Plague) ? (3 + randomer(rng, 2))   // 3..5
                                                     : (2 + randomer(rng, 3));  // 2..5

    // Пул кандидатов по типу события.
    std::vector<int> pool;
    switch (ev.kind) {
        case MarketEventKind::TechBoom:        // проводники/катализаторы
            for (int i = 0; i < n; ++i)
                if (defs[i].conductorTrait > 0.40 || defs[i].catalystTrait > 0.40)
                    pool.push_back(i);
            break;
        case MarketEventKind::WarDemand:       // конструкционные металлы/делящееся топливо
            for (int i = 0; i < n; ++i)
                if (defs[i].structuralTrait > 0.40 || defs[i].fissionFuelTrait > 0.25)
                    pool.push_back(i);
            break;
        case MarketEventKind::Famine:          // лёгкие летучие (Z<=8)
            for (int i = 0; i < n; ++i)
                if (defs[i].atomicNumber <= 8)
                    pool.push_back(i);
            break;
        case MarketEventKind::ResourceStrike:  // добыча остановлена -> дефицит
        case MarketEventKind::ProductionGlut:  // перепроизводство -> обвал
            pool = cluster.stars[s].resourceFocus;
            break;
        case MarketEventKind::Plague:          // шок спроса — случайные элементы
            break;                             // pool пуст -> случайный фолбэк ниже
    }

    // Сэмплировать count индексов из пула.
    if (!pool.empty()) {
        for (int i = 0; i < count; ++i)
            mePushUnique(ev.elements, pool[randomer(rng, int(pool.size()) - 1)]);
    }
    // Фолбэк: случайные индексы, если категория/resourceFocus пусты (или Plague).
    if (ev.elements.empty() && n > 0) {
        for (int i = 0; i < count; ++i)
            mePushUnique(ev.elements, randomer(rng, n - 1));
    }

    marketEvents.push_back(ev);
    // Анонс произойдёт в updateMarketEvents при первой активации.
}

// ---------------------------------------------------------------------------
void Game::updateMarketEvents(double dt) {
    const int marketCount = int(markets.size());
    const int starCount = int(cluster.stars.size());

    // 1. Каденс спавна.
    marketEventTimer += dt;
    const double SPAWN_INTERVAL = 8.0;
    while (marketEventTimer >= SPAWN_INTERVAL) {
        marketEventTimer -= SPAWN_INTERVAL;
        int active = 0;
        for (auto& e : marketEvents)
            if (time >= e.startTime && time < e.endTime) ++active;
        if (active < 6) spawnMarketEvent();
    }

    // 2. Истечение: вернуть рынок к 1.0 и сообщить, затем удалить событие.
    for (auto& ev : marketEvents) {
        if (time < ev.endTime) continue;
        if (ev.star >= 0 && ev.star < marketCount) {
            Market& m = markets[ev.star];
            if (!m.eventMul.empty())
                std::fill(m.eventMul.begin(), m.eventMul.end(), 1.0);
        }
        if (ev.announced) {
            std::string where = (ev.star >= 0 && ev.star < starCount)
                                    ? cluster.stars[ev.star].name : std::string("space");
            pushNews(std::string(meKindLabel(ev.kind)) + " in " + where + " has passed", 1);
        }
    }
    marketEvents.erase(
        std::remove_if(marketEvents.begin(), marketEvents.end(),
                       [&](const MarketEvent& ev) { return time >= ev.endTime; }),
        marketEvents.end());

    // 3. Пересобрать активные события без накопления между тиками.
    //    3a. Уникальные рынки активных событий.
    std::vector<int> uniqueMarkets;
    for (auto& ev : marketEvents) {
        if (ev.startTime <= time && time < ev.endTime &&
            ev.star >= 0 && ev.star < marketCount)
            mePushUnique(uniqueMarkets, ev.star);
    }
    //    3b. Сброс eventMul каждого затронутого рынка РОВНО один раз.
    for (int m : uniqueMarkets) {
        Market& mk = markets[m];
        if (mk.eventMul.size() != mk.prices.size())
            mk.eventMul.assign(mk.prices.size(), 1.0);
        else
            std::fill(mk.eventMul.begin(), mk.eventMul.end(), 1.0);
    }
    //    3c. Наложить множители всех активных событий + разовый анонс.
    for (auto& ev : marketEvents) {
        if (!(ev.startTime <= time && time < ev.endTime)) continue;
        if (ev.star < 0 || ev.star >= marketCount) continue;
        Market& mk = markets[ev.star];
        const double factor = meFactor(ev);
        for (size_t k = 0; k < ev.elements.size(); ++k) {
            int idx = ev.elements[k];
            if (idx >= 0 && size_t(idx) < mk.eventMul.size())
                mk.eventMul[idx] *= factor;
        }
        if (!ev.announced) {
            std::string where = (ev.star < starCount)
                                    ? cluster.stars[ev.star].name : std::string("a colony");
            std::string msg = std::string(meKindLabel(ev.kind)) + " at " + where +
                              (meIsCrash(ev.kind) ? ": prices crash" : ": prices surge");
            pushNews(msg, 1);
            ev.announced = true;
        }
    }
}
