#pragma once
#include <string>
#include <vector>

// Element definition used by generation and local markets.
struct ElementDefinition {
    int atomicNumber;
    const char* symbol;
    const char* name;
    double atomicMass;
    double abundanceWeight;
    double demandWeight;
    double basePrice;
    double valenceElectrons;
    double shellFill;
    double nobleStability;
    double oxidizerTrait;
    double reducerTrait;
    double metallicTrait;
    double structuralTrait;
    double conductorTrait;
    double catalystTrait;
    double fusionFuelTrait;
    double fissionFuelTrait;
    double nuclearStability;
    double activationCost;
    double handlingRisk;
    // --- Физика конфигурации и ядра (источник всех траитов выше) ---
    double period;              // номер внешней оболочки
    double dShellFill;          // заполнение (n-1)d, 0..1
    double fShellFill;          // заполнение (n-2)f, 0..1
    double electronegativity;   // 0..1, алленовский проксик
    double bindingPerNucleon;   // МэВ/нуклон по Вайцзеккеру (пик у железа)
    double fissility;           // Z^2/A — насколько ядро склонно делиться
    double relativisticBoost;   // релятивистское сжатие внешней s-орбитали (Au, Pt, Hg)
    double inertnessTrait;      // химическая стойкость: замкнутая оболочка ИЛИ благородный металл
};

// Масса одной торговой единицы = atomicMass * RESOURCE_MASS_SCALE.
extern const double RESOURCE_MASS_SCALE;
double elementUnitMass(int elementIndex);

const std::vector<ElementDefinition>& elementDefinitions();
size_t elementCount();
int elementIndex(const std::string& element);

// Ресурс (элемент)
class Resource {
public:
    std::string element; // Символ элемента (H, He, Li, ...)
    double amount;       // Количество (масса)
    Resource(const std::string& element_, double amount_);
};
