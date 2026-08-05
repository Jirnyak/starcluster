#pragma once
#include <string>
#include <vector>
#include "resource.h"
#include "drive.h"

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

    // --- Двигательная установка (см. drive.h) ---
    int driveIndex = 0;             // семейство и параметры движка
    // Бункер и бак — такие же СМЕСИ, как трюм. Никаких «одна ёмкость — один
    // элемент»: залить можно что угодно и сколько угодно, свойства считаются
    // средневзвешенными по массе. Выбор топлива стал градиентом качества,
    // а не набором запретов.
    //
    // Топливо: источник энергии. Остаётся в активной зоне и ПЕРЕГОРАЕТ,
    // поэтому зола падает обратно в трюм.
    std::vector<Resource> fuel;
    double fuelVolume = 24.0;       // ёмкость бункера, в единицах ОБЪЁМА
    // Рабочее тело: реактивная масса. Улетает в сопло безвозвратно.
    std::vector<Resource> propellant;
    double propellantVolume = 240.0; // ёмкость бака, в единицах ОБЪЁМА
    // Режим двигателя, 0..1. Ручка выбирает скорость истечения внутри
    // физически допустимой полосы:
    //   0.0 — макс эконом: минимальная скорость истечения, много рабочего
    //         тела и мало топлива; корабль тяжелее и медленнее;
    //   0.5 — ценовой оптимум по ЛОКАЛЬНЫМ ценам (значение по умолчанию);
    //   1.0 — полный форсаж: скорость истечения у потолка, рабочего тела
    //         минимум, топлива максимум; корабль легче и разгоняется быстрее.
    double throttle = 0.5;
    // Рабочая точка двигателя — фактическая скорость истечения, ВЫБРАННАЯ и
    // ЗАФИКСИРОВАННАЯ (см. shipTuneDrive). Это настройка, а не результат
    // вычисления на каждом вызове: иначе планировщик и реальный расход в полёте
    // считали бы по разным ценам и разъезжались. 0 — «подобрать автоматически».
    double cruiseExhaust = 0.0;
    // Доля от потолка скорости, на которой корабль идёт крейсером, 0.2..1.
    // Лететь медленнее объективно дешевле: бюджет быстроты равен 2*artanh(peak),
    // поэтому снижение крейсера прямо режет расход обоих расходников. Раньше
    // корабль всегда шёл на полном потолке, и этот размен игроку был недоступен.
    double cruiseFraction = 1.0;

    std::vector<Resource> cargo;
    double cargoCapacity = 100.0; // Максимальный груз (масса)
    double heavyWeapons = 10.0;
    double lightWeapons = 5.0;
    double armor = 5.0;
    double utility = 0.0;
    double hullHP = 60.0;       // текущая целостность корпуса
    double maxHullHP = 60.0;    // максимум целостности (корпус + модули)
    std::vector<int> modules;   // установленные модули (индексы в moduleDefs)
    int maxModules = 3;         // слоты апгрейдов
    int ownerFaction; // -1 если свободный
    int targetStar = -1; // Индекс звезды назначения
    bool enRoute = false; // В пути ли корабль
    Ship(const std::string& name_, double x_, double y_, double z_, double speed_, int ownerFaction_);
};

// Насколько «сжимается» маршрутный delta-V перед подстановкой в Циолковского.
// Формула остаётся настоящей (экспонента, оптимум, недостижимые маршруты), но
// без этого множителя межзвёздные delta-V дают массовые числа в сотни.
extern const double DELTAV_SCALE;

double resourceUnitMassByIndex(int elementIndex);
double resourceUnitMass(const std::string& element);
// Фактическая крейсерская скорость с учётом cruiseFraction.
double shipCruiseSpeed(const Ship& ship);
double shipCargoMass(const Ship& ship);
double shipFuelMass(const Ship& ship);
double shipPropellantMass(const Ship& ship);
double shipTotalMass(const Ship& ship);

// Объём, реально доступный под топливо. У факела топливо и есть рабочее тело,
// поэтому большой бак подключается к реактору и складывается с бункером.
double shipFuelTankVolume(const Ship& ship);
double shipFuelFill(const Ship& ship);        // 0..1 по объёму бункера
double shipPropellantFill(const Ship& ship);  // 0..1 по объёму бака
double shipCurrentAcceleration(const Ship& ship);

