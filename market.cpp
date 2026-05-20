#include "market.h"
#include <algorithm>
#include <cmath>

Market::Market() {}

namespace {

double lowMassTrait(const ElementDefinition& element) {
    return 1.0 / std::sqrt(std::max(1.0, element.atomicMass));
}

double covalentNetworkTrait(const ElementDefinition& element) {
    const double middleShell = std::max(0.0, 1.0 - std::abs(element.shellFill - 0.5) * 2.0);
    return (1.0 - element.metallicTrait) * middleShell / std::sqrt(std::max(1.0, double(element.atomicNumber)));
}

double roleDemand(const std::string& role, const ElementDefinition& element) {
    double k = 1.0;
    const double energy = element.fusionFuelTrait * 1.7 + element.fissionFuelTrait * 1.9;
    const double chemistry = element.oxidizerTrait + element.reducerTrait + covalentNetworkTrait(element) * 1.6;
    const double machinery = element.structuralTrait * 1.4 + element.conductorTrait * 1.3 + element.catalystTrait;
    const double stableMaterial = element.nobleStability * (0.5 + element.conductorTrait + element.structuralTrait);
    const double volatileUtility = lowMassTrait(element) * (0.4 + element.oxidizerTrait + element.reducerTrait);

    if (role == "habitat") {
        k += chemistry * 1.1 + volatileUtility * 2.2 + stableMaterial * 0.5;
    } else if (role == "refinery") {
        k += energy * 0.8 + chemistry * 1.3 + element.catalystTrait * 1.4 + machinery * 0.5;
    } else if (role == "shipyard") {
        k += machinery * 2.1 + energy * 0.9 + stableMaterial * 0.8;
    } else if (role == "research") {
        k += element.conductorTrait * 1.5 + element.catalystTrait * 1.8 + stableMaterial + element.handlingRisk * 1.1;
    } else if (role == "military") {
        k += machinery * 1.3 + energy * 1.8 + element.structuralTrait * 1.5 + element.handlingRisk * 0.8;
    } else {
        k += energy * 0.7 + chemistry * 0.7 + machinery * 0.6 + volatileUtility * 0.7;
    }
    return k;
}

}

void Market::seed(const std::vector<Resource>& localResources, const std::vector<double>& demandBias, const std::string& role_, double population, double industry) {
    role = role_;
    supply.clear();
    demand.clear();
    prices.clear();
    productionRate.clear();
    demandRate.clear();

    const auto& elements = elementDefinitions();
    supply.reserve(elements.size());
    demand.reserve(elements.size());
    prices.reserve(elements.size());
    productionRate.reserve(elements.size());
    demandRate.reserve(elements.size());

    for (size_t i = 0; i < elements.size(); ++i) {
        const ElementDefinition& element = elements[i];
        const double localSupply = i < localResources.size() ? localResources[i].amount : 0.0;
        const double roleK = roleDemand(role, element);
        const double proceduralDemand = i < demandBias.size() ? demandBias[i] : 1.0;
        const double need = (population * 0.0015 + industry * 24.0) * element.demandWeight * roleK * proceduralDemand;
        const double production = std::max(0.002, localSupply * (0.00016 + industry * 0.00006));

        supply.emplace_back(element.symbol, localSupply);
        demand.emplace_back(element.symbol, std::max(0.05, need));
        productionRate.push_back(production);
        demandRate.push_back(std::max(0.002, need * 0.018));
        prices.push_back(element.basePrice);
    }

    updatePrices();
}

void Market::update(double dt) {
    for (size_t i = 0; i < supply.size(); ++i) {
        supply[i].amount += productionRate[i] * dt;
        demand[i].amount += demandRate[i] * dt;
        const double demandCap = demandRate[i] * 900.0 + elementDefinitions()[i].demandWeight * 180.0;
        if (demand[i].amount > demandCap) demand[i].amount = demandCap;
    }
    updatePrices();
}

void Market::updatePrices() {
    const auto& elements = elementDefinitions();
    for (size_t i = 0; i < prices.size(); ++i) {
        const double supplyAmt = std::max(0.001, supply[i].amount);
        const double demandAmt = std::max(0.0, demand[i].amount);
        const double localDemand = i < demandRate.size() ? demandRate[i] * 80.0 : 1.0;
        const double localSupply = i < productionRate.size() ? productionRate[i] * 70.0 : 1.0;
        const double scarcity = (demandAmt + localDemand + 1.0) / (supplyAmt + localSupply + 1.0);
        double price = elements[i].basePrice * std::pow(scarcity, 0.62);
        const double minPrice = std::max(0.1, elements[i].basePrice * 0.08);
        const double maxPrice = elements[i].basePrice * 80.0;
        prices[i] = std::min(maxPrice, std::max(minPrice, price));
    }
}

double Market::pricePressure() const {
    if (prices.empty()) return 1.0;
    const auto& elements = elementDefinitions();
    double sum = 0.0;
    for (size_t i = 0; i < prices.size(); ++i) {
        sum += prices[i] / elements[i].basePrice;
    }
    return sum / double(prices.size());
}
