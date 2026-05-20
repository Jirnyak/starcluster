#pragma once
#include <vector>
#include <string>
#include "resource.h"

// Forward declarations
class ClusterStar;

// Класс звёздного кластера
class Cluster {
public:
    std::vector<ClusterStar> stars;
    // Генерация кластера
    void generate(size_t num_stars);
};

// Класс звезды (для кластера)
class ClusterStar {
public:
    double x, y, z; // Координаты в св. годах
    std::string name;
    std::string economyRole;
    double population = 0.0;
    double industry = 0.0;
    double habitability = 0.0;
    double defense = 0.0;
    int ownerFaction = -1;
    int occupyingFaction = -1;
    double captureProgress = 0.0;
    double capturePressure = 0.0;
    double contestedAt = -1.0;
    std::vector<Resource> resources; // Ресурсы в системе
    std::vector<double> demandBias; // Процедурный спрос системы по элементам
    std::vector<int> resourceFocus; // Элементы, которыми система богата
    std::vector<int> demandFocus;   // Элементы, которые система особенно потребляет
    ClusterStar(double x_, double y_, double z_, const std::string& name_);
};
