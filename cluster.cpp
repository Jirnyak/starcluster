
#include "cluster.h"
#include <random>
#include <cmath>
#include <algorithm>

// Реализация конструктора ClusterStar
ClusterStar::ClusterStar(double x_, double y_, double z_, const std::string& name_)
    : x(x_), y(y_), z(z_), name(name_) {}

// Генерация звёзд для симуляции
void Cluster::generate(size_t num_stars) {
    stars.clear();
    stars.reserve(num_stars);
    std::mt19937 rng(42);
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
        star.population = 500.0 + unit(rng) * 25000.0;
        star.industry = 0.4 + unit(rng) * 2.2;
        star.habitability = 0.18 + unit(rng) * 0.74;
        if (star.economyRole == "habitat") star.habitability += 0.18;
        if (star.economyRole == "frontier") star.habitability -= 0.08;
        star.habitability = std::max(0.05, std::min(1.0, star.habitability));
        star.defense = 0.8 + star.industry * 1.4 + star.population * 0.00008;
        if (star.economyRole == "military") star.defense += 2.6;
        if (star.economyRole == "shipyard") star.defense += 1.1;

        const double richness = 0.45 + unit(rng) * 1.5;
        const double metallicity = 0.22 + unit(rng) * 0.95;
        const double rarePocket = unit(rng) < 0.08 ? 3.5 + unit(rng) * 5.0 : 1.0;
        const double volatilePocket = unit(rng) < 0.18 ? 1.8 + unit(rng) * 2.5 : 1.0;
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
}
