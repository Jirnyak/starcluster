#include "market.h"
#include "econ.h"
#include <algorithm>
#include <cmath>

Market::Market() {}

namespace {

const double EPSV = 1e-9;
const double KAPPA = 0.34;        // скорость релаксации цены
const double PRICE_FLOOR_K = 0.04;
const double PRICE_CEIL_K = 220.0;

double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double clamp01(double v) { return clamp(v, 0.0, 1.0); }

// Денежный уровень СКОПЛЕНИЯ. Доли замещения зависят только от ОТНОСИТЕЛЬНЫХ
// цен, поэтому номинальный уровень ничем не закреплён и свободно дрейфует.
// Закрепляем его ОДИН раз на всю валюту — медленным средним по рынкам, — а не
// притяжением каждого элемента к «справедливой цене»: иначе такая привязка
// затирает главное, что должно двигать цену, — РЕДКОСТЬ.
double gClusterServiceCost = -1.0;
double gLevelDrive = 0.0;
bool gBuildingReference = false;   // защита от рекурсии при построении опорных цен

// Фоновая торговля остального скопления. Ни одна система не платит за товар
// сколь угодно много: если местная цена сильно выше общей, туда идёт чужой
// караван, и наоборот — местный избыток скупают. Ширина «вилки» — это цена
// доставки: у крупной связной системы она узкая, у глухого фронтира широкая,
// поэтому большой куш ждёт именно на отшибе.
double importBandDrive(size_t elementIndex, double price, double tradeAccess) {
    if (gBuildingReference) return 0.0;
    const double reference = marketReferencePrice(int(elementIndex));
    if (reference <= 0.0 || price <= 0.0) return 0.0;
    const double wedge = 1.0 + 1.15 / std::max(0.08, tradeAccess);   // во сколько раз возят дороже
    const double ceilingPrice = reference * wedge;
    const double floorPrice = reference / wedge;
    if (price > ceilingPrice) return clamp(std::log(ceilingPrice / price), -2.5, 0.0);
    if (price < floorPrice) return clamp(std::log(floorPrice / price), 0.0, 2.5);
    return 0.0;
}

void observeServiceLevel(double marketServiceCost) {
    if (marketServiceCost <= 0.0) return;
    if (gClusterServiceCost < 0.0) gClusterServiceCost = marketServiceCost;
    else gClusterServiceCost += (marketServiceCost - gClusterServiceCost) * 0.004;
    gLevelDrive = clamp(std::log(ECON_CREDITS_PER_SERVICE / std::max(0.01, gClusterServiceCost)), -1.2, 1.2);
}

// Пределы — только страховка от вырождения, не инструмент баланса.
double priceFloor(size_t elementIndex) {
    const double anchor = econAnchorPrice(int(elementIndex));
    return anchor > 0.0 ? std::max(0.02, anchor * 0.02) : 0.02;
}

double priceCeiling(size_t elementIndex) {
    const double anchor = econAnchorPrice(int(elementIndex));
    return anchor > 0.0 ? anchor * 400.0 : 20.0;
}

// Скретч-буферы: рынок обновляется по одному, память переиспользуется.
std::vector<double>& scratchWeights() { static std::vector<double> v; return v; }
std::vector<double>& scratchShares() { static std::vector<double> v; return v; }

// Ядро замещения: делит нужду каждой функции между кандидатами по (q/p)^SIGMA
// и переводит услуги обратно в массу элементов.
void computeSubstitution(Market& m) {
    const size_t n = m.prices.size();
    std::vector<double>& w = scratchWeights();
    std::vector<double>& shares = scratchShares();
    if (w.size() != n) w.assign(n, 0.0);
    if (shares.size() != n * EF_COUNT) shares.assign(n * EF_COUNT, 0.0);
    std::fill(shares.begin(), shares.end(), 0.0);

    if (m.demandRate.size() != n) m.demandRate.assign(n, 0.0);
    std::fill(m.demandRate.begin(), m.demandRate.end(), 0.0);

    // Обратная цена с локальной преференцией: p^-SIGMA * pref.
    for (size_t i = 0; i < n; ++i) {
        const double p = std::max(0.02, m.prices[i]);
        double pref = i < m.pref.size() ? m.pref[i] : 1.0;
        if (i < m.demandNoise.size()) pref *= 1.0 + 0.01 * m.demandNoise[i];
        w[i] = std::pow(p, -ECON_SIGMA) * pref;
    }

    if (m.rationing.size() != size_t(EF_COUNT)) m.rationing.assign(EF_COUNT, 1.0);
    std::vector<double>& rationed = m.rationing;
    std::fill(rationed.begin(), rationed.end(), 0.0);

    double costWeighted = 0.0, needTotal = 0.0;
    for (int f = 0; f < EF_COUNT; ++f) {
        const double need = f < int(m.needs.size()) ? m.needs[f] : 0.0;
        if (need <= EPSV) continue;

        double wsum = 0.0, cheapest = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double qp = econQualityPow(int(i), f);
            if (qp <= 0.0) continue;
            wsum += qp * w[i];
            const double cost = m.prices[i] * econQualityInv(int(i), f);   // цена одной услуги
            if (cheapest <= 0.0 || cost < cheapest) cheapest = cost;
        }
        if (wsum <= EPSV || cheapest <= 0.0) continue;   // функцию закрыть нечем — уйдёт в strain

        // Спрос ЭЛАСТИЧЕН: когда услуга дорожает против нормы, общество урезает
        // потребление (живёт теснее, летает реже, строит меньше). Недобранное
        // уходит в strain — систему это медленно душит. Без этого нужда
        // неутолима и цена дефицита уходит в бесконечность.
        const double affordability = ECON_CREDITS_PER_SERVICE / cheapest;
        const double rationing = clamp(std::pow(affordability, ECON_DEMAND_ELASTICITY), 0.02, 1.0);
        const double effectiveNeed = need * rationing;

        for (size_t i = 0; i < n; ++i) {
            const double qp = econQualityPow(int(i), f);
            if (qp <= 0.0) continue;
            const double share = qp * w[i] / wsum;
            shares[i * EF_COUNT + size_t(f)] = share;
            m.demandRate[i] += effectiveNeed * share * econQualityInv(int(i), f);
        }
        rationed[f] = rationing;
        costWeighted += need * cheapest;
        needTotal += need;
    }
    m.serviceCostAvg = needTotal > EPSV ? costWeighted / needTotal : 0.0;
}

} // namespace

