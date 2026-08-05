#include "ship.h"
#include "drive.h"
#include "modules.h"
#include <algorithm>
#include <cmath>

// Маршрутный delta-V сжимается перед Циолковским: см. комментарий в ship.h.
const double DELTAV_SCALE = 0.05;

namespace {

// Лучшее стартовое рабочее тело для тепловой камеры — самое лёгкое, что есть:
// скорость истечения идёт как 1/sqrt(A).
int defaultPropellantElement() {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    int best = 0;
    double bestMass = 1e18;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].atomicMass < bestMass) {
            bestMass = elements[i].atomicMass;
            best = int(i);
        }
    }
    return best;
}

// Стартовое топливо. Критерий — не «где больше энергии», а «где больше энергии,
// до которой можно ДОБРАТЬСЯ, которую можно ХРАНИТЬ и которую можно КУПИТЬ»:
//   specificEnergy * density   — энергия в единице объёма (бункер ограничен объёмом)
//   доля поджига движка        — реально ли её извлечь
//   nuclearStability           — доживёт ли вещество до заправки
//   / basePrice                — в цену уже зашиты распространённость и риск
// Все множители непрерывны и считаются для всех 118 элементов одинаково.
// По этому критерию побеждает деление: у актинидов энергии в единице объёма
// впятеро больше, чем у водорода, хотя на единицу массы водород сильнее в
// восемь раз. Ровно поэтому настоящий ЯРД — это урановая активная зона,
// греющая водород: топливо компактное, рабочее тело лёгкое.
int defaultFuelElement(int driveIndex) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    int best = 0;
    double bestScore = -1.0;
    for (size_t i = 0; i < elements.size(); ++i) {
        const ElementDefinition& e = elements[i];
        const double score = e.specificEnergy * e.density * driveIgnitionFraction(driveIndex, int(i)) *
                             e.nuclearStability /
                             (std::max(0.1, e.basePrice) * (1.0 + e.handlingRisk * 0.8));
        if (score > bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

double listAmountOf(const std::vector<Resource>& list, const std::string& symbol) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].element == symbol) return list[i].amount;
    }
    return 0.0;
}

void listAdd(std::vector<Resource>& list, const std::string& symbol, double units) {
    if (units <= 0.0) return;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].element != symbol) continue;
        list[i].amount += units;
        return;
    }
    list.push_back(Resource(symbol, units));
}

double listTake(std::vector<Resource>& list, const std::string& symbol, double units) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].element != symbol) continue;
        const double moved = std::min(units, list[i].amount);
        list[i].amount -= moved;
        if (list[i].amount <= 1e-9) list.erase(list.begin() + i);
        return moved;
    }
    return 0.0;
}

double listVolume(const std::vector<Resource>& list) {
    double volume = 0.0;
    for (size_t i = 0; i < list.size(); ++i) {
        const int idx = elementIndex(list[i].element);
        if (idx < 0) continue;
        volume += list[i].amount * elementUnitVolume(idx);
    }
    return volume;
}

// Снимает со смеси заданную МАССУ, забирая у компонентов пропорционально их
// вкладу. Возвращает, что именно списано, — это нужно для золы.
std::vector<Resource> listConsumeMass(std::vector<Resource>& list, double mass) {
    std::vector<Resource> taken;
    if (mass <= 0.0) return taken;
    double total = 0.0;
    for (size_t i = 0; i < list.size(); ++i) {
        const int idx = elementIndex(list[i].element);
        if (idx >= 0) total += list[i].amount * elementUnitMass(idx);
    }
    if (total <= 0.0) return taken;
    const double share = std::min(1.0, mass / total);
    for (size_t i = 0; i < list.size(); ) {
        const int idx = elementIndex(list[i].element);
        if (idx < 0) { ++i; continue; }
        const double units = list[i].amount * share;
        if (units > 1e-12) taken.push_back(Resource(list[i].element, units));
        list[i].amount -= units;
        if (list[i].amount <= 1e-9) list.erase(list.begin() + i); else ++i;
    }
    return taken;
}

