#include "drive.h"
#include "resource.h"
#include <algorithm>
#include <cmath>

const std::vector<DriveDef>& driveDefs() {
    //                name                 family                price      mass  chamber   eff  thrust  ignThr  SY  blurb
    static const std::vector<DriveDef> defs = {
        // Дефолтный движок: стоит на любом корпусе бесплатно, слот не занимает.
        // Это буквально NERVA: активная зона на делении греет лёгкий газ.
        {"Thermal Core I",   DriveFamily::Thermal,      0.0,    0.0,   0.85,  0.55,   1.00,    0.30,  0, "heats propellant; needs rich fuel"},
        {"Thermal Core II",  DriveFamily::Thermal,   6500.0,    6.0,   2.10,  0.66,   1.15,    0.18,  1, "hotter chamber, higher exhaust"},
        {"Thermal Core III", DriveFamily::Thermal,  34000.0,   14.0,   5.40,  0.74,   1.30,    0.10,  2, "refractory chamber; wide fuel range"},

        // Ионники: огромная скорость истечения, ничтожная тяга, компактные баки.
        {"Ion Grid I",       DriveFamily::Ion,      12000.0,    5.0,   4.80,  0.72,   0.16,    0.20,  1, "electric; dense heavy propellant"},
        {"Ion Grid II",      DriveFamily::Ion,      58000.0,   11.0,  11.00,  0.82,   0.22,    0.12,  2, "high voltage; very long range"},

        // Факел: топливо и есть рабочее тело. Только богатый синтез.
        {"Fusion Torch",     DriveFamily::Torch,   260000.0,   26.0,   0.00,  0.60,   0.85,    0.30,  3, "burns fuel as exhaust; light fusion only"},
    };
    return defs;
}

const char* driveFamilyLabel(DriveFamily family) {
    switch (family) {
        case DriveFamily::Thermal: return "THERMAL";
        case DriveFamily::Ion: return "ION";
        default: return "TORCH";
    }
}

namespace {

const DriveDef* driveAt(int driveIndex) {
    const std::vector<DriveDef>& defs = driveDefs();
    if (driveIndex < 0 || driveIndex >= int(defs.size())) return 0;
    return &defs[driveIndex];
}

double smoothstep(double edge0, double edge1, double value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0 : 0.0;
    const double t = std::max(0.0, std::min(1.0, (value - edge0) / (edge1 - edge0)));
    return t * t * (3.0 - 2.0 * t);
}

}

DriveFamily driveFamilyOf(int driveIndex) {
    const DriveDef* def = driveAt(driveIndex);
    return def ? def->family : DriveFamily::Thermal;
}

int defaultDriveIndex() {
    return 0;
}

double driveIgnitionFraction(int driveIndex, int elementIndex) {
    const DriveDef* def = driveAt(driveIndex);
    if (!def) return 0.0;
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(elements.size())) return 0.0;
    const ElementDefinition& e = elements[elementIndex];

    // Деление требует КРИТИЧЕСКОЙ СБОРКИ — плотной решётки ядер, где нейтрону
    // есть во что попасть. Благородный газ такой зоны не образует, цепная
    // реакция в нём не удержится, поэтому замкнутая оболочка гасит делительную
    // ветвь. Синтезу решётка не нужна, он идёт в плазме, — водород и гелий
    // штрафа не получают.
    const double fissionUsable = e.fissionFuelTrait * (1.0 - e.nobleStability);

    // Факел — установка синтеза: делением его не накормить. Отдельной проверки
    // на железный пик не нужно, выше железа fusionFuelTrait и так ноль.
    const double reachable = def->family == DriveFamily::Torch
        ? e.fusionFuelTrait
        : std::max(e.fusionFuelTrait, fissionUsable);

    // Плавная сходимость вместо обрыва: чем ближе траит к порогу камеры, тем
    // хуже горит, но запрета нет нигде. Залитое железо не «запрещено» — оно
    // просто балласт, и это видно по упавшей энергии.
    return smoothstep(def->ignitionThreshold * 0.5, def->ignitionThreshold * 1.5, reachable);
}

double driveExhaustCeiling(int driveIndex, const MixSummary& mix) {
    const DriveDef* def = driveAt(driveIndex);
    if (!def || mix.mass <= 0.0) return 0.0;
    const double unitMass = std::max(0.05, mix.meanAtomicMass * RESOURCE_MASS_SCALE);

    if (def->family == DriveFamily::Torch) {
        // Факел разгоняет собственные продукты синтеза: скорость истечения
        // растёт прямо из энерговыделения топлива, v ~ sqrt(2E/m).
        if (mix.specificEnergy <= 0.0) return 0.0;
        return std::sqrt(2.0 * mix.specificEnergy / NUCLEON_REST_MEV * def->efficiency) * 2.4;
    }

    if (def->family == DriveFamily::Thermal) {
        // Тепловая скорость: v ~ sqrt(2kT/m). Лёгкое рабочее тело выигрывает
        // прямо по массе — водород втрое быстрее воды, как у настоящего ЯРД.
        return std::sqrt(2.0 * def->chamberEnergy / unitMass) * 0.055;
    }

    // Ионник: скорость задаётся ускоряющим напряжением и от массы зависит слабо
    // (напряжение можно поднять). Тяжёлые ионы медленнее при том же напряжении,
    // но эта зависимость мягкая — ксенон остаётся рабочим выбором.
    return std::sqrt(def->chamberEnergy) * std::pow(unitMass, -0.22) * 0.085;
}

double driveJetEfficiency(int driveIndex, const MixSummary& mix) {
    const DriveDef* def = driveAt(driveIndex);
    if (!def) return 0.0;

    double efficiency = def->efficiency;
    if (def->family == DriveFamily::Ion) {
        // За каждый ион надо заплатить ионизацией. Благородные газы дорогие в
        // ионизации, но берут плотностью — поэтому штраф ощутимый, но не смертный.
        efficiency *= 0.55 + 0.45 * mix.meanIonizationEase;
    }
    // Возня с опасным веществом всегда стоит части энергии (ship.md).
    efficiency /= 1.0 + mix.meanHandlingRisk * 0.8;
    return std::max(0.02, std::min(1.0, efficiency));
}

bool driveUsesFuelAsPropellant(int driveIndex) {
    const DriveDef* def = driveAt(driveIndex);
    return def && def->family == DriveFamily::Torch;
}
