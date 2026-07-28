#include "game.h"
#include <string>
#include <vector>

// Ручная добыча руды у звезды, пока корабль пристыкован (plans_7).
// Медленный стабильный ручеёк: немного груза + подпитка исследований.
// См. game.h для API. Стиль: C-style, данные, без новых классов.

bool Game::playerToggleMining() {
    if (playerAgent < 0 || playerAgent >= (int)agents.size()) return false;
    Agent& p = agents[playerAgent];
    if (p.ship.enRoute) {
        lastEvent = "cannot mine in transit";
        return false;
    }
    if (playerMining) {
        playerMining = false;
        pushNews("Mining halted", 0);
        return true;
    }
    // Старт добычи — нужна валидная текущая звезда.
    if (p.currentStar < 0 || p.currentStar >= (int)cluster.stars.size()) return false;
    playerMining = true;
    miningStar = p.currentStar;
    miningTimer = 0.0;
    miningYieldAccum = 0.0;
    pushNews("Mining started at " + cluster.stars[miningStar].name, 0);
    return true;
}

void Game::updateMining(double dt) {
    if (!playerMining) return;

    if (playerAgent < 0 || playerAgent >= (int)agents.size()) {
        playerMining = false;
        pushNews("Mining interrupted", 0);
        return;
    }
    Agent& p = agents[playerAgent];
    if (p.ship.enRoute || p.currentStar != miningStar ||
        miningStar < 0 || miningStar >= (int)cluster.stars.size()) {
        playerMining = false;
        pushNews("Mining interrupted", 0);
        return;
    }

    const ClusterStar& star = cluster.stars[miningStar];
    const std::vector<ElementDefinition>& elems = elementDefinitions();
    const int ecount = (int)elementCount();
    if (ecount <= 0) {
        playerMining = false;
        return;
    }

    miningTimer += dt;
    const double CHUNK = 0.15; // лет на один кусок руды

    while (miningTimer >= CHUNK) {
        miningTimer -= CHUNK;

        // --- Выбор элемента руды ---
        int idx;
        if (!star.resourceFocus.empty()) {
            idx = star.resourceFocus[randomer(rng, int(star.resourceFocus.size()) - 1)];
        } else {
            // Сканируем определения элементов; берём первый металлический
            // со смещением по RNG. Фолбэк — idx 25 (Fe-ish).
            idx = 25;
            int offset = randomer(rng, ecount - 1);
            for (int scan = 0; scan < ecount; ++scan) {
                int j = (offset + scan) % ecount;
                if (elems[j].metallicTrait > 0.6) { idx = j; break; }
            }
        }
        if (idx < 0) idx = 0;
        if (idx >= ecount) idx = ecount - 1;

        // --- Объём куска (держим малым, ~1..3 массы) ---
        double amt = (1.2 + 0.4 * star.miningRichness) *
                     (0.85 + 0.30 * (randomer(rng, 100) / 100.0));
        amt *= (1.0 + p.ship.utility * 0.01);
        amt *= (1.0 + (tech.luck - 1.0) * 0.10); // маленький бонус удачи (no-op при luck==1)

        // --- Проверка вместимости трюма ---
        double unit = resourceUnitMassByIndex(idx);
        double addMass = amt * unit;
        if (shipCargoMass(p.ship) + addMass > p.ship.cargoCapacity) {
            playerMining = false;
            pushNews("Cargo hold full — mining stopped", 0);
            return;
        }

        // --- Добавляем в груз ---
        std::string sym(elems[idx].symbol);
        bool found = false;
        for (size_t i = 0; i < p.ship.cargo.size(); ++i) {
            if (p.ship.cargo[i].element == sym) {
                p.ship.cargo[i].amount += amt;
                found = true;
                break;
            }
        }
        if (!found) p.ship.cargo.emplace_back(sym, amt);

        miningYieldAccum += amt;
        addResearch(0.5); // подпитка исследований

        // --- Редкий бонус у пульсара/нейтронной звезды ---
        if (star.stellarClass == 1 && randomer(rng, 99) < 4) {
            // Ищем самый дорогой (по базовой цене) элемент.
            int bestIdx = idx;
            double bestPrice = -1.0;
            for (int j = 0; j < ecount; ++j) {
                if (elems[j].basePrice > bestPrice) {
                    bestPrice = elems[j].basePrice;
                    bestIdx = j;
                }
            }
            double bonusAmt = 0.5;
            double bonusMass = bonusAmt * resourceUnitMassByIndex(bestIdx);
            if (shipCargoMass(p.ship) + bonusMass <= p.ship.cargoCapacity) {
                std::string bsym(elems[bestIdx].symbol);
                bool bfound = false;
                for (size_t i = 0; i < p.ship.cargo.size(); ++i) {
                    if (p.ship.cargo[i].element == bsym) {
                        p.ship.cargo[i].amount += bonusAmt;
                        bfound = true;
                        break;
                    }
                }
                if (!bfound) p.ship.cargo.emplace_back(bsym, bonusAmt);
                addResearch(6.0);
                pushNews("Exotic matter siphoned from pulsar", 3);
            }
        }

        p.lastAction = "mining";
    }
}
