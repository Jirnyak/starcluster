// Балансовый стенд экономики замещения (make econ).
// Печатает матрицу способностей, опорные цены, поведение рынка и волну
// замещения. Никакого SDL — чистые числа, чтобы судить о модели до плейтеста.
#include "econ.h"
#include "market.h"
#include "resource.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Row {
    int index;
    double value;
};

bool byValueDesc(const Row& a, const Row& b) { return a.value > b.value; }

void printCandidates() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::printf("\n=== 1. МАТРИЦА СПОСОБНОСТЕЙ: ТОП-10 КАНДИДАТОВ ПО ФУНКЦИЯМ ===\n");
    for (int f = 0; f < EF_COUNT; ++f) {
        std::vector<Row> rows;
        for (size_t i = 0; i < elements.size(); ++i) {
            const double q = econQuality(int(i), f);
            if (q > 0.008) rows.push_back(Row{int(i), q});
        }
        std::sort(rows.begin(), rows.end(), byValueDesc);
        std::printf("%-3s %-14s [%3d кандидатов] ", econFunctionCode(f), econFunctionName(f), int(rows.size()));
        for (size_t k = 0; k < rows.size() && k < 10; ++k) {
            std::printf("%s %.2f  ", elements[rows[k].index].symbol, rows[k].value);
        }
        std::printf("\n");
    }
}

void printPrimaryMap() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::printf("\n=== 2. ГЛАВНАЯ ФУНКЦИЯ КАЖДОГО ЭЛЕМЕНТА (что напишем в клетке) ===\n");
    int junk = 0;
    for (size_t i = 0; i < elements.size(); ++i) {
        const int f = econPrimaryFunction(int(i));
        if (f < 0) ++junk;
        std::printf("%-3s%-4s ", elements[i].symbol, f >= 0 ? econFunctionCode(f) : "---");
        if ((i + 1) % 10 == 0) std::printf("\n");
    }
    std::printf("\nБЕСПОЛЕЗНЫХ ЭЛЕМЕНТОВ (нет ни одной функции): %d из %d\n", junk, int(elements.size()));
}

void printReferencePrices() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::vector<Row> rows;
    for (size_t i = 0; i < elements.size(); ++i) {
        rows.push_back(Row{int(i), marketReferencePrice(int(i))});
    }
    std::sort(rows.begin(), rows.end(), byValueDesc);
    std::printf("\n=== 3. ОПОРНАЯ ЦЕНА СКОПЛЕНИЯ (типичный рынок) ===\n");
    std::printf("ДОРОГИЕ:  ");
    for (int k = 0; k < 12; ++k) {
        const int idx = rows[size_t(k)].index;
        std::printf("%s(%s) %.0f  ", elements[idx].symbol,
                    econPrimaryFunction(idx) >= 0 ? econFunctionCode(econPrimaryFunction(idx)) : "---",
                    rows[size_t(k)].value);
    }
    std::printf("\nДЕШЁВЫЕ:  ");
    for (int k = 0; k < 12; ++k) {
        const int idx = rows[rows.size() - 1 - size_t(k)].index;
        std::printf("%s(%s) %.2f  ", elements[idx].symbol,
                    econPrimaryFunction(idx) >= 0 ? econFunctionCode(econPrimaryFunction(idx)) : "---",
                    rows[rows.size() - 1 - size_t(k)].value);
    }
    std::printf("\n");
}

// Синтетическая система: обилие × насыщенность, с «карманом» богатства.
std::vector<Resource> makeResources(double richness, double metallicity, int richElement, double richMul) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::vector<Resource> out;
    out.reserve(elements.size());
    for (size_t i = 0; i < elements.size(); ++i) {
        const ElementDefinition& e = elements[i];
        const double lowMass = 1.0 / std::sqrt(std::max(1.0, e.atomicMass));
        const double industrial = std::min(1.0, e.metallicTrait * 0.5 + e.structuralTrait * 0.35 + e.conductorTrait * 0.25);
        const double volatile_ = std::min(1.0, lowMass * (0.7 + e.oxidizerTrait + e.reducerTrait + e.fusionFuelTrait));
        double amount = e.abundanceWeight * richness;
        amount *= 0.35 + metallicity * industrial + (1.0 - metallicity) * volatile_;
        if (int(i) == richElement) amount *= richMul;
        out.emplace_back(e.symbol, std::max(0.001, amount));
    }
    return out;
}

void printMarketSnapshot(const char* title, Market& m) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::printf("\n--- %s (роль %s, strain %.0f%%) ---\n", title, m.role.c_str(), m.strain * 100.0);
    std::printf("НУЖДЫ: ");
    for (int f = 0; f < EF_COUNT; ++f) std::printf("%s %.1f  ", econFunctionCode(f), m.needs[f]);
    std::printf("\n");

    std::vector<Row> rows;
    for (size_t i = 0; i < m.demandRate.size(); ++i) rows.push_back(Row{int(i), m.demandRate[i]});
    std::sort(rows.begin(), rows.end(), byValueDesc);
    std::printf("%-4s %-4s %9s %9s %7s %9s %9s\n", "SYM", "FUN", "PRICE", "REF", "P/REF", "DEMAND/Y", "COVER,Y");
    for (int k = 0; k < 14; ++k) {
        const int i = rows[size_t(k)].index;
        const int f = econPrimaryFunction(i);
        const double ref = marketReferencePrice(i);
        std::printf("%-4s %-4s %9.1f %9.1f %7.2f %9.2f %9.1f\n",
                    elements[i].symbol, f >= 0 ? econFunctionCode(f) : "---",
                    m.prices[i], ref, ref > 0.0 ? m.prices[i] / ref : 0.0,
                    m.demandRate[i], std::min(999.0, m.coverageYears(i)));
    }
}