// Средневзвешенные по массе свойства смеси. ignitionDrive >= 0 включает учёт
// доли поджига (для топлива); -1 оставляет чистую ядерную энергию.
MixSummary summarize(const std::vector<Resource>& list, int ignitionDrive) {
    MixSummary out;
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    double energyMass = 0.0;
    for (size_t i = 0; i < list.size(); ++i) {
        const int idx = elementIndex(list[i].element);
        if (idx < 0 || idx >= int(elements.size())) continue;
        const ElementDefinition& e = elements[idx];
        const double m = list[i].amount * elementUnitMass(idx);
        if (m <= 0.0) continue;
        out.mass += m;
        out.volume += list[i].amount * elementUnitVolume(idx);
        out.meanAtomicMass += m * e.atomicMass;
        out.meanIonizationEase += m * e.ionizationEase;
        out.meanHandlingRisk += m * e.handlingRisk;
        const double ignition = ignitionDrive >= 0 ? driveIgnitionFraction(ignitionDrive, idx) : 1.0;
        energyMass += m * e.specificEnergy * ignition;
    }
    if (out.mass <= 0.0) {
        out.meanAtomicMass = 1.0;
        return out;
    }
    out.meanAtomicMass /= out.mass;
    out.meanIonizationEase /= out.mass;
    out.meanHandlingRisk /= out.mass;
    out.specificEnergy = energyMass / out.mass;
    return out;
}

}

Ship::Ship(const std::string& name_, double x_, double y_, double z_, double speed_, int ownerFaction_)
    : name(name_), x(x_), y(y_), z(z_), speed(std::min(0.5, std::max(0.1, speed_))), ownerFaction(ownerFaction_) {
    driveIndex = defaultDriveIndex();
    shipAutofit(*this);
}

