#include "resource.h"
#include <algorithm>
#include <cmath>

namespace {

struct RawElement {
    int atomicNumber;
    const char* symbol;
    const char* name;
};

// Электронная конфигурация по правилу Маделунга: единственный источник
// «химии» в игре. Из неё следуют блок (s/p/d/f), период, октет и всё
// поведение групп — без единого частного случая по символу элемента.
struct ShellState {
    double outerElectrons;   // электроны внешней s+p оболочки
    double outerCapacity;    // её ёмкость (2 для первого периода, иначе 8)
    double period;           // номер внешней оболочки
    double dFill;            // заполнение (n-1)d, 0..1
    double fFill;            // заполнение (n-2)f, 0..1
    int block;               // 0=s, 1=p, 2=d, 3=f — какая орбиталь заполняется последней
};

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

double smoothstep(double edge0, double edge1, double value) {
    const double t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

double gaussian(double x, double center, double width) {
    const double d = (x - center) / width;
    return std::exp(-d * d);
}

ShellState shellStateFor(int z) {
    // Порядок заполнения (правило Маделунга n+l): орбиталь = {n, тип, ёмкость},
    // тип 0=s, 1=p, 2=d, 3=f.
    struct Orbital { int n; int kind; int capacity; };
    static const Orbital order[] = {
        {1, 0, 2},  {2, 0, 2},  {2, 1, 6},  {3, 0, 2},  {3, 1, 6},
        {4, 0, 2},  {3, 2, 10}, {4, 1, 6},  {5, 0, 2},  {4, 2, 10},
        {5, 1, 6},  {6, 0, 2},  {4, 3, 14}, {5, 2, 10}, {6, 1, 6},
        {7, 0, 2},  {5, 3, 14}, {6, 2, 10}, {7, 1, 6}
    };
    const int orbitalCount = int(sizeof(order) / sizeof(order[0]));

    int filled[8][4];
    for (int n = 0; n < 8; ++n)
        for (int k = 0; k < 4; ++k) filled[n][k] = 0;

    int remaining = z;
    int period = 1;
    int block = 0;
    for (int i = 0; i < orbitalCount && remaining > 0; ++i) {
        const int put = std::min(remaining, order[i].capacity);
        filled[order[i].n][order[i].kind] += put;
        remaining -= put;
        if (put > 0) block = order[i].kind;                       // последняя заполняемая орбиталь
        if (order[i].kind == 0 && put > 0) period = order[i].n;   // новый период открывает s-орбиталь
        if (order[i].n > period && order[i].kind <= 1) period = order[i].n;
    }

    // Правило устойчивости полу-/полностью заполненной d-оболочки: обменная
    // энергия перетягивает один s-электрон в d (d4->d5, d9->d10). Универсальное
    // правило Хунда, а не список исключений: так рождаются Cr, Cu, Mo, Ag, Au.
    if (period >= 2 && filled[period][0] == 2) {
        const int d = filled[period - 1][2];
        if (d == 4 || d == 9) {
            filled[period - 1][2] = d + 1;
            filled[period][0] = 1;
        }
    }

    ShellState state;
    state.period = double(period);
    state.block = block;
    state.outerCapacity = period == 1 ? 2.0 : 8.0;
    state.outerElectrons = double(filled[period][0] + filled[period][1]);
    state.dFill = period >= 2 ? double(filled[period - 1][2]) / 10.0 : 0.0;
    state.fFill = period >= 3 ? double(filled[period - 2][3]) / 14.0 : 0.0;
    return state;
}

// --- Ядерная физика: формула Вайцзеккера (полуэмпирическая масса) ---
// Даёт кривую энергии связи с естественным пиком у железа: ни одной
// константы «26» в игровом коде — пик получается сам.
double bindingEnergyPerNucleon(double z, double a) {
    if (a < 1.0 || z < 1.0) return 0.0;
    const double aV = 15.75, aS = 17.8, aC = 0.711, aA = 23.7, aP = 11.18;
    const double n = a - z;
    double b = aV * a
             - aS * std::pow(a, 2.0 / 3.0)
             - aC * z * (z - 1.0) / std::pow(a, 1.0 / 3.0)
             - aA * (a - 2.0 * z) * (a - 2.0 * z) / a;
    const bool zEven = std::fmod(z, 2.0) < 0.5;
    const bool nEven = std::fmod(n, 2.0) < 0.5;
    if (zEven && nEven) b += aP / std::sqrt(a);
    else if (!zEven && !nEven) b -= aP / std::sqrt(a);
    return std::max(0.0, b / a);
}

double deterministicNoise(int z, double span) {
    const double n = std::sin(double(z) * 12.9898 + 78.233) * 43758.5453;
    const double f = n - std::floor(n);
    return std::exp((f - 0.5) * span);
}

double atomicMassFor(int z) {
    const double neutronExcess = smoothstep(26.0, 118.0, double(z)) * 0.38;
    return std::max(1.0, std::floor(double(z) * (2.0 + neutronExcess) - 1.0 / double(z) + 0.5));
}

ElementDefinition deriveElement(const RawElement& item) {
    const int z = item.atomicNumber;
    const double dz = double(z);
    const ShellState shell = shellStateFor(z);
    const double atomicMass = atomicMassFor(z);

    // --- Химия: всё следует из электронной конфигурации ---
    const double shellFill = clamp01(shell.outerElectrons / shell.outerCapacity);
    const double dPartial = clamp01(4.0 * shell.dFill * (1.0 - shell.dFill));   // максимум у полузаполненной d
    const double fPartial = clamp01(4.0 * shell.fFill * (1.0 - shell.fFill));
    // Металличность даёт принадлежность к d/f-блоку (заполняется внутренняя
    // оболочка при почти пустой внешней). В s/p-блоке решает электроотрицательность,
    // поэтому Kr и Br металлами не становятся, а Cu и Zn — да.
    const bool innerShellOpen = shell.block >= 2;

    // Замкнутая оболочка без открытых внутренних — благородство.
    const double nobleStability = clamp01(std::pow(shellFill, 9.0) * (1.0 - dPartial) * (1.0 - fPartial));
    const double reactivity = 1.0 - nobleStability;

    // Релятивистское сжатие внешней s-орбитали. Скорость 1s-электрона ~ Z*alpha,
    // отсюда лоренц-фактор: у золота Z*alpha = 0.58c, гамма = 1.22. Именно это
    // держит 6s-электрон и делает Au/Pt/Hg химически благородными — и именно
    // поэтому золото электроотрицательнее меди, хотя стоит НИЖЕ неё в группе.
    const double zAlpha = dz / 137.036;
    const double relGamma = 1.0 / std::sqrt(std::max(0.15, 1.0 - std::min(0.85, zAlpha * zAlpha)));
    const double relativisticBoost = relGamma - 1.0;

    // Электроотрицательность (алленовский проксик): растёт с заполнением
    // внешней оболочки, падает с номером периода и растёт от релятивистского
    // сжатия — тем сильнее, чем плотнее закрыта d-оболочка под внешним s.
    const double electronegativity = clamp01((0.20 + 0.80 * shellFill) * (2.6 / (shell.period + 0.6)) +
                                             0.25 * shell.dFill * shell.dFill +
                                             0.60 * relativisticBoost * (0.3 + 0.7 * shell.dFill));
    // Металличность: весь d/f-блок — металлы; в главных группах решает ЭО.
    const double metallicTrait = clamp01(innerShellOpen
        ? 0.88 + 0.12 * (1.0 - electronegativity)
        : 1.0 - smoothstep(0.26, 0.62, electronegativity));

    const double oxidizerTrait = clamp01(electronegativity * electronegativity * reactivity * 1.35);
    const double reducerTrait = clamp01(std::pow(1.0 - electronegativity, 1.5) * metallicTrait * reactivity * 1.25);

    // Прочность: связь d-электронов (пик у полузаполненной d), ковалентный
    // каркас у лёгких неметаллов, металлическая связь главных групп —
    // всё поделено на массу (удельная прочность).
    // Каркас требует НЕ МЕНЕЕ двух связей на атом: одна связь даёт молекулу газа,
    // а не решётку. Поэтому водород прочным материалом не становится.
    const double networkCapacity = clamp01((std::min(shell.outerElectrons, shell.outerCapacity - shell.outerElectrons) - 1.0) / 3.0);
    const double covalentNetworkTrait = clamp01((1.0 - metallicTrait) * networkCapacity *
                                                std::exp(-0.05 * std::max(0.0, dz - 6.0)) * 1.6);
    const double spMetalBond = clamp01(metallicTrait * std::min(shell.outerElectrons, 3.0) / 3.0);
    const double specificStrength = 1.0 / (0.55 + 0.45 * std::sqrt(atomicMass / 56.0));

    // --- Ядро: кривая энергии связи и делимость ---
    const double bindingPerNucleon = bindingEnergyPerNucleon(dz, atomicMass);
    static const double bindingPeak = 8.79;   // максимум кривой (район железа)
    const double fissility = dz * dz / std::max(1.0, atomicMass);   // Z^2/A
    // Устойчивость — это про РАСПАД, а не про энергию связи: лёгкие ядра связаны
    // слабо, но живут вечно. За свинцом стабильных изотопов нет, а сверхтяжёлые
    // гибнут от спонтанного деления, когда кулоновский член побеждает поверхностный.
    const double nuclearStability = clamp01(std::exp(-std::max(0.0, dz - 82.0) / 14.0) *
                                            std::exp(-std::max(0.0, fissility - 39.0) / 2.6));

    // Термояд: чем ниже энергия связи и ниже кулоновский барьер, тем лучше топливо.
    const double fusionYield = clamp01((bindingPeak - bindingPerNucleon) / bindingPeak);
    const double fusionFuelTrait = dz < 26.0
        ? clamp01(std::sqrt(fusionYield) * std::pow(dz, -0.7) * 1.02)
        : 0.0;
    // Деление: нужно окно делимости — ядро уже разваливается, но ещё живёт.
    const double fissionYield = clamp01((bindingEnergyPerNucleon(dz * 0.5, atomicMass * 0.5) - bindingPerNucleon) / 1.1);
    const double fissionFuelTrait = clamp01(fissionYield *
                                            smoothstep(36.0, 39.5, fissility) *
                                            (1.0 - smoothstep(41.5, 45.0, fissility)) *
                                            std::exp(-std::max(0.0, fissility - 40.0) / 2.2));

    // Благородство МЕТАЛЛА: заполненная d-оболочка, релятивистски удержанный
    // внешний электрон и живое ядро. Ни одного имени элемента — но наверх
    // выходят ровно золото, платиноиды и ртуть.
    // Отдавать приходится внешние s-электроны: у Cu/Ag/Au он ОДИН, у Zn/Cd/Hg —
    // два, поэтому те окисляются легко. Релятивизм тянет ртуть обратно вверх.
    const double outerLooseness = 0.6 + 0.4 * std::max(1.0, shell.outerElectrons);
    const double nobleMetalTrait = clamp01(std::pow(shell.dFill, 2.0) * (1.0 + 3.2 * relativisticBoost) *
                                           metallicTrait * nuclearStability *
                                           (0.35 + 0.65 * electronegativity) / outerLooseness);
    // Химическая стойкость вообще: газ с замкнутой оболочкой ИЛИ благородный металл.
    const double inertnessTrait = clamp01(std::max(nobleStability, nobleMetalTrait));

    const double activationCost = 0.3 + nobleStability * 0.7 + nuclearStability * 1.2;
    const double handlingRisk = clamp01((1.0 - nuclearStability) * 0.75 + fissionFuelTrait * 0.35 + reactivity * 0.10);
    const double safeHandling = 1.0 - handlingRisk;

    const double structuralTrait = clamp01((dPartial * 0.62 + covalentNetworkTrait * 0.55 + spMetalBond * 0.32) *
                                           specificStrength * (0.45 + 0.55 * safeHandling));
    // Проводимость: свободные s/p-электроны либо заполненная d-оболочка;
    // открытая f-оболочка рассеивает (редкие земли — плохие проводники).
    const double conductionBase = std::max(0.25 + 0.22 * std::min(shell.outerElectrons, 3.0),
                                           std::pow(shell.dFill, 3.0));
    // Подвижность носителей падает с массой; незаполненная d рассеивает электроны.
    const double carrierMobility = 1.0 / (0.55 + 0.45 * std::sqrt(atomicMass / 60.0));
    const double conductorTrait = clamp01(metallicTrait * conductionBase * carrierMobility *
                                          (1.0 - 0.75 * fPartial) / (1.0 + 0.55 * dPartial) *
                                          (1.05 - 0.25 * electronegativity));
    // Катализ: НЕЗАПОЛНЕННАЯ d-оболочка (свободные места для адсорбции),
    // тем ценнее, чем ближе к её заполнению — платиновая группа.
    const double catalystTrait = clamp01(std::sqrt(dPartial) * (0.3 + 0.7 * shell.dFill) *
                                         (1.0 - 0.8 * fPartial) *
                                         (0.45 + 0.55 * clamp01((shell.period - 3.0) / 3.0)) * 1.25);

    double abundance =
        9200.0 * std::exp(-1.02 * (dz - 1.0)) +
        540.0 * std::exp(-0.030 * dz) +
        80.0 * gaussian(dz, 26.0, 5.5);
    abundance *= std::exp(-0.025 * std::max(0.0, dz - 30.0));
    abundance *= 0.08 + 0.92 * std::pow(nuclearStability, 1.4);
    abundance *= deterministicNoise(z, 0.22);
    abundance = std::max(0.01, abundance);
    const double chemicalValue = oxidizerTrait * 0.9 + reducerTrait * 0.9 + reactivity * 0.45 + covalentNetworkTrait * 1.7;
    const double industrialValue = structuralTrait * 1.2 + conductorTrait * 1.15 + catalystTrait * 0.95 + nobleStability * 0.35;
    const double energyValue = fusionFuelTrait * 2.1 + fissionFuelTrait * 2.4;
    const double demand = std::max(0.08,
        0.35 + chemicalValue * 1.25 + industrialValue * 1.35 + energyValue * 1.55 + handlingRisk * 0.45);

    const double scarcityValue = 22.0 / std::sqrt(abundance + 1.0);
    const double prestigeMaterialTrait = clamp01(nobleStability * conductorTrait * structuralTrait * scarcityValue);
    double price =
        1.0 +
        demand * 3.2 +
        energyValue * 15.0 +
        chemicalValue * 4.0 +
        industrialValue * 5.0 +
        prestigeMaterialTrait * 18.0 +
        scarcityValue * 6.0 +
        handlingRisk * 8.0;
    price *= deterministicNoise(z + 149, 0.30);

    ElementDefinition element = ElementDefinition();
    element.atomicNumber = item.atomicNumber;
    element.symbol = item.symbol;
    element.name = item.name;
    element.atomicMass = atomicMass;
    element.abundanceWeight = abundance;
    element.demandWeight = demand;
    element.basePrice = std::max(0.1, price);
    element.valenceElectrons = shell.outerElectrons;
    element.shellFill = shellFill;
    element.nobleStability = nobleStability;
    element.oxidizerTrait = oxidizerTrait;
    element.reducerTrait = reducerTrait;
    element.metallicTrait = metallicTrait;
    element.structuralTrait = structuralTrait;
    element.conductorTrait = conductorTrait;
    element.catalystTrait = catalystTrait;
    element.fusionFuelTrait = fusionFuelTrait;
    element.fissionFuelTrait = fissionFuelTrait;
    element.nuclearStability = nuclearStability;
    element.activationCost = activationCost;
    element.handlingRisk = handlingRisk;
    element.period = shell.period;
    element.dShellFill = shell.dFill;
    element.fShellFill = shell.fFill;
    element.electronegativity = electronegativity;
    element.bindingPerNucleon = bindingPerNucleon;
    element.fissility = fissility;
    element.relativisticBoost = relativisticBoost;
    element.inertnessTrait = inertnessTrait;
    return element;
}

std::vector<ElementDefinition> buildElements() {
    const RawElement raw[] = {
        {1, "H", "Hydrogen"},
        {2, "He", "Helium"},
        {3, "Li", "Lithium"},
        {4, "Be", "Beryllium"},
        {5, "B", "Boron"},
        {6, "C", "Carbon"},
        {7, "N", "Nitrogen"},
        {8, "O", "Oxygen"},
        {9, "F", "Fluorine"},
        {10, "Ne", "Neon"},
        {11, "Na", "Sodium"},
        {12, "Mg", "Magnesium"},
        {13, "Al", "Aluminium"},
        {14, "Si", "Silicon"},
        {15, "P", "Phosphorus"},
        {16, "S", "Sulfur"},
        {17, "Cl", "Chlorine"},
        {18, "Ar", "Argon"},
        {19, "K", "Potassium"},
        {20, "Ca", "Calcium"},
        {21, "Sc", "Scandium"},
        {22, "Ti", "Titanium"},
        {23, "V", "Vanadium"},
        {24, "Cr", "Chromium"},
        {25, "Mn", "Manganese"},
        {26, "Fe", "Iron"},
        {27, "Co", "Cobalt"},
        {28, "Ni", "Nickel"},
        {29, "Cu", "Copper"},
        {30, "Zn", "Zinc"},
        {31, "Ga", "Gallium"},
        {32, "Ge", "Germanium"},
        {33, "As", "Arsenic"},
        {34, "Se", "Selenium"},
        {35, "Br", "Bromine"},
        {36, "Kr", "Krypton"},
        {37, "Rb", "Rubidium"},
        {38, "Sr", "Strontium"},
        {39, "Y", "Yttrium"},
        {40, "Zr", "Zirconium"},
        {41, "Nb", "Niobium"},
        {42, "Mo", "Molybdenum"},
        {43, "Tc", "Technetium"},
        {44, "Ru", "Ruthenium"},
        {45, "Rh", "Rhodium"},
        {46, "Pd", "Palladium"},
        {47, "Ag", "Silver"},
        {48, "Cd", "Cadmium"},
        {49, "In", "Indium"},
        {50, "Sn", "Tin"},
        {51, "Sb", "Antimony"},
        {52, "Te", "Tellurium"},
        {53, "I", "Iodine"},
        {54, "Xe", "Xenon"},
        {55, "Cs", "Caesium"},
        {56, "Ba", "Barium"},
        {57, "La", "Lanthanum"},
        {58, "Ce", "Cerium"},
        {59, "Pr", "Praseodymium"},
        {60, "Nd", "Neodymium"},
        {61, "Pm", "Promethium"},
        {62, "Sm", "Samarium"},
        {63, "Eu", "Europium"},
        {64, "Gd", "Gadolinium"},
        {65, "Tb", "Terbium"},
        {66, "Dy", "Dysprosium"},
        {67, "Ho", "Holmium"},
        {68, "Er", "Erbium"},
        {69, "Tm", "Thulium"},
        {70, "Yb", "Ytterbium"},
        {71, "Lu", "Lutetium"},
        {72, "Hf", "Hafnium"},
        {73, "Ta", "Tantalum"},
        {74, "W", "Tungsten"},
        {75, "Re", "Rhenium"},
        {76, "Os", "Osmium"},
        {77, "Ir", "Iridium"},
        {78, "Pt", "Platinum"},
        {79, "Au", "Gold"},
        {80, "Hg", "Mercury"},
        {81, "Tl", "Thallium"},
        {82, "Pb", "Lead"},
        {83, "Bi", "Bismuth"},
        {84, "Po", "Polonium"},
        {85, "At", "Astatine"},
        {86, "Rn", "Radon"},
        {87, "Fr", "Francium"},
        {88, "Ra", "Radium"},
        {89, "Ac", "Actinium"},
        {90, "Th", "Thorium"},
        {91, "Pa", "Protactinium"},
        {92, "U", "Uranium"},
        {93, "Np", "Neptunium"},
        {94, "Pu", "Plutonium"},
        {95, "Am", "Americium"},
        {96, "Cm", "Curium"},
        {97, "Bk", "Berkelium"},
        {98, "Cf", "Californium"},
        {99, "Es", "Einsteinium"},
        {100, "Fm", "Fermium"},
        {101, "Md", "Mendelevium"},
        {102, "No", "Nobelium"},
        {103, "Lr", "Lawrencium"},
        {104, "Rf", "Rutherfordium"},
        {105, "Db", "Dubnium"},
        {106, "Sg", "Seaborgium"},
        {107, "Bh", "Bohrium"},
        {108, "Hs", "Hassium"},
        {109, "Mt", "Meitnerium"},
        {110, "Ds", "Darmstadtium"},
        {111, "Rg", "Roentgenium"},
        {112, "Cn", "Copernicium"},
        {113, "Nh", "Nihonium"},
        {114, "Fl", "Flerovium"},
        {115, "Mc", "Moscovium"},
        {116, "Lv", "Livermorium"},
        {117, "Ts", "Tennessine"},
        {118, "Og", "Oganesson"}
    };

    std::vector<ElementDefinition> elements;
    elements.reserve(sizeof(raw) / sizeof(raw[0]));
    for (const RawElement& item : raw) {
        elements.push_back(deriveElement(item));
    }
    return elements;
}

}

const std::vector<ElementDefinition>& elementDefinitions() {
    static const std::vector<ElementDefinition> elements = buildElements();
    return elements;
}

const double RESOURCE_MASS_SCALE = 0.01;

double elementUnitMass(int elementIndex) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(elements.size())) return 1.0;
    return std::max(0.001, elements[elementIndex].atomicMass * RESOURCE_MASS_SCALE);
}

size_t elementCount() {
    return elementDefinitions().size();
}

int elementIndex(const std::string& element) {
    const auto& elements = elementDefinitions();
    for (size_t i = 0; i < elements.size(); ++i) {
        if (element == elements[i].symbol || element == elements[i].name) {
            return int(i);
        }
    }
    return -1;
}

Resource::Resource(const std::string& element_, double amount_)
    : element(element_), amount(amount_) {}