void Market::seed(const std::vector<Resource>& localResources, const std::vector<double>& demandBias, const std::string& role_, double population, double industry) {
    role = role_;
    supply.clear();
    demand.clear();
    prices.clear();
    productionRate.clear();
    demandRate.clear();
    demandNoise.clear();
    eventMul.clear();
    pref.clear();
    strain = 0.0;

    const std::vector<ElementDefinition>& elements = elementDefinitions();
    const size_t n = elements.size();
    supply.reserve(n);
    demand.reserve(n);
    prices.reserve(n);
    productionRate.reserve(n);
    demandRate.reserve(n);
    pref.reserve(n);

    // Масштаб хозяйства: столько услуг в год потребляет система.
    const double scale = std::max(0.5, population * 0.0015 + industry * 24.0);
    // Крупную обжитую систему чужие торговцы обслуживают плотно, глухую — редко.
    tradeAccess = clamp(0.10 + 0.90 * std::min(1.0, scale / 85.0), 0.10, 1.0);
    const double* profile = econRoleNeedProfile(role);
    needs.assign(EF_COUNT, 0.0);
    for (int f = 0; f < EF_COUNT; ++f) needs[f] = scale * profile[f];

    double reservoirTotal = 0.0;
    for (size_t i = 0; i < n && i < localResources.size(); ++i) {
        reservoirTotal += localResources[i].amount;
    }
    if (reservoirTotal <= 0.0) reservoirTotal = 1.0;

    for (size_t i = 0; i < n; ++i) {
        const double bias = i < demandBias.size() ? demandBias[i] : 1.0;
        supply.emplace_back(elements[i].symbol, 0.0);
        demand.emplace_back(elements[i].symbol, 0.0);
        productionRate.push_back(0.0);
        demandRate.push_back(0.0);
        pref.push_back(clamp(0.72 + 0.28 * bias, 0.72, 2.4));
        prices.push_back(std::max(0.02, econAnchorPrice(int(i))));
    }
    eventMul.assign(n, 1.0);
    demandNoise.assign(n, 0.0);

    // Добыча: ПРОПОРЦИИ — из недр системы (что залегает, то и копают), а ОБЪЁМ
    // калибруется по СОБСТВЕННОМУ спросу этой системы. Хозяйство добывает
    // примерно столько, сколько потребляет; расхождение пропорций недр и
    // пропорций нужды и есть причина, по которой система что-то ввозит и
    // вывозит — то есть причина существования торговли.
    computeSubstitution(*this);
    double demandTotal = 0.0;
    for (size_t i = 0; i < n; ++i) demandTotal += demandRate[i];
    const double outputTotal = std::max(0.01, demandTotal * 1.1);

    for (size_t i = 0; i < n; ++i) {
        const double reservoir = i < localResources.size() ? localResources[i].amount : 0.0;
        productionRate[i] = std::max(1e-5, outputTotal * reservoir / reservoirTotal);
        // Склад — оборот за несколько лет, а не вся кора планеты: поэтому
        // крупная закупка реально двигает цену.
        supply[i].amount = productionRate[i] * ECON_TARGET_COVERAGE;
    }

    // Несколько шагов релаксации: рынок стартует близко к равновесию, а не из шума.
    for (int pass = 0; pass < 12; ++pass) updatePrices();

    for (size_t i = 0; i < n; ++i) demand[i].amount = demandRate[i] * 8.0;
}

