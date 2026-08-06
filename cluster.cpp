
#include "cluster.h"
#include <random>
#include <cmath>
#include <algorithm>

// Реализация конструктора ClusterStar
ClusterStar::ClusterStar(double x_, double y_, double z_, const std::string& name_)
    : x(x_), y(y_), z(z_), name(name_) {}

double starPopulationWeight(const ClusterStar& star) {
    return std::max(0.0, star.population) / POPULATION_TYPICAL;
}

// Генерация звёзд для симуляции
void Cluster::generate(size_t num_stars, unsigned int seed) {
    stars.clear();
    stars.reserve(num_stars);
    // Раньше здесь стояла константа 42, поэтому карта звёзд была ОДНА И ТА ЖЕ во
    // всех партиях, каким бы ни был seed мира. Теперь скопление тоже зависит от seed.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double pi = 3.14159265358979323846;
    const double clusterRadius = 100.0;
    const double coreRadius = 18.0;
    const char* roles[] = {"habitat", "refinery", "shipyard", "research", "military", "frontier"};
    const auto& elements = elementDefinitions();

    for (size_t i = 0; i < num_stars; ++i) {
        const double u = std::max(1e-6, unit(rng));
        double r = coreRadius / std::sqrt(std::pow(u, -2.0 / 3.0) - 1.0);
        if (r > clusterRadius) {
            r = clusterRadius * std::pow(unit(rng), 1.0 / 3.0);
        }
        const double cosTheta = unit(rng) * 2.0 - 1.0;
        const double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        const double phi = unit(rng) * 2.0 * pi;
        const double x = r * sinTheta * std::cos(phi);
        const double y = r * sinTheta * std::sin(phi);
        const double z = r * cosTheta;

        std::string name = "Star_" + std::to_string(i);
        stars.emplace_back(x, y, z, name);

        ClusterStar& star = stars.back();
        star.economyRole = roles[i % (sizeof(roles) / sizeof(roles[0]))];
        // Тираж населения берётся ЗДЕСЬ (порядок обращений к rng не меняется),
        // а само население считается ниже — когда известна обитаемость.
        const double populationRoll = unit(rng);
        star.industry = 0.4 + unit(rng) * 2.2;
        star.habitability = 0.18 + unit(rng) * 0.74;
        if (star.economyRole == "habitat") star.habitability += 0.18;
        if (star.economyRole == "frontier") star.habitability -= 0.08;
        star.habitability = std::max(0.05, std::min(1.0, star.habitability));
        // Население — ЛОГАРИФМИЧЕСКИ равномерное по четырём порядкам и тем
        // сильнее, чем пригоднее система для жизни. Равномерное распределение
        // (было `500 + u*25000`) давало одинаково унылые посёлки везде; на
        // логарифме появляется то, ради чего в скопление и летят: редкие
        // метрополии на миллиарды рядом с фронтиром на сотни тысяч.
        star.population = std::pow(10.0, 6.0 + 3.5 * populationRoll) * (0.25 + star.habitability);
        // Оборона отсчитывается от ТИПИЧНОЙ системы, а не от абсолютного числа
        // людей: иначе метрополия получала бы четырёхзначную оборону.
        star.defense = 0.8 + star.industry * 1.4 + starPopulationWeight(star) * 2.0;
        if (star.economyRole == "military") star.defense += 2.6;
        if (star.economyRole == "shipyard") star.defense += 1.1;

        const double richness = 0.45 + unit(rng) * 1.5;
        const double metallicity = 0.22 + unit(rng) * 0.95;
        const double rarePocket = unit(rng) < 0.08 ? 3.5 + unit(rng) * 5.0 : 1.0;
        const double volatilePocket = unit(rng) < 0.18 ? 1.8 + unit(rng) * 2.5 : 1.0;
        star.miningRichness = richness;
        star.metallicity = metallicity;
        double sc_r = unit(rng);
        if (sc_r < 0.008) star.stellarClass = 1;      // Neutron star
        else if (sc_r < 0.010) star.stellarClass = 2; // Black hole (~20 per 10000)
        else if (sc_r < 0.015) star.stellarClass = 3; // White dwarf (~50)
        else if (sc_r < 0.025) star.stellarClass = 4; // Red giant (~100)
        else star.stellarClass = 0;                   // Main sequence

        if (star.stellarClass == 1) {
            star.spectralType = 'X';
            star.temperature = 1000000.0;
            star.mass = 1.4 + unit(rng) * 0.7;
            star.radius = 0.000015;
            star.colorR = 100; star.colorG = 200; star.colorB = 255;
        } else if (star.stellarClass == 2) {
            star.spectralType = 'H';
            star.temperature = 0.0;
            star.mass = 3.0 + unit(rng) * 12.0;
            star.radius = star.mass * 0.00000428; 
            star.colorR = 30; star.colorG = 15; star.colorB = 50; // Dark purple/black
        } else if (star.stellarClass == 3) {
            star.spectralType = 'D';
            star.temperature = 10000.0 + unit(rng) * 40000.0;
            star.mass = 0.5 + unit(rng) * 0.9;
            star.radius = 0.008 + unit(rng) * 0.01;
            star.colorR = 255; star.colorG = 255; star.colorB = 255;
        } else if (star.stellarClass == 4) {
            star.spectralType = 'M';
            star.temperature = 3000.0 + unit(rng) * 1500.0;
            star.mass = 0.8 + unit(rng) * 1.5;
            star.radius = 20.0 + unit(rng) * 80.0;
            star.colorR = 255; star.colorG = 100; star.colorB = 60;
        } else {
            double r = unit(rng);
            if (r < 0.00003) {
                star.spectralType = 'O';
                star.temperature = 30000.0 + unit(rng) * 20000.0;
                star.mass = 16.0 + unit(rng) * 34.0;
                star.radius = 6.6 + unit(rng) * 10.0;
                star.colorR = 155; star.colorG = 176; star.colorB = 255;
            } else if (r < 0.0013) {
                star.spectralType = 'B';
                star.temperature = 10000.0 + unit(rng) * 20000.0;
                star.mass = 2.1 + unit(rng) * 13.9;
                star.radius = 1.8 + unit(rng) * 4.8;
                star.colorR = 170; star.colorG = 191; star.colorB = 255;
            } else if (r < 0.0073) {
                star.spectralType = 'A';
                star.temperature = 7500.0 + unit(rng) * 2500.0;
                star.mass = 1.4 + unit(rng) * 0.7;
                star.radius = 1.4 + unit(rng) * 0.4;
                star.colorR = 202; star.colorG = 215; star.colorB = 255;
            } else if (r < 0.0373) {
                star.spectralType = 'F';
                star.temperature = 6000.0 + unit(rng) * 1500.0;
                star.mass = 1.04 + unit(rng) * 0.36;
                star.radius = 1.15 + unit(rng) * 0.25;
                star.colorR = 248; star.colorG = 247; star.colorB = 255;
            } else if (r < 0.1133) {
                star.spectralType = 'G';
                star.temperature = 5200.0 + unit(rng) * 800.0;
                star.mass = 0.8 + unit(rng) * 0.24;
                star.radius = 0.96 + unit(rng) * 0.19;
                star.colorR = 255; star.colorG = 244; star.colorB = 232;
            } else if (r < 0.2343) {
                star.spectralType = 'K';
                star.temperature = 3700.0 + unit(rng) * 1500.0;
                star.mass = 0.45 + unit(rng) * 0.35;
                star.radius = 0.7 + unit(rng) * 0.26;
                star.colorR = 255; star.colorG = 210; star.colorB = 161;
            } else {
                star.spectralType = 'M';
                star.temperature = 2400.0 + unit(rng) * 1300.0;
                star.mass = 0.08 + unit(rng) * 0.37;
                star.radius = 0.1 + unit(rng) * 0.6;
                star.colorR = 255; star.colorG = 204; star.colorB = 111;
            }
        }
        star.luminosity = std::pow(star.radius, 2.0) * std::pow(star.temperature / 5778.0, 4.0);

        star.resources.reserve(elements.size());
        star.demandBias.assign(elements.size(), 0.35);
        std::vector<double> supplyBias(elements.size(), 1.0);

        for (size_t e = 0; e < elements.size(); ++e) {
            supplyBias[e] = 0.18 + unit(rng) * unit(rng) * 2.4;
            star.demandBias[e] = 0.28 + unit(rng) * unit(rng) * 2.8;
        }

        const int resourceFocusCount = 2 + int(unit(rng) * 6.0);
        const int demandFocusCount = 3 + int(unit(rng) * 8.0);
        const int depletedCount = 8 + int(unit(rng) * 18.0);

        for (int n = 0; n < resourceFocusCount; ++n) {
            const int idx = int(unit(rng) * double(elements.size()));
            if (idx >= 0 && idx < int(elements.size())) {
                supplyBias[idx] *= 7.0 + unit(rng) * 34.0;
                star.resourceFocus.push_back(idx);
            }
        }
        for (int n = 0; n < demandFocusCount; ++n) {
            const int idx = int(unit(rng) * double(elements.size()));
            if (idx >= 0 && idx < int(elements.size())) {
                star.demandBias[idx] *= 4.0 + unit(rng) * 18.0;
                star.demandFocus.push_back(idx);
            }
        }
        for (int n = 0; n < depletedCount; ++n) {
            const int idx = int(unit(rng) * double(elements.size()));
            if (idx >= 0 && idx < int(elements.size())) {
                supplyBias[idx] *= 0.02 + unit(rng) * 0.22;
            }
        }

        for (size_t e = 0; e < elements.size(); ++e) {
            const ElementDefinition& element = elements[e];
            const double lowMassTrait = 1.0 / std::sqrt(std::max(1.0, element.atomicMass));
            const double volatileScore = std::min(1.0, lowMassTrait * (0.7 + element.oxidizerTrait + element.reducerTrait + element.fusionFuelTrait));
            const double industrialScore = std::min(1.0,
                element.metallicTrait * 0.45 +
                element.structuralTrait * 0.35 +
                element.conductorTrait * 0.25 +
                element.catalystTrait * 0.25);
            const double rareScore = std::min(1.0,
                element.catalystTrait * 0.35 +
                element.fissionFuelTrait * 0.55 +
                element.handlingRisk * 0.35 +
                0.08 / std::sqrt(element.abundanceWeight + 0.01));
            double amount = element.abundanceWeight * richness * (0.55 + unit(rng) * 0.95);
            amount *= 0.35 + metallicity * industrialScore + (1.0 - metallicity) * volatileScore;
            amount *= 1.0 + (volatilePocket - 1.0) * volatileScore;
            amount *= 1.0 + (rarePocket - 1.0) * rareScore;
            amount *= supplyBias[e];
            star.resources.emplace_back(element.symbol, std::max(0.001, amount));
        }
    }

    // Центр и радиус скопления — производные от уже сгенерированных координат,
    // поэтому в сейв не идут: восстанавливаются вместе с миром из seed.
    centreX = centreY = centreZ = 0.0;
    radiusLy = 0.0;
    if (stars.empty()) return;
    for (const ClusterStar& s : stars) { centreX += s.x; centreY += s.y; centreZ += s.z; }
    centreX /= double(stars.size());
    centreY /= double(stars.size());
    centreZ /= double(stars.size());
    for (const ClusterStar& s : stars) {
        const double dx = s.x - centreX, dy = s.y - centreY, dz = s.z - centreZ;
        radiusLy = std::max(radiusLy, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
}