// Средневзвешенные свойства смесей. У топлива энергия уже умножена на долю
// поджига текущего движка, поэтому «мёртвый» балласт виден сразу.
MixSummary shipFuelMix(const Ship& ship);
MixSummary shipPropellantMix(const Ship& ship);
// Что реально летит из сопла: у факела это топливо, у остальных — рабочее тело.
MixSummary shipExhaustMix(const Ship& ship);

// Настраивает рабочую точку двигателя под текущие состав баков, ручку и цены.
// Вызывается там, где что-то из этого меняется: заправка, перелив, вылет.
void shipTuneDrive(Ship& ship, double propellantPrice, double fuelPrice);

// Потолок скорости истечения на текущей паре движок/рабочее тело.
double shipMaxExhaustVelocity(const Ship& ship);

// Во что обходится манёвр на deltaV. Скорость истечения выбирается так, чтобы
// минимизировать суммарную стоимость по ЛОКАЛЬНЫМ ценам, затем сдвигается
// ручкой throttle. Цены в кредитах за единицу МАССЫ; если они неизвестны,
// передавайте 1.0 — тогда оптимум сводится к минимуму суммарной массы.
struct RouteCost {
    bool feasible = false;
    double exhaustVelocity = 0.0;
    double propellantMass = 0.0;
    double fuelMass = 0.0;
};
// deltaV здесь — бюджет БЫСТРОТЫ (rapidity), а не скорости: складывать можно
// только её. На нерелятивистских скоростях они совпадают.
RouteCost shipRouteCost(const Ship& ship, double deltaV, double propellantPrice, double fuelPrice);

// Оценка расхода на перелёт заданной длины (разгон + торможение).
RouteCost shipEstimateRoute(const Ship& ship, double distance, double propellantPrice, double fuelPrice);
// Хватает ли залитого на такой перелёт.
bool shipCanFlyDistance(const Ship& ship, double distance);

// Тратит рабочее тело и топливо на манёвр, возвращает реально выданный deltaV.
// Расход снимается со смеси ПРОПОРЦИОНАЛЬНО долям компонентов. Перегоревшее
// топливо превращается в золу и дописывается в ashOut (если он задан): лёгкие
// ядра сливаются вверх к железу, тяжёлые делятся вниз к нему же.
double shipConsumeForDeltaV(Ship& ship, double desiredDeltaV, std::vector<Resource>* ashOut);

// Элемент, в который перегорает данное топливо. -1, если золы не остаётся.
int elementAshProduct(int fuelElement);

// Перегруз трюма сверх паспортной нормы. Загрузить можно всегда — взлететь
// с перегрузом нельзя. Возвращает избыток массы, 0 если нормы хватает.
double shipCargoOverload(const Ship& ship);

// Преобладающий по массе компонент смеси; если ёмкость пуста — разумный
// дефолт для этой роли. Нужен для автозаправки и подписей в UI.
int shipDominantFuelElement(const Ship& ship);
int shipDominantPropellantElement(const Ship& ship);

// Перелив трюм <-> ёмкости. Возвращают реально перелитое количество единиц.
// Запретов по элементам нет: ограничивают только объём ёмкости и наличие.
double shipLoadFuel(Ship& ship, int elementIdx, double units);
double shipLoadPropellant(Ship& ship, int elementIdx, double units);
double shipDrainFuel(Ship& ship, int elementIdx, double units);
double shipDrainPropellant(Ship& ship, int elementIdx, double units);

// Сливает за борт то, что перестало влезать после смены баков или движка.
void shipTrimTanks(Ship& ship);

void shipAutofit(Ship& ship);

struct ShipClass;
// Применяет табличный класс к корпусу и заливает баки под пробку. Раньше это
// было размазано по трём местам в game.cpp, причём shipAutofit успевал затереть
// табличные ёмкости производными от трюма — теперь порядок один и правильный.
void shipApplyClass(Ship& ship, const ShipClass& sc);

struct ShipClass {
    std::string name;
    double dryMass;
    double driveThrust;
    double driveEfficiency;
    double cargoCapacity;
    double propellantVolume;
    double fuelVolume;
    double heavyWeapons;
    double lightWeapons;
    double armor;
    double utility;
    double price;
    int maxModules;
};

const std::vector<ShipClass>& shipClasses();

// Потолок скорости корпуса, доли c. НЕ забит в таблицу руками, а выведен из
// двух её же полей, поэтому лестница гладкая по построению и не может
// разъехаться при правке класса:
//   ступень цены  — общий уровень корпуса (стартовые ~0.10c, топовые ~0.50c);
//   тяговооружённость — роль внутри ступени (перехватчик быстрее рудовоза).
double shipClassMaxSpeed(const ShipClass& sc);
