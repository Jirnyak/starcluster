#pragma once
#include <string>
#include <vector>

class Ship;

// Слот модуля — для группировки в окне апгрейдов.
enum class ModuleSlot {
    Drive = 0,    // двигатель: скорость/ускорение
    Cargo = 1,    // трюм: грузоподъёмность
    Fuel = 2,     // баки: запас топлива
    Weapon = 3,   // вооружение: тяжёлое/лёгкое
    Defense = 4,  // защита: броня/корпус
    Sensor = 5,   // сенсоры: дальность обнаружения (utility)
    Special = 6   // особое
};

// Определение модуля. Бонусы "запекаются" в поля корабля при установке.
struct ModuleDef {
    std::string name;
    ModuleSlot slot;
    double price;
    double mass;          // прибавка к dryMass
    double cargoBonus;
    double fuelBonus;
    double speedBonus;
    double accelBonus;
    double heavyBonus;
    double lightBonus;
    double armorBonus;
    double hullBonus;
    double utilityBonus;
    int minShipyard;      // требуемый уровень верфи для установки
    std::string blurb;
};

const std::vector<ModuleDef>& moduleDefs();

// Применяет бонусы модуля к полям корабля (без вызова shipAutofit).
void applyModuleToShip(Ship& ship, const ModuleDef& def);
void removeModuleFromShip(Ship& ship, const ModuleDef& def);

const char* moduleSlotLabel(ModuleSlot slot);