const std::vector<ShipClass>& shipClasses() {
    static std::vector<ShipClass> classes = {
        // Name, dryMass, thrust, eff, cargo, propellantVol, fuelVol, HW, LW, Armor, Utility, Price, maxModules
        // Объёмы баков заданы в единицах ОБЪЁМА: сколько влезет по массе, решает
        // плотность залитого элемента. Водородный бак вмещает мало массы, но
        // даёт высокую скорость истечения; ртутный — наоборот.

        // Tier 0: Emergency
        {"Escape Pod", 5.0, 1.0, 0.40, 0.0, 12.0, 1.2, 0.0, 0.0, 1.0, 0.0, 0.0, 1},

        // Стартовый корпус игрока. Раньше он строился вручную в Game::init под
        // именем "Player" и в этом списке НЕ ЧИСЛИЛСЯ, из-за чего зачёт старого
        // корпуса в цену нового (`buyShip`) не срабатывал на ПЕРВОЙ же покупке:
        // игрок платил полную цену. Теперь корпус — обычный класс, лестница цен
        // непрерывна, а зачёт работает с первой сделки.
        {"Hauler", 60.0, 18.0, 0.55, 110.0, 240.0, 24.0, 0.0, 5.0, 8.0, 0.0, 10000.0, 3},

        // Base (Tens of thousands)
        {"Light Courier", 30.0, 15.0, 0.65, 50.0, 120.0, 12.0, 0.0, 10.0, 5.0, 0.0, 15000.0, 3},
        {"Smuggler Runabout", 45.0, 25.0, 0.60, 100.0, 180.0, 18.0, 5.0, 15.0, 10.0, 10.0, 22000.0, 4},
        {"Light Freighter", 100.0, 20.0, 0.50, 300.0, 240.0, 24.0, 0.0, 5.0, 20.0, 0.0, 30000.0, 2},
        {"Light Fighter", 40.0, 25.0, 0.60, 20.0, 90.0, 9.0, 10.0, 50.0, 10.0, 5.0, 50000.0, 2},
        {"Interceptor", 35.0, 35.0, 0.70, 10.0, 90.0, 9.0, 5.0, 80.0, 5.0, 15.0, 80000.0, 1},

        // Advanced (Hundreds of thousands)
        {"Heavy Courier", 60.0, 40.0, 0.70, 150.0, 240.0, 24.0, 0.0, 20.0, 15.0, 5.0, 150000.0, 4},
        {"Mining Barge", 400.0, 30.0, 0.40, 1500.0, 720.0, 72.0, 20.0, 5.0, 150.0, 10.0, 200000.0, 5},
        {"Medium Freighter", 250.0, 50.0, 0.55, 1000.0, 600.0, 60.0, 0.0, 10.0, 50.0, 10.0, 300000.0, 3},
        {"Gunboat", 150.0, 60.0, 0.60, 50.0, 240.0, 24.0, 80.0, 50.0, 60.0, 20.0, 450000.0, 3},
        {"Heavy Fighter", 80.0, 60.0, 0.65, 50.0, 180.0, 18.0, 50.0, 100.0, 30.0, 20.0, 600000.0, 2},
        {"Assault Craft", 100.0, 80.0, 0.65, 60.0, 240.0, 24.0, 150.0, 50.0, 40.0, 30.0, 900000.0, 4},

        // Industrial / Military (Millions)
        {"Corvette", 120.0, 120.0, 0.70, 200.0, 300.0, 30.0, 10.0, 300.0, 80.0, 50.0, 1500000.0, 4},
        {"Frigate", 250.0, 150.0, 0.70, 400.0, 600.0, 60.0, 200.0, 150.0, 150.0, 80.0, 2000000.0, 5},
        {"Heavy Bomber", 150.0, 100.0, 0.75, 100.0, 480.0, 48.0, 500.0, 10.0, 100.0, 20.0, 2500000.0, 3},
        {"Blockade Runner", 200.0, 250.0, 0.80, 800.0, 900.0, 90.0, 50.0, 100.0, 120.0, 150.0, 4000000.0, 6},
        {"Heavy Freighter", 1000.0, 150.0, 0.60, 4000.0, 1800.0, 180.0, 10.0, 20.0, 200.0, 50.0, 8000000.0, 4},
        {"Destroyer", 350.0, 250.0, 0.75, 400.0, 720.0, 72.0, 800.0, 200.0, 250.0, 120.0, 15000000.0, 5},
        {"Cruiser", 400.0, 200.0, 0.70, 600.0, 900.0, 90.0, 400.0, 400.0, 400.0, 150.0, 25000000.0, 6},
        {"Strike Cruiser", 450.0, 300.0, 0.75, 500.0, 1200.0, 120.0, 1000.0, 500.0, 350.0, 200.0, 40000000.0, 5},
        {"Megafreighter", 4000.0, 500.0, 0.65, 20000.0, 4800.0, 480.0, 50.0, 50.0, 1000.0, 100.0, 80000000.0, 6},

        // Capital (Billions)
        {"Battlecruiser", 2000.0, 1500.0, 0.80, 3000.0, 3600.0, 360.0, 3000.0, 1000.0, 2000.0, 500.0, 5000000000.0, 7},
        {"Carrier", 3000.0, 1200.0, 0.85, 10000.0, 4800.0, 480.0, 1000.0, 5000.0, 1500.0, 1500.0, 8000000000.0, 8},
        {"Super-Freighter", 10000.0, 1500.0, 0.65, 80000.0, 15000.0, 1500.0, 100.0, 100.0, 2500.0, 200.0, 9500000000.0, 7},
        {"Giga-Freighter", 20000.0, 2500.0, 0.70, 150000.0, 24000.0, 2400.0, 200.0, 200.0, 5000.0, 500.0, 12000000000.0, 8},
        {"Dreadnought", 8000.0, 6000.0, 0.85, 10000.0, 12000.0, 1200.0, 20000.0, 5000.0, 15000.0, 2000.0, 150000000000.0, 9},

        // Super-Capital (Trillions)
        {"Leviathan", 50000.0, 50000.0, 0.90, 50000.0, 60000.0, 6000.0, 150000.0, 20000.0, 80000.0, 10000.0, 2000000000000.0, 10},
        {"Tera-Freighter", 200000.0, 30000.0, 0.80, 2000000.0, 300000.0, 30000.0, 1000.0, 1000.0, 50000.0, 5000.0, 8000000000000.0, 12},
        {"Titan", 100000.0, 120000.0, 0.95, 100000.0, 240000.0, 24000.0, 800000.0, 100000.0, 300000.0, 50000.0, 50000000000000.0, 12},
        {"Star Destroyer", 250000.0, 180000.0, 1.00, 300000.0, 600000.0, 60000.0, 2000000.0, 250000.0, 800000.0, 100000.0, 200000000000000.0, 15},
        {"Fortress", 500000.0, 200000.0, 1.00, 1000000.0, 1200000.0, 120000.0, 5000000.0, 500000.0, 2000000.0, 200000.0, 900000000000000.0, 20}
    };
    return classes;
}

