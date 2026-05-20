#include "resource.h"
#include <algorithm>
#include <cmath>

namespace {

struct RawElement {
    int atomicNumber;
    const char* symbol;
    const char* name;
};

struct ShellState {
    double outerElectrons;
    double outerCapacity;
    double period;
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
    const int capacities[] = {2, 8, 8, 18, 18, 32, 32};
    int remaining = z;
    for (int i = 0; i < int(sizeof(capacities) / sizeof(capacities[0])); ++i) {
        if (remaining <= capacities[i]) {
            return ShellState{double(remaining), double(capacities[i]), double(i + 1)};
        }
        remaining -= capacities[i];
    }
    return ShellState{32.0, 32.0, 7.0};
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
    const double shellFill = clamp01(shell.outerElectrons / shell.outerCapacity);
    const double nobleStability = clamp01(std::pow(shellFill, 3.2));
    const double reactivity = 1.0 - nobleStability;
    const double middleShell = clamp01(1.0 - std::abs(shellFill - 0.5) * 2.0);
    const double lowMassTrait = 1.0 / std::sqrt(std::max(1.0, dz));

    const double atomicMass = atomicMassFor(z);
    const double oxidizerTrait = clamp01(reactivity * shellFill);
    const double reducerTrait = clamp01(reactivity * (1.0 - shellFill) * (0.45 + 0.55 * smoothstep(2.0, 24.0, dz)));
    const double metallicTrait = clamp01(smoothstep(3.0, 78.0, dz) * (0.35 + 0.65 * (1.0 - shellFill)));
    const double ironDistance = std::abs(dz - 26.0) / 92.0;
    const double nuclearStability = clamp01(1.0 - std::pow(ironDistance, 0.72));
    const double structuralTrait = clamp01(metallicTrait * std::sqrt(dz / 118.0) * (0.55 + 0.45 * nuclearStability));
    const double conductorTrait = clamp01(metallicTrait * (0.35 + 0.45 * nobleStability + 0.20 * middleShell));
    const double catalystTrait = clamp01((metallicTrait * middleShell + conductorTrait * 0.35) * (0.35 + 0.65 * reactivity));

    const double fusionFuelTrait = z < 26 ? clamp01(std::pow((26.0 - dz) / 25.0, 0.9) * lowMassTrait) : 0.0;
    const double fissileFraction = smoothstep(70.0, 118.0, dz) * (0.35 + 0.65 * reactivity);
    const double fissionFuelTrait = z > 26 ? clamp01(std::pow((dz - 26.0) / 92.0, 1.15) * fissileFraction) : 0.0;
    const double activationCost = 0.3 + nobleStability * 0.7 + nuclearStability * 1.2;
    const double handlingRisk = clamp01((1.0 - nuclearStability) * 0.65 + fissionFuelTrait * 0.55 + reactivity * 0.12);

    double abundance =
        9200.0 * std::exp(-1.02 * (dz - 1.0)) +
        540.0 * std::exp(-0.030 * dz) +
        80.0 * gaussian(dz, 26.0, 5.5);
    abundance *= std::exp(-0.025 * std::max(0.0, dz - 30.0));
    abundance *= 1.0 + 0.35 * nuclearStability;
    abundance *= deterministicNoise(z, 0.22);
    const double highInstability = smoothstep(82.0, 118.0, dz);
    abundance *= (1.0 - 0.92 * highInstability) * std::exp(-0.08 * highInstability * std::max(0.0, dz - 82.0));
    abundance = std::max(0.01, abundance);

    const double covalentNetworkTrait = clamp01((1.0 - metallicTrait) * middleShell * std::exp(-0.055 * std::max(0.0, dz - 6.0)));
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
