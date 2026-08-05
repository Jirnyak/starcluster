#pragma once
#include <string>
#include <vector>

// Модель двигателя (см. ship.md, «Fuel And Propellant»).
//
// Ядерный двигатель — это ИСТОЧНИК ЭНЕРГИИ плюс СПОСОБ превратить её в тягу.
// Источник — топливо: синтез лёгких ядер или деление тяжёлых, обе ветви меряются
// одной величиной specificEnergy (расстояние до пика кривой связи).
// Способ — семейство двигателя, и их три:
//
//   Thermal  реактор ГРЕЕТ рабочее тело, наружу летит горячий газ. Химического
//            горения нет вообще: водород не сжигают, его нагревают. Скорость
//            истечения ~ sqrt(T/A), поэтому нужно ЛЁГКОЕ рабочее тело.
//            Высокая тяга. Баки огромные: водород рыхлый.
//   Ion      реактор даёт ЭЛЕКТРИЧЕСТВО, рабочее тело ионизуется и разгоняется
//            полем. Скорость истечения задаётся напряжением, а не массой, зато
//            тяга мизерная, а за ионизацию надо платить энергией.
//            Баки компактные: выгодно тяжёлое и плотное.
//   Torch    термоядерный факел: продукты синтеза САМИ и есть выхлоп.
//            Топливо = рабочее тело, второй бак не нужен.
enum class DriveFamily {
    Thermal = 0,
    Ion = 1,
    Torch = 2
};

// Свойства СМЕСИ в баке или бункере. Бак — такой же список элементов, как трюм,
// поэтому всё, что нужно физике, — это средневзвешенные по массе величины:
// смесь газов ведёт себя как один газ со средней молярной массой.
struct MixSummary {
    double mass = 0.0;
    double volume = 0.0;
    double meanAtomicMass = 1.0;
    double meanIonizationEase = 0.0;
    double meanHandlingRisk = 0.0;
    // МэВ/нуклон, УЖЕ умноженные на долю поджига: то, что движок не в состоянии
    // зажечь, едет балластом и в энергию не даёт ничего.
    double specificEnergy = 0.0;
};

struct DriveDef {
    std::string name;
    DriveFamily family;
    double price;
    double mass;            // прибавка к сухой массе
    double chamberEnergy;   // термо: тепловой потолок камеры; ион: ускоряющее напряжение
    double efficiency;      // 0..1, доля энергии, попавшая в струю
    double thrustCoeff;     // множитель тяги; у ионника мизерный
    // Порог поджига по max(fusionFuelTrait, fissionFuelTrait). specificEnergy
    // говорит, сколько энергии в ядре ЕСТЬ, а траиты — можно ли до неё
    // добраться: у свинца энергия связи ниже железа, но кулоновский барьер и
    // окно делимости не пускают. Порог НЕ запрет, а середина плавной сходимости
    // (см. driveIgnitionFraction): плохое топливо возить можно, толку мало.
    double ignitionThreshold;
    int minShipyard;
    std::string blurb;
};

const std::vector<DriveDef>& driveDefs();
const char* driveFamilyLabel(DriveFamily family);
DriveFamily driveFamilyOf(int driveIndex);

// Индекс дефолтного движка, который стоит на корабле бесплатно и слот не занимает.
int defaultDriveIndex();

// Какую долю ядерной энергии элемента движок в состоянии высвободить, 0..1.
// Непрерывная функция: никаких «этим кормить нельзя», только «этим невыгодно».
double driveIgnitionFraction(int driveIndex, int elementIndex);

// Потолок скорости истечения на данной смеси рабочего тела.
double driveExhaustCeiling(int driveIndex, const MixSummary& propellantMix);
// Доля энергии, дошедшая до струи, с учётом цены ионизации и опасности смеси.
double driveJetEfficiency(int driveIndex, const MixSummary& propellantMix);

// Факел жжёт топливо как рабочее тело — второй бак ему не нужен.
bool driveUsesFuelAsPropellant(int driveIndex);