double resourceUnitMassByIndex(int elementIndex) {
    return elementUnitMass(elementIndex);
}

double resourceUnitMass(const std::string& element) {
    if (element.rfind("Module: ", 0) == 0) {
        std::string modName = element.substr(8);
        for (const auto& def : moduleDefs()) {
            if (def.name == modName) return def.mass;
        }
    }
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

MixSummary shipFuelMix(const Ship& ship) {
    return summarize(ship.fuel, ship.driveIndex);
}

MixSummary shipPropellantMix(const Ship& ship) {
    return summarize(ship.propellant, -1);
}

MixSummary shipExhaustMix(const Ship& ship) {
    // У факела из сопла летит само топливо, поэтому и свойства струи берутся
    // у топливной смеси — включая долю поджига.
    return driveUsesFuelAsPropellant(ship.driveIndex) ? shipFuelMix(ship) : shipPropellantMix(ship);
}

double shipFuelMass(const Ship& ship) {
    return shipFuelMix(ship).mass;
}

double shipPropellantMass(const Ship& ship) {
    return shipPropellantMix(ship).mass;
}

double shipTotalMass(const Ship& ship) {
    return std::max(1.0, ship.dryMass + shipCargoMass(ship) + shipFuelMass(ship) + shipPropellantMass(ship));
}

double shipFuelTankVolume(const Ship& ship) {
    if (driveUsesFuelAsPropellant(ship.driveIndex)) return ship.fuelVolume + ship.propellantVolume;
    return ship.fuelVolume;
}

double shipFuelFill(const Ship& ship) {
    const double capacity = shipFuelTankVolume(ship);
    if (capacity <= 0.0) return 0.0;
    return std::max(0.0, std::min(1.0, listVolume(ship.fuel) / capacity));
}

double shipPropellantFill(const Ship& ship) {
    if (driveUsesFuelAsPropellant(ship.driveIndex)) return shipFuelFill(ship);
    if (ship.propellantVolume <= 0.0) return 0.0;
    return std::max(0.0, std::min(1.0, listVolume(ship.propellant) / ship.propellantVolume));
}

double shipMaxExhaustVelocity(const Ship& ship) {
    return driveExhaustCeiling(ship.driveIndex, shipExhaustMix(ship));
}

double shipCurrentAcceleration(const Ship& ship) {
    if (shipFuelMix(ship).mass <= 0.0) return 0.0;
    if (!driveUsesFuelAsPropellant(ship.driveIndex) && shipPropellantMix(ship).mass <= 0.0) return 0.0;
    const std::vector<DriveDef>& drives = driveDefs();
    const double thrustCoeff = (ship.driveIndex >= 0 && ship.driveIndex < int(drives.size()))
        ? drives[ship.driveIndex].thrustCoeff : 1.0;
    const double massLimited = ship.driveThrust * thrustCoeff / shipTotalMass(ship);
    return std::max(0.0, std::min(ship.acceleration, massLimited));
}

RouteCost shipRouteCost(const Ship& ship, double deltaV, double propellantPrice, double fuelPrice) {
    RouteCost result;
    if (deltaV <= 0.0) {
        result.feasible = true;
        return result;
    }

    const bool torch = driveUsesFuelAsPropellant(ship.driveIndex);
    const MixSummary fuelMix = shipFuelMix(ship);
    const MixSummary exhaustMix = shipExhaustMix(ship);

    const double ceiling = driveExhaustCeiling(ship.driveIndex, exhaustMix);
    if (ceiling <= 1e-6) return result;

    const double jetEfficiency = driveJetEfficiency(ship.driveIndex, exhaustMix);
    // Энергия на единицу массы в долях c²: МэВ/нуклон, делённые на энергию покоя
    // нуклона. Так ядерная и кинетическая энергия живут в одних единицах.
    const double energyPerMass = fuelMix.specificEnergy / NUCLEON_REST_MEV * jetEfficiency;
    if (energyPerMass <= 1e-12) return result;

    // Сухая масса на конец манёвра: корпус и груз — то, что не сгорит.
    const double finalMass = std::max(1.0, ship.dryMass + shipCargoMass(ship));
    const double effectiveDeltaV = deltaV * DELTAV_SCALE;

    // Факел жжёт топливо как рабочее тело: скорость истечения не выбирается,
    // её задаёт сама реакция, и расход считается одним потоком.
    if (torch) {
        const double ratio = std::exp(std::min(60.0, effectiveDeltaV / ceiling));
        const double burnMass = finalMass * (ratio - 1.0);
        if (!std::isfinite(burnMass)) return result;
        result.feasible = true;
        result.exhaustVelocity = ceiling;
        result.fuelMass = burnMass;
        return result;
    }

    // Стоимость манёвра как функция скорости истечения:
    //   cost(v) = (e^(dV/v) - 1) * M * [ Pp + Pf*v^2 / (2*E) ]
    // Слева экспонента наказывает низкую скорость, справа энергия наказывает
    // высокую. Минимум внутри — его и ищем сканированием по логарифму.
    //
    // Оптимум обязан ВЛЕЗАТЬ в баки: дешёвый режим, под который негде возить
    // рабочее тело, бесполезен. Поэтому среди влезающих берём самый дешёвый,
    // а если не влезает ни один — тот, что переполняет меньше всех.
    const double priceP = std::max(0.0, propellantPrice);
    const double priceF = std::max(0.0, fuelPrice);
    const double veMin = std::max(1e-4, ceiling * 0.02);
    // Плотность смесей: сколько объёма занимает единица массы каждого потока.
    const double propVolumePerMass = exhaustMix.mass > 0.0 ? exhaustMix.volume / exhaustMix.mass : 1.0;
    const double fuelVolumePerMass = fuelMix.mass > 0.0 ? fuelMix.volume / fuelMix.mass : 1.0;

    double bestVe = ceiling;
    double bestCost = -1.0;
    double bestOverflow = -1.0;
    double bestOverflowVe = ceiling;
    const int samples = 48;
    for (int i = 0; i < samples; ++i) {
        const double t = double(i) / double(samples - 1);
        const double ve = veMin * std::pow(ceiling / veMin, t);
        const double ratio = std::exp(std::min(60.0, effectiveDeltaV / ve));
        if (!std::isfinite(ratio)) continue;
        const double propMass = finalMass * (ratio - 1.0);
        const double fuelMass = 0.5 * propMass * ve * ve / energyPerMass;
        if (!std::isfinite(propMass) || !std::isfinite(fuelMass)) continue;

        const double overflow = std::max(
            propMass * propVolumePerMass / std::max(1e-6, ship.propellantVolume),
            fuelMass * fuelVolumePerMass / std::max(1e-6, shipFuelTankVolume(ship)));
        if (bestOverflow < 0.0 || overflow < bestOverflow) {
            bestOverflow = overflow;
            bestOverflowVe = ve;
        }
        if (overflow > 1.0) continue;

        const double cost = priceP * propMass + priceF * fuelMass;
        if (bestCost < 0.0 || cost < bestCost) {
            bestCost = cost;
            bestVe = ve;
        }
    }
    if (bestCost < 0.0) {
        if (bestOverflow < 0.0) return result;
        bestVe = bestOverflowVe;
    }

    // Ручка игрока выбирает точку внутри полосы [минимум .. оптимум .. потолок].
    // Интерполяция логарифмическая, потому что и стоимость, и массовое число
    // зависят от скорости истечения экспоненциально: в логарифме ручка идёт
    // равномерно, а в линейной шкале half-way ощущался бы как «почти форсаж».
    const double t = std::max(0.0, std::min(1.0, ship.throttle));
    const double lowEnd = std::min(bestVe, veMin);
    const double highEnd = std::max(bestVe, ceiling);
    const double ve = t <= 0.5
        ? lowEnd * std::pow(bestVe / std::max(1e-9, lowEnd), t * 2.0)
        : bestVe * std::pow(highEnd / std::max(1e-9, bestVe), (t - 0.5) * 2.0);

    const double ratio = std::exp(std::min(60.0, effectiveDeltaV / ve));
    const double propMass = finalMass * (ratio - 1.0);
    const double fuelMass = 0.5 * propMass * ve * ve / energyPerMass;
    if (!std::isfinite(propMass) || !std::isfinite(fuelMass)) return result;

    result.feasible = true;
    result.exhaustVelocity = ve;
    result.propellantMass = propMass;
    result.fuelMass = fuelMass;
    return result;
}

RouteCost shipEstimateRoute(const Ship& ship, double distance, double propellantPrice, double fuelPrice) {
    if (distance <= 0.0) {
        RouteCost zero;
        zero.feasible = true;
        return zero;
    }
    const double accel = std::max(0.001, std::min(ship.acceleration, ship.driveThrust / shipTotalMass(ship)));
    const double peakSpeed = std::min(ship.speed, std::sqrt(distance * accel));
    // Разгон и торможение симметричны (ship.md, «Movement Fuel Cost», вариант 1).
    return shipRouteCost(ship, peakSpeed * 2.0, propellantPrice, fuelPrice);
}

bool shipCanFlyDistance(const Ship& ship, double distance) {
    if (distance <= 0.0) return true;
    const RouteCost cost = shipEstimateRoute(ship, distance, 1.0, 1.0);
    if (!cost.feasible) return false;
    if (shipFuelMix(ship).mass + 1e-6 < cost.fuelMass) return false;
    if (!driveUsesFuelAsPropellant(ship.driveIndex) &&
        shipPropellantMix(ship).mass + 1e-6 < cost.propellantMass) return false;
    return true;
}

int elementAshProduct(int fuelElement) {
    const std::vector<ElementDefinition>& elements = elementDefinitions();
    if (fuelElement < 0 || fuelElement >= int(elements.size())) return -1;
    const int z = elements[fuelElement].atomicNumber;
    // Обе ветви идут к железу: лёгкие ядра сливаются вверх, тяжёлые делятся вниз.
    // На самом пике жечь нечего, золы тоже нет.
    if (z == 26) return -1;
    const int target = z < 26 ? std::min(26, z * 2) : std::max(1, z / 2);
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].atomicNumber == target) return int(i);
    }
    return -1;
}

