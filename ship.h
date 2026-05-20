#pragma once
#include <string>
#include <vector>
#include "resource.h"

// Корабль
class Ship {
public:
    std::string name;
    double x, y, z; // Положение
    double speed;   // Максимальная скорость в долях c
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    double acceleration = 0.18; // c в год
    double dryMass = 50.0;
    double driveThrust = 14.0;
    double driveEfficiency = 0.62;
    int fuelElement = 0;
    double fuel = 0.0;
    double fuelCapacity = 3200.0;
    std::vector<Resource> cargo;
    double cargoCapacity = 100.0; // Максимальный груз (масса)
    int ownerFaction; // -1 если свободный
    int targetStar = -1; // Индекс звезды назначения
    bool enRoute = false; // В пути ли корабль
    Ship(const std::string& name_, double x_, double y_, double z_, double speed_, int ownerFaction_);
};

double resourceUnitMassByIndex(int elementIndex);
double resourceUnitMass(const std::string& element);
double shipCargoMass(const Ship& ship);
double shipFuelMass(const Ship& ship);
double shipTotalMass(const Ship& ship);
double shipFuelFraction(const Ship& ship);
double shipCurrentAcceleration(const Ship& ship);
double shipFuelNeededForDeltaV(const Ship& ship, double deltaV);
double shipEstimateRouteFuel(const Ship& ship, double distance);
double shipConsumeFuelForDeltaV(Ship& ship, double desiredDeltaV);
void shipAutofit(Ship& ship);
