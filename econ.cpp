#include "econ.h"
#include "resource.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Матрица способностей строится ОДИН раз из континуума ElementDefinition.
// Каждая формула — чистая функция физических полей элемента; ни одного
// частного случая по символу/номеру. Затем каждый столбец нормируется на
// своего лучшего кандидата, чтобы функции были сравнимы между собой.
// ---------------------------------------------------------------------------

const double ECON_SIGMA = 3.5;
const double ECON_TARGET_COVERAGE = 6.0;
const double ECON_QUALITY_FLOOR = 0.22;
const double ECON_CREDITS_PER_SERVICE = 140.0;
const double ECON_DEMAND_ELASTICITY = 2.0;

namespace {

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

struct EconTables {
    std::vector<double> quality;    // [element * EF_COUNT + f]
    std::vector<double> qualityPow; // q^SIGMA
    std::vector<double> qualityInv; // 1/q
    std::vector<int> primary;
    std::vector<double> primaryQ;
};

EconTables buildTables() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    const size_t n = elements.size();

    EconTables t;
    t.quality.assign(n * EF_COUNT, 0.0);
    t.qualityPow.assign(n * EF_COUNT, 0.0);
    t.qualityInv.assign(n * EF_COUNT, 0.0);
    t.primary.assign(n, -1);
    t.primaryQ.assign(n, 0.0);

    for (size_t i = 0; i < n; ++i) {
        const ElementDefinition& e = elements[i];

        // Производные конфигурации (те же величины, что рождают сам элемент).
        const double lowMass = std::pow(std::max(1.0, e.atomicMass), -0.35);
        const double reactivity = clamp01(1.0 - e.nobleStability);
        const double dPartial = clamp01(4.0 * e.dShellFill * (1.0 - e.dShellFill));
        const double fPartial = clamp01(4.0 * e.fShellFill * (1.0 - e.fShellFill));
        const double safe = clamp01(1.0 - e.handlingRisk);
        const double bonding = clamp01(std::min(e.valenceElectrons, 8.0 - e.valenceElectrons) / 4.0);

        double q[EF_COUNT];
        // Топливо — прямо из кривой энергии связи и параметра делимости.
        q[EF_FUSION]   = e.fusionFuelTrait;
        q[EF_FISSION]  = e.fissionFuelTrait;
        // Конструкция/электроника/химия — траиты, выведенные из конфигурации.
        q[EF_STRUCT]   = e.structuralTrait;
        q[EF_CONDUCT]  = e.conductorTrait;
        q[EF_CATALYST] = e.catalystTrait;
        // Жизнеобеспечение: лёгкие неметаллы, способные образовывать связи (органогены).
        // Инертные газы связей не дают — множитель reactivity их отсекает.
        q[EF_LIFE]     = clamp01(lowMass * (1.0 - e.metallicTrait) * (0.35 + bonding) * reactivity * 2.1);
        // Реагенты: сильный окислитель ИЛИ сильный восстановитель.
        q[EF_REACT]    = clamp01(std::max(e.oxidizerTrait, e.reducerTrait) * reactivity * 0.85);
        // Защита от излучения: сечение поглощения ~ Z^n, плюс плотность и стабильность.
        q[EF_SHIELD]   = clamp01(std::pow(double(e.atomicNumber) / 82.0, 1.6) *
                                 std::sqrt(e.atomicMass / 207.0) * e.nuclearStability * safe);
        // Жаропрочность: связь полузаполненной d-оболочки поздних периодов.
        q[EF_REFRACT]  = clamp01(std::pow(dPartial, 0.8) * (1.0 - 0.9 * fPartial) *
                                 (0.3 + 0.7 * clamp01((e.period - 3.0) / 3.0)) * safe);
        // Химическая стойкость: инертная среда (замкнутая оболочка) ИЛИ
        // благородный металл (заполненная d + релятивистски удержанный s).
        // Контакты, покрытия, тигли, рабочие тела — всё, что не должно вступать
        // в реакцию. Отсюда и ценность золота: оно годится сразу в несколько ниш.
        q[EF_INERT]    = clamp01(e.inertnessTrait * (0.55 + 0.45 * safe) * 1.15);

        // Доступность вещества: то, что распадается за секунды, товаром не бывает.
        // Делящееся топливо этот же фильтр уже несёт в собственном окне делимости.
        const double availability = 0.10 + 0.90 * e.nuclearStability;
        for (int f = 0; f < EF_COUNT; ++f) {
            t.quality[i * EF_COUNT + f] = clamp01(q[f] * (f == EF_FISSION ? 1.0 : availability));
        }
    }

    // Нормировка по столбцам: лучший кандидат функции = 1.0.
    for (int f = 0; f < EF_COUNT; ++f) {
        double best = 0.0;
        for (size_t i = 0; i < n; ++i) best = std::max(best, t.quality[i * EF_COUNT + f]);
        if (best <= 1e-9) continue;
        for (size_t i = 0; i < n; ++i) t.quality[i * EF_COUNT + f] /= best;
    }

    // Производные таблицы: q^SIGMA и 1/q (pow в цикле рынка недопустим).
    for (size_t i = 0; i < n; ++i) {
        for (int f = 0; f < EF_COUNT; ++f) {
            const double q = t.quality[i * EF_COUNT + f];
            // Технологический порог: слишком плохой кандидат не годится ни за какие
            // деньги (корпус из цезия не строят). Так у каждой функции остаётся
            // обозримый круг реальных конкурентов, а не вся таблица.
            const bool usable = q > ECON_QUALITY_FLOOR;
            // Качество измеряется НА КИЛОГРАММ, а торгуют и возят ЕДИНИЦАМИ:
            // переводим в услугу на единицу, иначе лёгкие газы дают абсурдно
            // много пользы на тонну трюма.
            const double perUnit = q * elementUnitMass(int(i));
            t.qualityPow[i * EF_COUNT + f] = usable ? std::pow(perUnit, ECON_SIGMA) : 0.0;
            t.qualityInv[i * EF_COUNT + f] = usable ? 1.0 / perUnit : 0.0;
            if (usable && q > t.primaryQ[i]) {
                t.primaryQ[i] = q;
                t.primary[i] = f;
            }
        }
    }
    return t;
}