double shipConsumeForDeltaV(Ship& ship, double desiredDeltaV, std::vector<Resource>* ashOut) {
    if (desiredDeltaV <= 0.0) return 0.0;
    const bool torch = driveUsesFuelAsPropellant(ship.driveIndex);
    const double fuelHave = shipFuelMix(ship).mass;
    const double propHave = shipPropellantMix(ship).mass;
    if (fuelHave <= 0.0) return 0.0;
    if (!torch && propHave <= 0.0) return 0.0;

    RouteCost need = shipRouteCost(ship, desiredDeltaV, 1.0, 1.0);
    if (!need.feasible) return 0.0;

    double achieved = desiredDeltaV;
    // Не хватает запаса — урезаем манёвр до того, что реально вытянем. Ищем
    // максимальный deltaV, влезающий в остаток, делением отрезка пополам.
    const bool short_ = need.fuelMass > fuelHave || (!torch && need.propellantMass > propHave);
    if (short_) {
        double low = 0.0;
        double high = desiredDeltaV;
        for (int i = 0; i < 40; ++i) {
            const double mid = 0.5 * (low + high);
            const RouteCost trial = shipRouteCost(ship, mid, 1.0, 1.0);
            const bool ok = trial.feasible && trial.fuelMass <= fuelHave &&
                            (torch || trial.propellantMass <= propHave);
            if (ok) low = mid; else high = mid;
        }
        achieved = low;
        need = shipRouteCost(ship, achieved, 1.0, 1.0);
        if (!need.feasible) return 0.0;
    }

    // Расход снимается со смеси пропорционально долям компонентов.
    const std::vector<Resource> burned = listConsumeMass(ship.fuel, need.fuelMass);
    if (!torch) listConsumeMass(ship.propellant, need.propellantMass);

    // Топливо из активной зоны не улетает — оно перегорает. Дефект массы порядка
    // процента, так что зола весит практически столько же, сколько сгорело.
    // У факела продукты синтеза уходят в сопло, поэтому золы нет вовсе.
    if (ashOut && !torch) {
        const std::vector<ElementDefinition>& elements = elementDefinitions();
        for (size_t i = 0; i < burned.size(); ++i) {
            const int from = elementIndex(burned[i].element);
            const int ash = elementAshProduct(from);
            if (from < 0 || ash < 0) continue;
            const double burnedMass = burned[i].amount * elementUnitMass(from);
            const double defect = elements[from].specificEnergy / NUCLEON_REST_MEV;
            const double ashMass = burnedMass * std::max(0.0, 1.0 - defect);
            const double ashUnits = ashMass / std::max(1e-6, elementUnitMass(ash));
            if (ashUnits > 1e-9) ashOut->push_back(Resource(elements[ash].symbol, ashUnits));
        }
    }
    return achieved;
}