void Market::updatePrices() {
    const size_t n = prices.size();
    if (n == 0) return;
    if (needs.size() != size_t(EF_COUNT)) needs.assign(EF_COUNT, 0.0);
    if (pref.size() != n) pref.assign(n, 1.0);

    computeSubstitution(*this);

    for (size_t i = 0; i < n; ++i) {
        const double D = demandRate[i];
        const double P = i < productionRate.size() ? productionRate[i] : 0.0;
        const double imbalance = (D - P) / (D + P + EPSV);
        const double stock = i < supply.size() ? supply[i].amount : 0.0;
        const double cover = D > EPSV ? stock / D : ECON_TARGET_COVERAGE * 9.0;
        const double coverPress = clamp((ECON_TARGET_COVERAGE - std::min(cover, ECON_TARGET_COVERAGE * 9.0)) / ECON_TARGET_COVERAGE, -1.0, 1.0);
        const double ev = i < eventMul.size() ? eventMul[i] : 1.0;
        const double evPress = clamp(ev - 1.0, -0.85, 3.0);

        const double drive = 0.55 * imbalance + 0.45 * coverPress + 0.5 * evPress + 0.6 * gLevelDrive +
                            1.1 * importBandDrive(i, prices[i], tradeAccess);
        const double factor = clamp(std::exp(KAPPA * drive), 0.55, 1.9);
        prices[i] = clamp(prices[i] * factor, priceFloor(i), priceCeiling(i));
    }
}

