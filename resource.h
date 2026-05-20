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
};

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