namespace {

// Общий перелив трюм -> ёмкость. Запретов по элементам нет: ограничивают
// только свободный объём и наличие в трюме.
double loadInto(Ship& ship, std::vector<Resource>& dest, double capacity, int elementIdx, double units) {
    if (units <= 0.0 || elementIdx < 0 || elementIdx >= int(elementCount())) return 0.0;
    const std::string symbol = elementDefinitions()[elementIdx].symbol;
    const double free = std::max(0.0, capacity - listVolume(dest));
    const double roomUnits = free / std::max(1e-9, elementUnitVolume(elementIdx));
    const double moved = std::min(std::min(units, roomUnits), listAmountOf(ship.cargo, symbol));
    if (moved <= 1e-9) return 0.0;
    listTake(ship.cargo, symbol, moved);
    listAdd(dest, symbol, moved);
    return moved;
}

// Слив ёмкость -> трюм: нужен, чтобы сменить схему или продать остаток.
// Вместимость трюма НЕ ограничивает слив. cargoCapacity — паспортная норма,
// а не физическая стена: перегрузиться можно всегда, за это просто нельзя
// будет взлететь (см. startJourney). Иначе игрок не смог бы вылить бак,
// чтобы сменить топливную схему, и застревал бы намертво.
double drainInto(Ship& ship, std::vector<Resource>& src, int elementIdx, double units) {
    if (units <= 0.0 || elementIdx < 0 || elementIdx >= int(elementCount())) return 0.0;
    const std::string symbol = elementDefinitions()[elementIdx].symbol;
    const double moved = std::min(units, listAmountOf(src, symbol));
    if (moved <= 1e-9) return 0.0;
    listTake(src, symbol, moved);
    listAdd(ship.cargo, symbol, moved);
    return moved;
}

}