void Market::update(double dt) {
    const size_t n = prices.size();
    if (n == 0 || dt <= 0.0) return;
    dt = std::min(dt, 40.0);
    if (needs.size() != size_t(EF_COUNT)) needs.assign(EF_COUNT, 0.0);
    if (pref.size() != n) pref.assign(n, 1.0);
    if (supply.size() != n || productionRate.size() != n) return;

    computeSubstitution(*this);
    const std::vector<double>& shares = scratchShares();

    // 1. Потребление: сколько нужды реально закрыто из запаса.
    double served[EF_COUNT];
    for (int f = 0; f < EF_COUNT; ++f) served[f] = 0.0;

    for (size_t i = 0; i < n; ++i) {
        const double want = demandRate[i] * dt;
        if (want <= EPSV) continue;
        const double got = std::min(want, std::max(0.0, supply[i].amount));
        supply[i].amount = std::max(0.0, supply[i].amount - got);
        const double fill = clamp01(got / want);
        for (int f = 0; f < EF_COUNT; ++f) {
            const double share = shares[i * EF_COUNT + size_t(f)];
            if (share <= 0.0) continue;
            served[f] += needs[f] * rationing[f] * share * fill;
        }
    }

    double needTotal = 0.0, servedTotal = 0.0;
    for (int f = 0; f < EF_COUNT; ++f) {
        needTotal += needs[f];
        servedTotal += served[f];
    }
    strain = needTotal > EPSV ? clamp01(1.0 - servedTotal / needTotal) : 0.0;
    observeServiceLevel(serviceCostAvg);

    // 2. Добыча/производство пополняет запас.
    for (size_t i = 0; i < n; ++i) supply[i].amount += productionRate[i] * dt;

    // 3. Цена идёт туда, где спрос сходится с предложением.
    const double step = clamp(dt, 0.1, 6.0);
    for (size_t i = 0; i < n; ++i) {
        const double D = demandRate[i];
        const double P = productionRate[i];
        const double imbalance = (D - P) / (D + P + EPSV);
        const double cover = D > EPSV ? supply[i].amount / D : ECON_TARGET_COVERAGE * 9.0;
        const double coverPress = clamp((ECON_TARGET_COVERAGE - std::min(cover, ECON_TARGET_COVERAGE * 9.0)) / ECON_TARGET_COVERAGE, -1.0, 1.0);
        const double ev = i < eventMul.size() ? eventMul[i] : 1.0;
        const double evPress = clamp(ev - 1.0, -0.85, 3.0);

        const double drive = 0.55 * imbalance + 0.45 * coverPress + 0.5 * evPress + 0.6 * gLevelDrive +
                            1.1 * importBandDrive(i, prices[i], tradeAccess);
        const double factor = clamp(std::exp(KAPPA * drive * step), 0.5, 2.2);
        prices[i] = clamp(prices[i] * factor, priceFloor(i), priceCeiling(i));
        demand[i].amount = demandRate[i] * 8.0;
    }
}

void Market::applyTrade(int elementIndex, double deltaUnits) {
    if (elementIndex < 0 || elementIndex >= int(prices.size())) return;
    if (deltaUnits == 0.0) return;
    supply[elementIndex].amount = std::max(0.0, supply[elementIndex].amount + deltaUnits);

    // Глубина рынка — годовой оборот за срок целевого запаса. Сделка размером
    // с этот оборот двигает цену в разы; мелкая — почти не двигает.
    const double flow = std::max(1e-6, demandRate[elementIndex] + productionRate[elementIndex]);
    const double depth = flow * ECON_TARGET_COVERAGE;
    const double impact = clamp(-deltaUnits / depth, -3.0, 3.0);
    const double factor = clamp(std::exp(0.35 * impact), 0.5, 2.2);
    prices[elementIndex] = clamp(prices[elementIndex] * factor, priceFloor(size_t(elementIndex)), priceCeiling(size_t(elementIndex)));
}

double Market::pricePressure() const {
    if (prices.empty()) return 1.0;
    double sum = 0.0;
    int counted = 0;
    for (size_t i = 0; i < prices.size(); ++i) {
        const double reference = marketReferencePrice(int(i));
        if (reference <= 0.0) continue;
        sum += prices[i] / reference;
        ++counted;
    }
    return counted > 0 ? sum / double(counted) : 1.0;
}

double Market::serviceCost(int functionIndex) const {
    if (functionIndex < 0 || functionIndex >= EF_COUNT) return 0.0;
    double best = 0.0;
    for (size_t i = 0; i < prices.size(); ++i) {
        const double q = econQuality(int(i), functionIndex);
        if (q <= 0.008) continue;
        const double cost = prices[i] / q;
        if (best <= 0.0 || cost < best) best = cost;
    }
    return best;
}

double Market::fairPrice(int elementIndex) const {
    if (elementIndex < 0 || elementIndex >= int(prices.size())) return 0.0;
    double best = 0.0;
    for (int f = 0; f < EF_COUNT; ++f) {
        const double q = econQuality(elementIndex, f);
        if (q <= 0.008) continue;
        best = std::max(best, q * serviceCost(f));
    }
    return best;
}

