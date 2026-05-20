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