namespace {
int dominantOf(const std::vector<Resource>& list, int fallback) {
    int best = fallback;
    double bestMass = 0.0;
    for (size_t i = 0; i < list.size(); ++i) {
        const int idx = elementIndex(list[i].element);
        if (idx < 0) continue;
        const double m = list[i].amount * elementUnitMass(idx);
        if (m > bestMass) { bestMass = m; best = idx; }
    }
    return best;
}
}

double shipCargoOverload(const Ship& ship) {
    return std::max(0.0, shipCargoMass(ship) - ship.cargoCapacity);
}

int shipDominantFuelElement(const Ship& ship) {
    return dominantOf(ship.fuel, defaultFuelElement(ship.driveIndex));
}

int shipDominantPropellantElement(const Ship& ship) {
    return dominantOf(ship.propellant, defaultPropellantElement());
}

double shipLoadFuel(Ship& ship, int elementIdx, double units) {
    return loadInto(ship, ship.fuel, shipFuelTankVolume(ship), elementIdx, units);
}

double shipLoadPropellant(Ship& ship, int elementIdx, double units) {
    if (driveUsesFuelAsPropellant(ship.driveIndex)) return 0.0;
    return loadInto(ship, ship.propellant, ship.propellantVolume, elementIdx, units);
}

double shipDrainFuel(Ship& ship, int elementIdx, double units) {
    return drainInto(ship, ship.fuel, elementIdx, units);
}

