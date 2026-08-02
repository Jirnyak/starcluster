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
    double miningRichness = 1.0;     // насыщенность руды (для ручной добычи)
    double metallicity = 0.5;        // металличность (тип руды)
    int stellarClass = 0;            // 0 обычная звезда, 1 нейтронная/пульсар
    char spectralType = 'G';         // O, B, A, F, G, K, M, X (Neutron)
    double temperature = 5778.0;     // K
    double mass = 1.0;               // Solar masses
    double radius = 1.0;             // Solar radii
    double luminosity = 1.0;         // Solar luminosities
    uint8_t colorR = 255;
    uint8_t colorG = 255;
    uint8_t colorB = 255;
    std::vector<Resource> resources; // Ресурсы в системе
    std::vector<double> demandBias; // Процедурный спрос системы по элементам
    std::vector<int> resourceFocus; // Элементы, которыми система богата
    std::vector<int> demandFocus;   // Элементы, которые система особенно потребляет
    ClusterStar(double x_, double y_, double z_, const std::string& name_);
};
