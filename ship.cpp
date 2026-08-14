#include "ship.h"
#include "drive.h"
#include "modules.h"
#include <algorithm>
#include <cmath>

// Маршрутный delta-V сжимается перед Циолковским: см. комментарий в ship.h.
const double DELTAV_SCALE = 0.004;

namespace {

// --- Релятивистская кинематика -------------------------------------------
// Скорости складывать нельзя, складывать можно БЫСТРОТУ w = artanh(v/c).
// Именно она входит в релятивистское уравнение Циолковского: m0/m1 = e^(dw/ve).
// Отсюда сама собой берётся расходимость при v -> c: чем ближе к световой,
// тем дороже каждый следующий прирост скорости.
double rapidityOf(double beta) {
    const double b = std::max(-0.999999, std::min(0.999999, beta));
    return 0.5 * std::log((1.0 + b) / (1.0 - b));
}

// Кинетическая энергия единицы массы выхлопа в долях c²: (gamma - 1).
// В нерелятивистском пределе даёт привычные 0.5*v², но не занижает энергию
// на быстрой струе.
double exhaustSpecificEnergy(double ve) {
    const double v = std::max(0.0, std::min(0.999999, ve));
    return 1.0 / std::sqrt(std::max(1e-12, 1.0 - v * v)) - 1.0;
}

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

// Топливо из активной зоны не улетает — оно перегорает. Дефект массы порядка
// процента, так что зола весит практически столько же, сколько сгорело. Один
// закон на все места, где реактор что-то жжёт: и полёт, и буровая.
void spillAsh(const std::vector<Resource>& burned, std::vector<Resource>* ashOut) {
    if (!ashOut) return;
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
        // (§20.11) Трюм у капсулы НЕ нулевой. С нулём она не могла ни возить, ни
        // добывать (бур упирался в «CARGO FULL» на первом же кадре), то есть после
        // гибели корабля игрок оставался без единого способа заработать — тупик, а
        // не наказание. 12 — меньше десятой доли стартового корпуса: подняться со
        // дна можно, но это долгая дорога, и это правильная цена за потерю борта.
        {"Escape Pod", 5.0, 1.0, 0.40, 12.0, 12.0, 1.2, 0.0, 0.0, 1.0, 0.0, 0.0, 1},

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

double shipClassMaxSpeed(const ShipClass& sc) {
    // Ступень корпуса по цене: 10^4 (стартовый) .. 10^15 (крепость).
    const double tier = std::max(0.0, std::min(1.0,
        (std::log10(std::max(1.0, sc.price)) - 4.0) / 11.0));
    // Роль внутри ступени: тяга на единицу сухой массы, 0.15 .. 1.2 по таблице.
    const double agility = std::max(0.0, std::min(1.0,
        (sc.driveThrust / std::max(1.0, sc.dryMass) - 0.15) / 1.05));
    return 0.10 + 0.42 * std::max(0.0, std::min(1.0, 0.65 * tier + 0.35 * agility));
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

double shipCruiseSpeed(const Ship& ship) {
    return std::max(0.02, ship.speed * std::max(0.2, std::min(1.0, ship.cruiseFraction)));
}

double shipCargoMass(const Ship& ship) {
    double mass = 0.0;
    for (size_t i = 0; i < ship.cargo.size(); ++i) {
        mass += ship.cargo[i].amount * resourceUnitMass(ship.cargo[i].element);
    }
    return mass;
}

// Списание массы из БУНКЕРА с учётом антивещества (§31.3).
//
// Антивещество едет в бункере вместе с топливом и входит в смесь, поэтому и
// гореть обязано в той же пропорции по массе: иначе оно давало бы прибавку к
// энергии вечно, ни на грамм не убывая. Возвращает СПИСАННЫЕ ЭЛЕМЕНТЫ — их и
// только их превращает в золу вызывающий: аннигилировавшая пара уходит в
// излучение целиком, золы от неё не остаётся.
static std::vector<Resource> consumeBunkerMass(Ship& ship, double mass) {
    if (mass <= 0.0) return std::vector<Resource>();
    const double amUnit = exoticDefs()[EX_ANTIMATTER].unitMass;
    const double amMass = ship.exotic[EX_ANTIMATTER] * amUnit;
    double elemMass = 0.0;
    for (size_t i = 0; i < ship.fuel.size(); ++i) {
        const int idx = elementIndex(ship.fuel[i].element);
        if (idx >= 0) elemMass += ship.fuel[i].amount * elementUnitMass(idx);
    }
    const double total = elemMass + amMass;
    if (total <= 1e-12) return std::vector<Resource>();
    const double share = std::min(1.0, mass / total);
    if (amMass > 0.0 && amUnit > 1e-12) {
        ship.exotic[EX_ANTIMATTER] = std::max(0.0, ship.exotic[EX_ANTIMATTER] * (1.0 - share));
    }
    return listConsumeMass(ship.fuel, elemMass * share);
}

double shipExoticMass(const Ship& ship) {
    double mass = 0.0;
    for (int k = 0; k < EX_COUNT; ++k) mass += ship.exotic[k] * exoticDefs()[size_t(k)].unitMass;
    return mass;
}

double shipInertExoticMass(const Ship& ship) {
    double inert = 0.0;
    for (int k = 0; k < EX_COUNT; ++k) {
        if (k == EX_ANTIMATTER) continue;
        inert += ship.exotic[k] * exoticDefs()[size_t(k)].unitMass;
    }
    return inert;
}

double shipExoticRoom(const Ship& ship) {
    double held = 0.0;
    for (int k = 0; k < EX_COUNT; ++k) held += ship.exotic[k];
    return std::max(0.0, ship.containment - held);
}

MixSummary shipFuelMix(const Ship& ship) {
    MixSummary mix = summarize(ship.fuel, ship.driveIndex);
    // --- Аннигиляционный катализ (§31.3) ---------------------------------
    //
    // Антивещество не «улучшает» двигатель — оно ЕДЕТ В БУНКЕРЕ ВМЕСТЕ С
    // ТОПЛИВОМ и добавляет к смеси свою энергию. Никакой новой физики: смесь
    // и так считается средневзвешенной по массе, просто у этого компонента
    // энергия не 1..8 МэВ на нуклон, а 1863 — две энергии покоя нуклона, потому
    // что пара «нуклон + антинуклон» уходит в излучение ЦЕЛИКОМ.
    //
    // Отсюда и весь баланс, без единой подгонки: доля в тысячную по массе
    // удваивает энергию смеси, потому что 0.001·1863 сравнимо с 1·8. Поджигать
    // антивещество не надо ничем — оно горит само, поэтому доля поджига у него
    // ровно 1.0 и слабый движок выигрывает от него ровно так же, как сильный.
    //
    // Расходуется оно ПРОПОРЦИОНАЛЬНО топливу (shipConsumeForDeltaV), то есть
    // кончается вместе с рейсом, а не «работает вечно».
    const double am = ship.exotic[EX_ANTIMATTER];
    if (am > 0.0 && mix.mass > 0.0) {
        const double amMass = am * exoticDefs()[EX_ANTIMATTER].unitMass;
        const double total = mix.mass + amMass;
        mix.specificEnergy = (mix.specificEnergy * mix.mass + EXOTIC_ANNIHILATION_MEV * amMass) / total;
        mix.mass = total;
    }
    return mix;
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
    // Антивещество здесь НЕ прибавляется: оно едет в бункере и уже посчитано
    // в `shipFuelMass` (см. shipFuelMix). А нейтрониум и конденсат — мёртвый
    // груз в ячейке, и вес у них настоящий: полный отсек нейтрониума реально
    // сажает разгон, потому что единица его тяжелее любого элемента таблицы.
    return std::max(1.0, ship.dryMass + shipCargoMass(ship) + shipFuelMass(ship) +
                    shipPropellantMass(ship) + shipInertExoticMass(ship));
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

namespace {

// Рабочая точка двигателя: скорость истечения зависит ТОЛЬКО от конфигурации
// корабля, цен и ручки — но НЕ от длины конкретного манёвра.
//
// Это принципиально. Полёт интегрируется по тикам, и если выбирать ve под dV
// каждого тика, оптимум уползает вниз (при малом dV экспонента вырождается в
// линейный член), режим двигателя втихую меняется посреди рейса и расход
// перестаёт сходиться с маршрутной оценкой. Точка отсчёта — номинальный манёвр
// корабля: разгон до собственного потолка скорости и торможение.
double chosenExhaustVelocity(const Ship& ship, double propellantPrice, double fuelPrice,
                             double ceiling, double energyPerMass, double payload) {
    const double nominalDeltaV = std::max(1e-6, 2.0 * rapidityOf(shipCruiseSpeed(ship))) * DELTAV_SCALE;
    const double priceP = std::max(0.0, propellantPrice);
    const double priceF = std::max(0.0, fuelPrice);
    const double veMin = std::max(1e-4, ceiling * 0.02);

    const MixSummary exhaustMix = shipExhaustMix(ship);
    const MixSummary fuelMix = shipFuelMix(ship);
    const double propVolumePerMass = exhaustMix.mass > 0.0 ? exhaustMix.volume / exhaustMix.mass : 1.0;
    const double fuelVolumePerMass = fuelMix.mass > 0.0 ? fuelMix.volume / fuelMix.mass : 1.0;

    // Стоимость номинального манёвра как функция скорости истечения:
    //   cost(v) = (e^(dV/v) - 1) * M * [ Pp + Pf*v^2 / (2*E) ]
    // Слева экспонента наказывает низкую скорость, справа энергия — высокую.
    // Минимум внутри; ищем сканированием по логарифму, отбрасывая режимы, под
    // которые негде возить вещество: дешёвый, но не влезающий режим бесполезен.
    double bestVe = ceiling;
    double bestCost = -1.0;
    double bestOverflow = -1.0;
    double bestOverflowVe = ceiling;
    // Границы РЕАЛЬНО достижимой полосы. Ручка обязана ходить по ней, а не по
    // сырому потолку: на деле сверху её ограничивает не конструкция сопла, а
    // условие k < 1 (топливо не должно съедать само себя), и снизу — объём
    // баков. Иначе у топлива победнее половина шкалы оказывается мёртвой.
    double loFeasible = -1.0;
    double hiFeasible = -1.0;
    const int samples = 48;
    for (int i = 0; i < samples; ++i) {
        const double t = double(i) / double(samples - 1);
        const double ve = veMin * std::pow(ceiling / veMin, t);
        const double ratio = std::exp(std::min(60.0, nominalDeltaV / ve));
        if (!std::isfinite(ratio)) continue;
        // Топливо входит в массу, которую само же разгоняет, — решаем это
        // замкнуто (см. shipRouteCost). k >= 1 означает, что манёвр недостижим
        // ни при каком запасе: топливо на разгон топлива съедает само себя.
        const double k = exhaustSpecificEnergy(ve) / energyPerMass * (ratio - 1.0);
        if (!(k < 0.999)) continue;
        const double fuelMass = k * payload / (1.0 - k);
        const double propMass = (payload + fuelMass) * (ratio - 1.0);
        if (!std::isfinite(propMass) || !std::isfinite(fuelMass)) continue;

        const double overflow = std::max(
            propMass * propVolumePerMass / std::max(1e-6, ship.propellantVolume),
            fuelMass * fuelVolumePerMass / std::max(1e-6, shipFuelTankVolume(ship)));
        if (bestOverflow < 0.0 || overflow < bestOverflow) {
            bestOverflow = overflow;
            bestOverflowVe = ve;
        }
        if (overflow > 1.0) continue;

        if (loFeasible < 0.0) loFeasible = ve;
        hiFeasible = ve;

        const double cost = priceP * propMass + priceF * fuelMass;
        if (bestCost < 0.0 || cost < bestCost) {
            bestCost = cost;
            bestVe = ve;
        }
    }
    if (bestCost < 0.0) {
        if (bestOverflow < 0.0) return 0.0;
        bestVe = bestOverflowVe;
    }

    // Ручка игрока выбирает точку внутри полосы [минимум .. оптимум .. потолок].
    // Интерполяция логарифмическая, потому что и стоимость, и массовое число
    // зависят от скорости истечения экспоненциально: в логарифме ручка идёт
    // равномерно, а в линейной шкале half-way ощущался бы как «почти форсаж».
    const double t = std::max(0.0, std::min(1.0, ship.throttle));
    const double lowEnd = std::min(bestVe, loFeasible > 0.0 ? loFeasible : veMin);
    const double highEnd = std::max(bestVe, hiFeasible > 0.0 ? hiFeasible : ceiling);
    return t <= 0.5
        ? lowEnd * std::pow(bestVe / std::max(1e-9, lowEnd), t * 2.0)
        : bestVe * std::pow(highEnd / std::max(1e-9, bestVe), (t - 0.5) * 2.0);
}

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

    // МЁРТВАЯ МАССА манёвра — всё, что двигатель обязан разогнать, но что НЕ
    // улетит в сопло. Это корпус, груз, экзотика И ТОПЛИВО, УЖЕ ЗАЛИТОЕ В
    // БУНКЕР: оно перегорает в золу почти той же массы и остаётся на борту.
    //
    // ⚠️ (§48) Здесь стояло `dryMass + cargo + exotic`, а топливо решалось из
    // уравнения — то есть оценка считала борт, у которого в бункере ровно
    // столько, сколько нужно на это плечо. В жизни бункер залит под пробку, и
    // замер это поймал: топлива на борту 140…166 единиц массы при рабочем теле
    // 8…32. Полный бункер — мёртвый груз, которого в расчёте не было ВООБЩЕ.
    // Отсюда расхождение оценки с фактом в 2.44 раза и сухие баки на подлёте
    // у бортов, залившихся «с запасом в полтора расчёта». Сам полёт при этом
    // считал верно (сожжено/расход по фактической массе = 0.96).
    const double fuelAboard = fuelMix.mass;
    const double hullAndCargo = ship.dryMass + shipCargoMass(ship) + shipInertExoticMass(ship);
    // Выбор скорости истечения — ценовой оптимум, а не требование; он остался на
    // прежней мерке нагрузки, чтобы эта правка меняла ОДНУ вещь за раз.
    const double payload = std::max(1.0, hullAndCargo);
    const double effectiveDeltaV = deltaV * DELTAV_SCALE;

    // Факел жжёт топливо как рабочее тело: скорость истечения не выбирается,
    // её задаёт сама реакция, и расход считается одним потоком. Мёртвая масса
    // у него другая: улетает как раз топливо, а рабочее тело (если в баке
    // что-то осталось) едет балластом.
    if (torch) {
        const double ratio = std::exp(std::min(60.0, effectiveDeltaV / ceiling));
        const double deadMass = std::max(1.0, hullAndCargo + shipPropellantMix(ship).mass);
        const double burnMass = deadMass * (ratio - 1.0);
        if (!std::isfinite(burnMass)) return result;
        result.feasible = true;
        result.exhaustVelocity = ceiling;
        result.fuelMass = burnMass;
        return result;
    }

    // Зафиксированная рабочая точка имеет приоритет: планировщик и расход в
    // полёте обязаны считать по одной и той же скорости истечения.
    const double ve = ship.cruiseExhaust > 1e-9
        ? std::min(ship.cruiseExhaust, ceiling)
        : chosenExhaustVelocity(ship, propellantPrice, fuelPrice, ceiling, energyPerMass, payload);
    if (ve <= 1e-9) return result;

    // В сопло уходит ТОЛЬКО рабочее тело; топливо перегорает в золу почти той
    // же массы и остаётся на борту. Поэтому топливо разгоняет само себя, и
    // уравнение самоссылочно. Пусть D — мёртвая масса (корпус, груз, топливо
    // УЖЕ на борту), A — это залитое топливо, dF — топливо, которое ещё надо
    // долить, W — рабочее тело:
    //
    //     W  = (D + dF) * (e^x - 1)        Циолковский: улетает только W
    //     A + dF = W * (gamma-1)/E         энергия на разгон этого W
    //  => dF = (k*D - A) / (1 - k),  k = (gamma-1)/E * (e^x - 1)
    //
    // При A = 0 это ровно прежняя формула — прежняя была её частным случаем
    // «бункер пуст», и именно поэтому недосчитывала полный бункер (§48).
    //
    // При k >= 1 решения нет ВООБЩЕ: топливо, нужное чтобы везти топливо,
    // съедает само себя. Это честный предел достижимости, а не переполнение —
    // такой манёвр не выполнить ни при каком запасе, нужен другой режим,
    // другое рабочее тело или другой движок. Величина k от запаса не зависит,
    // поэтому граница достижимости правкой не сдвинулась.
    const double ratio = std::exp(std::min(60.0, effectiveDeltaV / ve));
    const double k = exhaustSpecificEnergy(ve) / energyPerMass * (ratio - 1.0);
    if (!(k < 0.999)) return result;
    const double deadMass = std::max(1.0, hullAndCargo + fuelAboard);
    const double extraFuel = std::max(0.0, (k * deadMass - fuelAboard) / (1.0 - k));
    const double propMass = (deadMass + extraFuel) * (ratio - 1.0);
    // Сгорит топлива ровно столько, сколько энергии ушло в струю; по построению
    // это не больше, чем `A + dF`, то есть требование выполнимо тем запасом,
    // который оно же и называет.
    const double fuelMass = (deadMass + extraFuel) * k;
    if (!std::isfinite(propMass) || !std::isfinite(fuelMass)) return result;

    result.feasible = true;
    result.exhaustVelocity = ve;
    result.propellantMass = propMass;
    result.fuelMass = fuelMass;
    return result;
}

void shipTuneDrive(Ship& ship, double propellantPrice, double fuelPrice) {
    ship.cruiseExhaust = 0.0;   // сначала сбрасываем, чтобы подбор шёл начисто
    const MixSummary fuelMix = shipFuelMix(ship);
    const MixSummary exhaustMix = shipExhaustMix(ship);
    const double ceiling = driveExhaustCeiling(ship.driveIndex, exhaustMix);
    if (ceiling <= 1e-6) return;
    const double energyPerMass = fuelMix.specificEnergy / NUCLEON_REST_MEV *
                                 driveJetEfficiency(ship.driveIndex, exhaustMix);
    if (energyPerMass <= 1e-12) return;
    const double payload = std::max(1.0, ship.dryMass + shipCargoMass(ship) + shipInertExoticMass(ship));
    ship.cruiseExhaust = chosenExhaustVelocity(ship, propellantPrice, fuelPrice,
                                               ceiling, energyPerMass, payload);
}

RouteCost shipEstimateRoute(const Ship& ship, double distance, double propellantPrice, double fuelPrice) {
    if (distance <= 0.0) {
        RouteCost zero;
        zero.feasible = true;
        return zero;
    }
    const double accel = std::max(0.001, std::min(ship.acceleration, ship.driveThrust / shipTotalMass(ship)));
    const double peakSpeed = std::min(shipCruiseSpeed(ship), std::sqrt(distance * accel));
    // Разгон и торможение симметричны (ship.md, «Movement Fuel Cost», вариант 1),
    // но складываются БЫСТРОТЫ, а не скорости: 2*artanh(peak), не 2*peak.
    // Иначе на быстром корпусе с потолком 0.5c бюджет вышел бы ровно 1.0c —
    // величина, которой не существует.
    return shipRouteCost(ship, 2.0 * rapidityOf(peakSpeed), propellantPrice, fuelPrice);
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
    const MixSummary fuelMix = shipFuelMix(ship);
    const MixSummary exhaustMix = shipExhaustMix(ship);
    const double fuelAvail = fuelMix.mass;
    const double propAvail = shipPropellantMix(ship).mass;
    if (fuelAvail <= 0.0) return 0.0;
    if (!torch && propAvail <= 0.0) return 0.0;

    // Скорость истечения берём из планировщика: она зависит от ручки, цен и
    // ёмкостей, и повторять этот выбор здесь нельзя — разъедется с оценкой.
    const RouteCost plan = shipRouteCost(ship, desiredDeltaV, 1.0, 1.0);
    if (!plan.feasible || plan.exhaustVelocity <= 1e-9) return 0.0;
    const double ve = plan.exhaustVelocity;
    const double energyPerMass = fuelMix.specificEnergy / NUCLEON_REST_MEV *
                                 driveJetEfficiency(ship.driveIndex, exhaustMix);
    if (energyPerMass <= 1e-12) return 0.0;

    // ДИФФЕРЕНЦИАЛЬНАЯ форма Циолковского: выброшенная масса считается от массы
    // ТЕКУЩЕЙ, а не от «сухой после манёвра».
    //
    //     burn = m0 * (1 - exp(-dV / ve))
    //
    // Это критично, потому что полёт интегрируется по тикам. Если на каждом
    // тике брать за точку отсчёта одну и ту же конечную массу, сумма
    // sum(e^xi - 1) окажется НАМНОГО меньше e^(sum xi) - 1: экспонента выпуклая.
    // На замере такой расход занижался в шесть раз. В этой же форме тики
    // телескопируются точно в общий Циолковский, и оценка сходится с фактом.
    const double m0 = shipTotalMass(ship);
    const double burnForDeltaV = [&](double dv) {
        return m0 * (1.0 - std::exp(-std::min(60.0, dv * DELTAV_SCALE / ve)));
    }(desiredDeltaV);

    // Сколько массы вообще можно выбросить с текущими запасами.
    //
    // Энергия струи здесь ОБЯЗАНА считаться той же формулой, что и ниже, где
    // топливо реально списывается: `exhaustSpecificEnergy(ve)` = (gamma - 1).
    // Раньше тут стоял классический `ve^2/2` — на рабочих скоростях истечения
    // (до 0.32c на форсаже) он занижает энергию на 8.5%, то есть ограничитель
    // разрешал выбросить больше рабочего тела, чем оплачено топливом, и в
    // урезанном манёвре корабль получал delta-V из ниоткуда.
    const double fuelLimitedBurn = torch
        ? fuelAvail
        : fuelAvail * energyPerMass / std::max(1e-12, exhaustSpecificEnergy(ve));
    const double maxBurn = std::min(torch ? fuelAvail : propAvail, fuelLimitedBurn);
    const double burn = std::min(burnForDeltaV, std::min(maxBurn, m0 * 0.999));
    if (burn <= 1e-12) return 0.0;

    // Урезанный манёвр — обратное преобразование той же формулы, без итераций.
    const double achieved = burn >= burnForDeltaV - 1e-12
        ? desiredDeltaV
        : -std::log(std::max(1e-12, 1.0 - burn / m0)) * ve / DELTAV_SCALE;

    std::vector<Resource> burned;
    if (torch) {
        // У факела топливо и есть выхлоп: один поток, золы не остаётся.
        consumeBunkerMass(ship, burn);
    } else {
        listConsumeMass(ship.propellant, burn);
        const double fuelBurn = std::min(fuelAvail, burn * exhaustSpecificEnergy(ve) / energyPerMass);
        burned = consumeBunkerMass(ship, fuelBurn);
    }

    spillAsh(burned, ashOut);
    return achieved;
}

double shipReactorPower(const Ship& ship) {
    if (shipFuelMix(ship).mass <= 0.0) return 0.0;   // гореть нечему — установка стоит
    // Паспортная мощность установки, а не пересчёт джоулей: РАЗМЕР берём от тяги
    // (корпус несёт реактор под свой двигатель), КАЧЕСТВО — от КПД поставленного
    // движка. Обе величины уже есть в таблицах, ни одна не зависит от содержимого
    // бака — иначе бур глох бы от пустого бака рабочего тела, хотя реактору оно
    // не нужно.
    return std::max(0.0, ship.driveThrust) * driveReactorEfficiency(ship.driveIndex);
}

double shipDrillPower(const Ship& ship) {
    // Реактор задаёт потолок, буровая — какую его долю удаётся пустить в породу.
    // Без установки бур импровизированный (множитель 1), с ней — во столько раз
    // мощнее, во сколько её оценили модули.
    return shipReactorPower(ship) * (1.0 + std::max(0.0, ship.miningRig));
}

double shipDrawReactorEnergy(Ship& ship, double energy, std::vector<Resource>* ashOut) {
    if (energy <= 0.0) return 0.0;
    // ⚠️ Здесь берётся ЧИСТАЯ топливная смесь, БЕЗ антивещества.
    //
    // Антивещество впрыскивается в камеру тяги, а не в активную зону реактора:
    // катализировать им нагрев бура физически нечего. Формально смесь одна, и
    // сначала так и было написано — но тогда сеанс добычи в поясе или одно
    // нажатие `V` (просчёт Тимертии, §28) молча уничтожали антивещество по
    // 240 000 Cr за единицу, и нигде в интерфейсе этот расход не показывался.
    const MixSummary fuelMix = summarize(ship.fuel, ship.driveIndex);
    if (fuelMix.mass <= 0.0) return 0.0;
    // Энергия единицы массы топлива — та же величина, что и в полёте (§12), но БЕЗ
    // КПД струи: реактор греет буровую, а не разгоняет рабочее тело.
    const double energyPerMass = fuelMix.specificEnergy / NUCLEON_REST_MEV;
    if (energyPerMass <= 1e-12) return 0.0;   // в бункере один балласт — гореть нечему
    const double want = energy / energyPerMass;
    const double burn = std::min(want, fuelMix.mass);
    if (burn <= 1e-12) return 0.0;
    spillAsh(listConsumeMass(ship.fuel, burn), ashOut);
    return burn * energyPerMass;
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
    const double moved = loadInto(ship, ship.fuel, shipFuelTankVolume(ship), elementIdx, units);
    if (moved > 0.0) ship.cruiseExhaust = 0.0;   // состав сменился — подобрать заново
    return moved;
}

double shipLoadPropellant(Ship& ship, int elementIdx, double units) {
    if (driveUsesFuelAsPropellant(ship.driveIndex)) return 0.0;
    const double moved = loadInto(ship, ship.propellant, ship.propellantVolume, elementIdx, units);
    if (moved > 0.0) ship.cruiseExhaust = 0.0;
    return moved;
}

double shipDrainFuel(Ship& ship, int elementIdx, double units) {
    const double moved = drainInto(ship, ship.fuel, elementIdx, units);
    if (moved > 0.0) ship.cruiseExhaust = 0.0;
    return moved;
}

double shipDrainPropellant(Ship& ship, int elementIdx, double units) {
    const double moved = drainInto(ship, ship.propellant, elementIdx, units);
    if (moved > 0.0) ship.cruiseExhaust = 0.0;
    return moved;
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
    // Скорость — часть лестницы корпусов. Раньше её тут не было вовсе, и
    // покупка нового корабля не давала ни единицы скорости.
    ship.speed = shipClassMaxSpeed(sc);
    ship.cargoCapacity = sc.cargoCapacity;
    ship.heavyWeapons = sc.heavyWeapons;
    ship.lightWeapons = sc.lightWeapons;
    ship.armor = sc.armor;
    ship.utility = sc.utility;
    // (§20.11) Буровая — такой же ЗАПЕЧЁННЫЙ бонус, как трюм или броня, и такой же
    // смертный: она стоит на КОРПУСЕ, а корпус здесь заменяется. Без обнуления
    // повторялась бы грабля §15.1B — установка переживала бы и гибель борта
    // (`downgradeAgentToEscapePod` чистит modules, но поля перезаписывает именно
    // этот вызов), и пересадку на другой корпус. `buyShip` запекает модули заново
    // после этого вызова, поэтому законная навеска не теряется.
    ship.miningRig = 0.0;
    ship.maxModules = sc.maxModules;
    shipAutofit(ship);
    // Табличные значения ставим ПОСЛЕ autofit, иначе он их перетрёт.
    ship.dryMass = sc.dryMass;
    ship.driveThrust = sc.driveThrust;
    ship.driveEfficiency = sc.driveEfficiency;
    ship.propellantVolume = sc.propellantVolume;
    ship.fuelVolume = sc.fuelVolume;
    // (§48) БУНКЕР ЗАЛИВАЕТСЯ РАБОЧИМ ЗАПАСОМ, А НЕ ПОД ПРОБКУ.
    //
    // ⚠️ Топливо плотное, рабочее тело — нет: у Hauler полный бункер весит 220
    // при сухой массе корпуса 60, вчетверо больше самого корабля. Пока оценка
    // маршрута не видела мёртвой массы (§48), это было бесплатно; теперь оно
    // честно оплачивается рабочим телом, и корабль, вышедший с верфи «полным»,
    // возит собственный штраф. Замер на плече 7 ly: полный бункер требует 8.15
    // рабочего тела, четверть бункера — 2.9, то есть дорога дешевеет втрое.
    // Никто в здравом уме не идёт в короткий рейс с полным баком урана.
    fillTank(ship.fuel, defaultFuelElement(ship.driveIndex), shipFuelTankVolume(ship) * FUEL_START_SHARE, 1.0);
    if (!driveUsesFuelAsPropellant(ship.driveIndex)) {
        fillTank(ship.propellant, defaultPropellantElement(), ship.propellantVolume, 1.0);
    }
    ship.maxHullHP = std::max(20.0, 30.0 + ship.armor * 3.0 + ship.dryMass * 0.30);
    ship.hullHP = ship.maxHullHP;
    // Рабочая точка двигателя подбирается СРАЗУ. Иначе каждый расчёт маршрута
    // для такого корабля запускает 48-сэмпловый подбор заново, а ИИ считает
    // маршруты тысячами за такт — на замере это давало 1842 подбора в кадр.
    shipTuneDrive(ship, 1.0, 1.0);
}

void shipApplyChromocoreFactors(Ship& ship, double materials, double tactics, double kinematics) {
    if (materials > 1.0 && std::isfinite(materials)) {
        ship.cargoCapacity *= materials;
        ship.maxHullHP *= materials;
        ship.hullHP = std::min(ship.hullHP * materials, ship.maxHullHP);
    }
    if (tactics > 1.0 && std::isfinite(tactics)) {
        ship.heavyWeapons *= tactics;
        ship.lightWeapons *= tactics;
    }
    if (kinematics > 1.0 && std::isfinite(kinematics)) {
        // Тот же потолок 0.5c, что и был при выдаче ядра: кинематика ускоряет
        // корабль, но не отменяет световой предел лестницы корпусов.
        ship.speed = std::min(0.5, ship.speed * kinematics);
        ship.acceleration *= std::sqrt(kinematics);
    }
}

void shipEmergencyPrime(Ship& ship, double share) {
    if (share <= 0.0) return;
    if (share > 1.0) share = 1.0;
    const double fuelWant = shipFuelTankVolume(ship) * share;
    if (listVolume(ship.fuel) < fuelWant) {
        fillTank(ship.fuel, defaultFuelElement(ship.driveIndex), fuelWant, 1.0);
    }
    if (!driveUsesFuelAsPropellant(ship.driveIndex)) {
        const double propWant = ship.propellantVolume * share;
        if (listVolume(ship.propellant) < propWant) {
            fillTank(ship.propellant, defaultPropellantElement(), propWant, 1.0);
        }
    }
    shipTuneDrive(ship, 1.0, 1.0);
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
    // Рабочая точка двигателя подбирается СРАЗУ, при создании корпуса. Иначе
    // каждый расчёт маршрута для такого корабля запускает 48-сэмпловый подбор
    // заново, а ИИ считает маршруты тысячами за такт: на замере это давало
    // 1842 подбора в кадр и вдвое утяжеляло симуляцию.
    shipTuneDrive(ship, 1.0, 1.0);
}