double Market::marketShare(int elementIndex, int functionIndex) const {
    if (elementIndex < 0 || elementIndex >= int(prices.size())) return 0.0;
    if (functionIndex < 0 || functionIndex >= EF_COUNT) return 0.0;
    const double qp = econQualityPow(elementIndex, functionIndex);
    if (qp <= 0.0) return 0.0;

    double wsum = 0.0, mine = 0.0;
    for (size_t i = 0; i < prices.size(); ++i) {
        const double q = econQualityPow(int(i), functionIndex);
        if (q <= 0.0) continue;
        double pref = i < this->pref.size() ? this->pref[i] : 1.0;
        const double w = q * std::pow(std::max(0.02, prices[i]), -ECON_SIGMA) * pref;
        wsum += w;
        if (int(i) == elementIndex) mine = w;
    }
    return wsum > EPSV ? mine / wsum : 0.0;
}

double Market::coverageYears(int elementIndex) const {
    if (elementIndex < 0 || elementIndex >= int(supply.size())) return 0.0;
    const double D = elementIndex < int(demandRate.size()) ? demandRate[elementIndex] : 0.0;
    if (D <= EPSV) return 999.0;
    return supply[elementIndex].amount / D;
}

int Market::dominantNeedFunction() const {
    int best = -1;
    double bestValue = 0.0;
    for (int f = 0; f < int(needs.size()) && f < EF_COUNT; ++f) {
        if (needs[f] > bestValue) { bestValue = needs[f]; best = f; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Опорная цена скопления: чего элемент стоит на «типичном» рынке. Считается
// один раз — синтетическая система со средним профилем нужд, средним обилием и
// средней индустрией, прогнанная до равновесия. Отклонение локальной цены от
// опорной и есть сигнал арбитража для игрока.
// ---------------------------------------------------------------------------
namespace {

std::vector<double> buildReferencePrices() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    const size_t n = elements.size();
    const char* roles[] = {"habitat", "refinery", "shipyard", "research", "military", "frontier"};

    // Опорная цена — МЕДИАНА по выборке типовых систем скопления: разные роли,
    // разная насыщенность недр, разная металличность. Отклонение местной цены
    // от этой медианы и есть сигнал арбитража для игрока.
    gBuildingReference = true;
    std::vector<std::vector<double> > samples(n);
    std::vector<double> flatBias(n, 1.0);

    for (int r = 0; r < 6; ++r) {
        for (int variant = 0; variant < 3; ++variant) {
            const double richness = 0.55 + variant * 0.75;
            const double metallicity = 0.28 + variant * 0.30;

            std::vector<Resource> reservoir;
            reservoir.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                const ElementDefinition& e = elements[i];
                const double lowMass = 1.0 / std::sqrt(std::max(1.0, e.atomicMass));
                const double industrial = std::min(1.0, e.metallicTrait * 0.5 + e.structuralTrait * 0.35 + e.conductorTrait * 0.25);
                const double volatiles = std::min(1.0, lowMass * (0.7 + e.oxidizerTrait + e.reducerTrait + e.fusionFuelTrait));
                double amount = e.abundanceWeight * richness;
                amount *= 0.35 + metallicity * industrial + (1.0 - metallicity) * volatiles;
                reservoir.emplace_back(e.symbol, std::max(0.001, amount));
            }

            Market m;
            m.seed(reservoir, flatBias, roles[r], 7000.0 + variant * 8000.0, 0.7 + variant * 1.0);
            for (int year = 0; year < 25; ++year) m.update(1.0);
            for (size_t i = 0; i < n; ++i) samples[i].push_back(m.prices[i]);
        }
    }

    std::vector<double> reference(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        std::sort(samples[i].begin(), samples[i].end());
        reference[i] = samples[i][samples[i].size() / 2];
    }
    gBuildingReference = false;
    return reference;
}

} // namespace

double marketReferencePrice(int elementIndex) {
    static const std::vector<double> reference = buildReferencePrices();
    if (elementIndex < 0 || elementIndex >= int(reference.size())) return 0.0;
    return reference[size_t(elementIndex)];
}
