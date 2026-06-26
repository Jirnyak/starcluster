#include "ship.h"
#include <algorithm>
#include <cmath>

namespace {

const double RESOURCE_MASS_SCALE = 0.01;

int defaultFuelElement() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    int best = 0;
    double bestScore = -1.0;
    for (size_t i = 0; i < elements.size(); ++i) {
        const ElementDefinition& element = elements[i];
        const double score = element.fusionFuelTrait / std::sqrt(std::max(1.0, element.atomicMass));
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

double fuelEnergyDensityByIndex(int elementIndex) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(elements.size())) return 0.0;
    const ElementDefinition& element = elements[elementIndex];
    const double nuclear =
        element.fusionFuelTrait * 4.0 +
        element.fissionFuelTrait * 4.6;
    return std::max(0.04, nuclear / std::max(0.15, element.activationCost));
}

}

Ship::Ship(const std::string& name_, double x_, double y_, double z_, double speed_, int ownerFaction_)
    : name(name_), x(x_), y(y_), z(z_), speed(std::min(0.5, std::max(0.1, speed_))), ownerFaction(ownerFaction_) {
    fuelElement = defaultFuelElement();
    shipAutofit(*this);
}

const std::vector<ShipClass>& shipClasses() {
    static std::vector<ShipClass> classes = {
        // Name, dryMass, thrust, eff, cargo, fuel, HW, LW, Armor, Utility, Price
        
        // Tier 0: Emergency
        {"Escape Pod", 5.0, 1.0, 0.40, 0.0, 200.0, 0.0, 0.0, 1.0, 0.0, 0.0},
        
        // Base (Tens of thousands)
        {"Light Courier", 30.0, 15.0, 0.65, 50.0, 2000.0, 0.0, 10.0, 5.0, 0.0, 15000.0},
        {"Smuggler Runabout", 45.0, 25.0, 0.60, 100.0, 3000.0, 5.0, 15.0, 10.0, 10.0, 22000.0},
        {"Light Freighter", 100.0, 20.0, 0.50, 300.0, 4000.0, 0.0, 5.0, 20.0, 0.0, 30000.0},
        {"Light Fighter", 40.0, 25.0, 0.60, 20.0, 1500.0, 10.0, 50.0, 10.0, 5.0, 50000.0},
        {"Interceptor", 35.0, 35.0, 0.70, 10.0, 1500.0, 5.0, 80.0, 5.0, 15.0, 80000.0},
        
        // Advanced (Hundreds of thousands)
        {"Heavy Courier", 60.0, 40.0, 0.70, 150.0, 4000.0, 0.0, 20.0, 15.0, 5.0, 150000.0},
        {"Mining Barge", 400.0, 30.0, 0.40, 1500.0, 12000.0, 20.0, 5.0, 150.0, 10.0, 200000.0},
        {"Medium Freighter", 250.0, 50.0, 0.55, 1000.0, 10000.0, 0.0, 10.0, 50.0, 10.0, 300000.0},
        {"Gunboat", 150.0, 60.0, 0.60, 50.0, 4000.0, 80.0, 50.0, 60.0, 20.0, 450000.0},
        {"Heavy Fighter", 80.0, 60.0, 0.65, 50.0, 3000.0, 50.0, 100.0, 30.0, 20.0, 600000.0},
        {"Assault Craft", 100.0, 80.0, 0.65, 60.0, 4000.0, 150.0, 50.0, 40.0, 30.0, 900000.0},
        
        // Industrial / Military (Millions)
        {"Corvette", 120.0, 120.0, 0.70, 200.0, 5000.0, 10.0, 300.0, 80.0, 50.0, 1500000.0},
        {"Frigate", 250.0, 150.0, 0.70, 400.0, 10000.0, 200.0, 150.0, 150.0, 80.0, 2000000.0},
        {"Heavy Bomber", 150.0, 100.0, 0.75, 100.0, 8000.0, 500.0, 10.0, 100.0, 20.0, 2500000.0},
        {"Blockade Runner", 200.0, 250.0, 0.80, 800.0, 15000.0, 50.0, 100.0, 120.0, 150.0, 4000000.0},
        {"Heavy Freighter", 1000.0, 150.0, 0.60, 4000.0, 30000.0, 10.0, 20.0, 200.0, 50.0, 8000000.0},
        {"Destroyer", 350.0, 250.0, 0.75, 400.0, 12000.0, 800.0, 200.0, 250.0, 120.0, 15000000.0},
        {"Cruiser", 400.0, 200.0, 0.70, 600.0, 15000.0, 400.0, 400.0, 400.0, 150.0, 25000000.0},
        {"Strike Cruiser", 450.0, 300.0, 0.75, 500.0, 20000.0, 1000.0, 500.0, 350.0, 200.0, 40000000.0},
        {"Megafreighter", 4000.0, 500.0, 0.65, 20000.0, 80000.0, 50.0, 50.0, 1000.0, 100.0, 80000000.0},
        
        // Capital (Billions)
        {"Battlecruiser", 2000.0, 1500.0, 0.80, 3000.0, 60000.0, 3000.0, 1000.0, 2000.0, 500.0, 5000000000.0},
        {"Carrier", 3000.0, 1200.0, 0.85, 10000.0, 80000.0, 1000.0, 5000.0, 1500.0, 1500.0, 8000000000.0},
        {"Super-Freighter", 10000.0, 1500.0, 0.65, 80000.0, 250000.0, 100.0, 100.0, 2500.0, 200.0, 9500000000.0},
        {"Giga-Freighter", 20000.0, 2500.0, 0.70, 150000.0, 400000.0, 200.0, 200.0, 5000.0, 500.0, 12000000000.0},
        {"Dreadnought", 8000.0, 6000.0, 0.85, 10000.0, 200000.0, 20000.0, 5000.0, 15000.0, 2000.0, 150000000000.0},
        
        // Super-Capital (Trillions)
        {"Leviathan", 50000.0, 50000.0, 0.90, 50000.0, 1000000.0, 150000.0, 20000.0, 80000.0, 10000.0, 2000000000000.0},
        {"Tera-Freighter", 200000.0, 30000.0, 0.80, 2000000.0, 5000000.0, 1000.0, 1000.0, 50000.0, 5000.0, 8000000000000.0},
        {"Titan", 100000.0, 120000.0, 0.95, 100000.0, 4000000.0, 800000.0, 100000.0, 300000.0, 50000.0, 50000000000000.0},
        {"Star Destroyer", 250000.0, 180000.0, 1.00, 300000.0, 10000000.0, 2000000.0, 250000.0, 800000.0, 100000.0, 200000000000000.0},
        {"Fortress", 500000.0, 200000.0, 1.00, 1000000.0, 20000000.0, 5000000.0, 500000.0, 2000000.0, 200000.0, 900000000000000.0}
    };
    return classes;
}