const EconTables& tables() {
    static const EconTables t = buildTables();
    return t;
}

// Профили нужд по ролям. Это данные о ПОТРЕБНОСТЯХ ОБЩЕСТВА, а не про элементы:
// какая доля хозяйства уходит на жизнеобеспечение, конструкцию, энергию и т.д.
// Какими элементами это закроется — решает рынок через замещение.
const double kRoleNeeds[][EF_COUNT] = {
    // FUS   FIS   STR   CND   CAT   LIF   RCT   SHD   RFR   INR
    { 0.12, 0.03, 0.10, 0.10, 0.06, 0.34, 0.12, 0.03, 0.02, 0.08 }, // habitat
    { 0.14, 0.05, 0.12, 0.07, 0.24, 0.08, 0.18, 0.03, 0.07, 0.02 }, // refinery
    { 0.10, 0.05, 0.30, 0.14, 0.04, 0.06, 0.05, 0.09, 0.13, 0.04 }, // shipyard
    { 0.08, 0.10, 0.06, 0.26, 0.16, 0.10, 0.06, 0.05, 0.04, 0.09 }, // research
    { 0.12, 0.18, 0.20, 0.10, 0.03, 0.06, 0.05, 0.14, 0.10, 0.02 }, // military
    { 0.18, 0.02, 0.16, 0.07, 0.04, 0.30, 0.13, 0.02, 0.03, 0.05 }  // frontier
};

const char* kRoleNames[] = {"habitat", "refinery", "shipyard", "research", "military", "frontier"};

} // namespace

const char* econFunctionCode(int f) {
    switch (f) {
        case EF_FUSION:   return "FUS";
        case EF_FISSION:  return "FIS";
        case EF_STRUCT:   return "STR";
        case EF_CONDUCT:  return "CND";
        case EF_CATALYST: return "CAT";
        case EF_LIFE:     return "LIF";
        case EF_REACT:    return "RCT";
        case EF_SHIELD:   return "SHD";
        case EF_REFRACT:  return "RFR";
        case EF_INERT:    return "INR";
        default:          return "---";
    }
}

const char* econFunctionName(int f) {
    switch (f) {
        case EF_FUSION:   return "FUSION FUEL";
        case EF_FISSION:  return "FISSION FUEL";
        case EF_STRUCT:   return "STRUCTURE";
        case EF_CONDUCT:  return "CONDUCTORS";
        case EF_CATALYST: return "CATALYSIS";
        case EF_LIFE:     return "LIFE SUPPORT";
        case EF_REACT:    return "REAGENTS";
        case EF_SHIELD:   return "SHIELDING";
        case EF_REFRACT:  return "REFRACTORY";
        case EF_INERT:    return "INERT / NOBLE";
        default:          return "UNKNOWN";
    }
}

double econQuality(int elementIndex, int functionIndex) {
    const EconTables& t = tables();
    if (elementIndex < 0 || functionIndex < 0 || functionIndex >= EF_COUNT) return 0.0;
    const size_t at = size_t(elementIndex) * EF_COUNT + size_t(functionIndex);
    return at < t.quality.size() ? t.quality[at] : 0.0;
}

double econQualityPow(int elementIndex, int functionIndex) {
    const EconTables& t = tables();
    if (elementIndex < 0 || functionIndex < 0 || functionIndex >= EF_COUNT) return 0.0;
    const size_t at = size_t(elementIndex) * EF_COUNT + size_t(functionIndex);
    return at < t.qualityPow.size() ? t.qualityPow[at] : 0.0;
}

double econQualityInv(int elementIndex, int functionIndex) {
    const EconTables& t = tables();
    if (elementIndex < 0 || functionIndex < 0 || functionIndex >= EF_COUNT) return 0.0;
    const size_t at = size_t(elementIndex) * EF_COUNT + size_t(functionIndex);
    return at < t.qualityInv.size() ? t.qualityInv[at] : 0.0;
}

int econPrimaryFunction(int elementIndex) {
    const EconTables& t = tables();
    if (elementIndex < 0 || size_t(elementIndex) >= t.primary.size()) return -1;
    return t.primary[size_t(elementIndex)];
}

double econPrimaryQuality(int elementIndex) {
    const EconTables& t = tables();
    if (elementIndex < 0 || size_t(elementIndex) >= t.primaryQ.size()) return 0.0;
    return t.primaryQ[size_t(elementIndex)];
}

double econAnchorPrice(int elementIndex) {
    const double q = econPrimaryQuality(elementIndex);
    // Цена ЕДИНИЦЫ = кредиты за услугу * услуга на кг * кг в единице.
    // Отсюда полный трюм товара «в норме» стоит примерно
    // ECON_CREDITS_PER_SERVICE * грузоподъёмность, независимо от элемента.
    return q > 0.0 ? ECON_CREDITS_PER_SERVICE * q * elementUnitMass(elementIndex) : 0.0;
}

const double* econRoleNeedProfile(const std::string& role) {
    const int count = int(sizeof(kRoleNames) / sizeof(kRoleNames[0]));
    for (int i = 0; i < count; ++i) {
        if (role == kRoleNames[i]) return kRoleNeeds[i];
    }
    return kRoleNeeds[0];
}