void printSubstitutionWave() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::printf("\n=== 5. ВОЛНА ЗАМЕЩЕНИЯ: ДЕФИЦИТ ЛИДЕРА ФУНКЦИИ STRUCT ===\n");

    Market m;
    std::vector<double> bias(elements.size(), 1.0);
    m.seed(makeResources(1.0, 0.6, -1, 1.0), bias, "shipyard", 12000.0, 1.6);

    std::vector<Row> rows;
    for (size_t i = 0; i < elements.size(); ++i) {
        const double s = m.marketShare(int(i), EF_STRUCT);
        if (s > 0.001) rows.push_back(Row{int(i), s});
    }
    std::sort(rows.begin(), rows.end(), byValueDesc);
    const int watch = std::min(6, int(rows.size()));

    std::printf("ГОД  strain ");
    for (int k = 0; k < watch; ++k) std::printf("| %-4s цена доля ", elements[rows[size_t(k)].index].symbol);
    std::printf("\n");

    const int leader = rows[0].index;
    for (int year = 0; year <= 120; year += 10) {
        if (year > 0) {
            for (int t = 0; t < 10; ++t) {
                // Дефицит: с 40-го года лидера выгребают вдвое быстрее производства.
                if (year > 40) m.supply[size_t(leader)].amount *= 0.55;
                m.update(1.0);
            }
        }
        std::printf("%4d %5.0f%% ", year, m.strain * 100.0);
        for (int k = 0; k < watch; ++k) {
            const int i = rows[size_t(k)].index;
            std::printf("| %9.1f %4.0f%% ", m.prices[i], m.marketShare(i, EF_STRUCT) * 100.0);
        }
        std::printf("\n");
    }
    std::printf("(с 40-го года запас %s выгребается — смотри, как доля уходит к суррогатам)\n",
                elements[leader].symbol);
}

void printSpatialSpread() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    std::printf("\n=== 6. ПРОСТРАНСТВЕННЫЙ РАЗБРОС ЦЕН (что и есть игра) ===\n");
    const char* roles[] = {"habitat", "refinery", "shipyard", "research", "military", "frontier"};

    std::vector<Market> markets;
    for (int r = 0; r < 6; ++r) {
        for (int variant = 0; variant < 3; ++variant) {
            Market m;
            std::vector<double> bias(elements.size(), 1.0);
            const double richness = 0.5 + variant * 0.8;
            const double metallicity = 0.25 + variant * 0.3;
            const int rich = variant == 0 ? 25 : (variant == 1 ? 7 : 78);
            m.seed(makeResources(richness, metallicity, rich, 22.0), bias, roles[r], 6000.0 + variant * 9000.0, 0.6 + variant * 1.1);
            for (int y = 0; y < 30; ++y) m.update(1.0);
            markets.push_back(m);
        }
    }

    std::printf("%-4s %-4s %9s %9s %8s  %s\n", "SYM", "FUN", "МИН", "МАКС", "РАЗБРОС", "СПРЕД %");
    std::vector<Row> interesting;
    for (size_t i = 0; i < elements.size(); ++i) {
        double lo = 1e30, hi = 0.0;
        for (size_t k = 0; k < markets.size(); ++k) {
            lo = std::min(lo, markets[k].prices[i]);
            hi = std::max(hi, markets[k].prices[i]);
        }
        if (lo <= 0.0) continue;
        interesting.push_back(Row{int(i), hi / lo});
    }
    std::sort(interesting.begin(), interesting.end(), byValueDesc);
    for (int k = 0; k < 15; ++k) {
        const int i = interesting[size_t(k)].index;
        double lo = 1e30, hi = 0.0;
        for (size_t j = 0; j < markets.size(); ++j) {
            lo = std::min(lo, markets[j].prices[i]);
            hi = std::max(hi, markets[j].prices[i]);
        }
        const int f = econPrimaryFunction(i);
        std::printf("%-4s %-4s %9.2f %9.2f %8.1fx  %+.0f%%\n",
                    elements[i].symbol, f >= 0 ? econFunctionCode(f) : "---",
                    lo, hi, hi / lo, (hi / lo - 1.0) * 100.0);
    }

    double strainSum = 0.0, strainMax = 0.0;
    for (size_t k = 0; k < markets.size(); ++k) {
        strainSum += markets[k].strain;
        strainMax = std::max(strainMax, markets[k].strain);
    }
    std::printf("STRAIN: средний %.0f%%, максимум %.0f%% по %d системам\n",
                strainSum / double(markets.size()) * 100.0, strainMax * 100.0, int(markets.size()));
}

} // namespace

int main() {
    printCandidates();
    printPrimaryMap();
    printReferencePrices();

    std::printf("\n=== 4. ЖИВОЙ РЫНОК ===\n");
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    {
        Market m;
        std::vector<double> bias(elements.size(), 1.0);
        m.seed(makeResources(1.2, 0.75, 25, 30.0), bias, "shipyard", 14000.0, 1.8);
        for (int y = 0; y < 40; ++y) m.update(1.0);
        printMarketSnapshot("ВЕРФЬ, БОГАТАЯ ЖЕЛЕЗОМ", m);
    }
    {
        Market m;
        std::vector<double> bias(elements.size(), 1.0);
        m.seed(makeResources(0.6, 0.2, 7, 12.0), bias, "habitat", 20000.0, 0.7);
        for (int y = 0; y < 40; ++y) m.update(1.0);
        printMarketSnapshot("ЖИЛАЯ СИСТЕМА, БЕДНАЯ МЕТАЛЛАМИ", m);
    }

    printSubstitutionWave();
    printSpatialSpread();
    std::printf("\n");
    return 0;
}