double resourceUnitMassByIndex(int elementIndex) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (elementIndex < 0 || elementIndex >= int(elements.size())) return 1.0;
    return std::max(0.001, elements[elementIndex].atomicMass * RESOURCE_MASS_SCALE);
}

double resourceUnitMass(const std::string& element) {
    const int index = elementIndex(element);
    return index >= 0 ? resourceUnitMassByIndex(index) : 1.0;
}

double shipCargoMass(const Ship& ship) {
    double mass = 0.0;
    for (size_t i = 0; i < ship.cargo.size(); ++i) {
        mass += ship.cargo[i].amount * resourceUnitMass(ship.cargo[i].element);
    }
    return mass;
}

double shipFuelMass(const Ship& ship) {
    return ship.fuel * resourceUnitMassByIndex(ship.fuelElement);
}

double shipTotalMass(const Ship& ship) {
    return std::max(1.0, ship.dryMass + shipCargoMass(ship) + shipFuelMass(ship));
}

double shipFuelFraction(const Ship& ship) {
    return ship.fuelCapacity > 0.0 ? std::max(0.0, std::min(1.0, ship.fuel / ship.fuelCapacity)) : 0.0;
}

double shipCurrentAcceleration(const Ship& ship) {
    if (ship.fuel <= 0.0) return 0.0;
    const double massLimited = ship.driveThrust / shipTotalMass(ship);
    return std::max(0.0, std::min(ship.acceleration, massLimited));
}

double shipFuelNeededForDeltaV(const Ship& ship, double deltaV) {
    if (deltaV <= 0.0) return 0.0;
    const double unitMass = resourceUnitMassByIndex(ship.fuelElement);
    const double effectiveEnergy = fuelEnergyDensityByIndex(ship.fuelElement) * std::max(0.05, ship.driveEfficiency);
    const double shipMass = shipTotalMass(ship);
    const double neededFuelMass = 0.5 * shipMass * deltaV * deltaV / effectiveEnergy;
    return neededFuelMass / unitMass;
}

double shipEstimateRouteFuel(const Ship& ship, double distance) {
    if (distance <= 0.0) return 0.0;
    const double accel = std::max(0.001, std::min(ship.acceleration, ship.driveThrust / shipTotalMass(ship)));
    const double peakSpeed = std::min(ship.speed, std::sqrt(distance * accel));
    return shipFuelNeededForDeltaV(ship, peakSpeed) * 2.0;
}

double shipConsumeFuelForDeltaV(Ship& ship, double desiredDeltaV) {
    if (desiredDeltaV <= 0.0 || ship.fuel <= 0.0) return 0.0;

    const double unitMass = resourceUnitMassByIndex(ship.fuelElement);
    const double availableFuelMass = ship.fuel * unitMass;
    const double neededFuelMass = shipFuelNeededForDeltaV(ship, desiredDeltaV) * unitMass;
    const double effectiveEnergy = fuelEnergyDensityByIndex(ship.fuelElement) * std::max(0.05, ship.driveEfficiency);
    const double shipMass = shipTotalMass(ship);

    if (neededFuelMass <= availableFuelMass) {
        ship.fuel -= neededFuelMass / unitMass;
        return desiredDeltaV;
    }

    const double possibleDeltaV = std::sqrt(std::max(0.0, 2.0 * availableFuelMass * effectiveEnergy / shipMass));
    ship.fuel = 0.0;
    return std::min(desiredDeltaV, possibleDeltaV);
}

void shipAutofit(Ship& ship) {
    if (ship.fuelElement < 0 || ship.fuelElement >= int(elementCount())) {
        ship.fuelElement = defaultFuelElement();
    }
    ship.dryMass = std::max(12.0, 26.0 + ship.cargoCapacity * 0.32 + ship.speed * 72.0);
    ship.driveThrust = std::max(1.0, ship.dryMass * ship.acceleration * (1.05 + ship.speed * 1.4));
    ship.fuelCapacity = std::max(800.0, 1500.0 + ship.cargoCapacity * 18.0 + ship.speed * 1800.0);
    ship.fuel = ship.fuelCapacity * 0.84;
}
