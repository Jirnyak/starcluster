#pragma once
#include <vector>
#include <string>
#include "resource.h"

// Локальный рынок ресурсов
class Market {
public:
    std::vector<Resource> supply; // Предложение
    std::vector<Resource> demand; // Спрос
    std::vector<double> prices;   // Цены по индексам ресурсов
    std::vector<double> productionRate;
    std::vector<double> demandRate;
    std::string role;
    Market();
    void seed(const std::vector<Resource>& localResources, const std::vector<double>& demandBias, const std::string& role_, double population, double industry);
    void update(double dt);
    void updatePrices(); // Пересчёт цен
    double pricePressure() const;
};