double shipDrainPropellant(Ship& ship, int elementIdx, double units) {
    return drainInto(ship, ship.propellant, elementIdx, units);
}

namespace {

// Заливает ёмкость одним элементом до заданной доли объёма.
void fillTank(std::vector<Resource>& dest, int elementIdx, double capacity, double fraction) {
    dest.clear();
    if (elementIdx < 0 || capacity <= 0.0 || fraction <= 0.0) return;
    const double units = capacity * fraction / std::max(1e-9, elementUnitVolume(elementIdx));
    if (units > 1e-9) dest.push_back(Resource(elementDefinitions()[elementIdx].symbol, units));
}

}

void shipApplyClass(Ship& ship, const ShipClass& sc) {
    ship.cargoCapacity = sc.cargoCapacity;
    ship.heavyWeapons = sc.heavyWeapons;
    ship.lightWeapons = sc.lightWeapons;
    ship.armor = sc.armor;
    ship.utility = sc.utility;
    ship.maxModules = sc.maxModules;
    shipAutofit(ship);
    // Табличные значения ставим ПОСЛЕ autofit, иначе он их перетрёт.
    ship.dryMass = sc.dryMass;
    ship.driveThrust = sc.driveThrust;
    ship.driveEfficiency = sc.driveEfficiency;
    ship.propellantVolume = sc.propellantVolume;
    ship.fuelVolume = sc.fuelVolume;
    fillTank(ship.fuel, defaultFuelElement(ship.driveIndex), shipFuelTankVolume(ship), 1.0);
    if (!driveUsesFuelAsPropellant(ship.driveIndex)) {
        fillTank(ship.propellant, defaultPropellantElement(), ship.propellantVolume, 1.0);
    }
    ship.maxHullHP = std::max(20.0, 30.0 + ship.armor * 3.0 + ship.dryMass * 0.30);
    ship.hullHP = ship.maxHullHP;
}

void shipTrimTanks(Ship& ship) {
    const double fuelCapacity = shipFuelTankVolume(ship);
    const double fuelUsed = listVolume(ship.fuel);
    if (fuelUsed > fuelCapacity && fuelUsed > 0.0) {
        const MixSummary mix = summarize(ship.fuel, -1);
        listConsumeMass(ship.fuel, mix.mass * (1.0 - fuelCapacity / fuelUsed));
    }
    const double propUsed = listVolume(ship.propellant);
    if (propUsed > ship.propellantVolume && propUsed > 0.0) {
        const MixSummary mix = summarize(ship.propellant, -1);
        listConsumeMass(ship.propellant, mix.mass * (1.0 - ship.propellantVolume / propUsed));
    }
}

void shipAutofit(Ship& ship) {
    const std::vector<DriveDef>& drives = driveDefs();
    if (ship.driveIndex < 0 || ship.driveIndex >= int(drives.size())) ship.driveIndex = defaultDriveIndex();
    ship.dryMass = std::max(12.0, 26.0 + ship.cargoCapacity * 0.32 + ship.speed * 72.0);
    ship.driveThrust = std::max(1.0, ship.dryMass * ship.acceleration * (1.05 + ship.speed * 1.4));
    ship.propellantVolume = std::max(60.0, 90.0 + ship.cargoCapacity * 1.1 + ship.speed * 110.0);
    ship.fuelVolume = std::max(6.0, ship.propellantVolume * 0.10);
    if (ship.fuel.empty()) {
        fillTank(ship.fuel, defaultFuelElement(ship.driveIndex), shipFuelTankVolume(ship), 0.84);
    }
    if (ship.propellant.empty() && !driveUsesFuelAsPropellant(ship.driveIndex)) {
        fillTank(ship.propellant, defaultPropellantElement(), ship.propellantVolume, 0.84);
    }
    ship.maxHullHP = std::max(20.0, 30.0 + ship.armor * 3.0 + ship.dryMass * 0.30);
    ship.hullHP = ship.maxHullHP;
}
