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
    // --- Двигательная модель (см. ship.md, «Fuel As Resources») ---
    double density;             // масса на единицу объёма конденсированной фазы
    double ionizationEase;      // 0..1, насколько дёшево ободрать внешний электрон
    double specificEnergy;      // МэВ/нуклон, доступно при движении ядра к железу
};

// Ядерная энергия на единицу МАССЫ топлива. Одна формула на синтез и деление:
// расстояние до пика кривой связи. Ноль ровно на железе, растёт в обе стороны.
double elementSpecificEnergy(int elementIndex);
// Объём одной торговой единицы: масса единицы, делённая на плотность.
double elementUnitVolume(int elementIndex);

// Масса одной торговой единицы = atomicMass * RESOURCE_MASS_SCALE.
extern const double RESOURCE_MASS_SCALE;
// Энергия покоя нуклона в МэВ. Переводит МэВ/нуклон в доли c², благодаря чему
// ядерная энергия и кинетическая энергия струи считаются в одних единицах.
extern const double NUCLEON_REST_MEV;
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
