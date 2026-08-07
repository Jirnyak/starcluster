#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "resource.h"

// Forward declarations
class ClusterStar;

// Население ТИПИЧНОЙ обитаемой системы. Не балансная крутилка, а физическая
// величина: до §17 генератор давал 500..25 500 человек — уездный городок на
// целую звёздную систему, — и вся экономика наследовала этот масштаб. Глубина
// рынка выходила в десятки кредитов, поэтому полный трюм рудовоза обваливал
// цену вдвое, а лестница прогрессии (корпуса до 1e15, система 1e9) висела в
// воздухе на пять порядков выше.
//
// Стратегические оценки ИИ («насколько эта система привлекательна») сравнивают
// системы ДРУГ С ДРУГОМ, поэтому читают население не в людях, а в долях
// типичного — через starPopulationWeight ниже. Одна нормировка вместо десятка
// пересчитанных магических коэффициентов.
const double POPULATION_TYPICAL = 1.0e8;

// Население в долях типичной системы: 1.0 — типичная, 10 — метрополия.
double starPopulationWeight(const ClusterStar& star);

// Имя системы: произносимая основа плюс номер звезды — «Varen-417».
//
// Номер оставлен намеренно: по нему систему ищут в списках и называют вслух,
// когда основа вылетела из головы. Основа собирается из открытых слогов, а не
// из букв, поэтому читается одинаково на обоих языках и запоминается.
//
// Генератор НЕ трогает основной поток случайных чисел скопления: имя — чистая
// функция от (seed, index), и порядок обращений к rng в generate() не сдвинут.
std::string starNameFor(unsigned int seed, size_t index);

// Основа имени без номера: «Varen-417» -> «Varen». Имя из старого сейва
// (`Star_417`, номера через `_`) возвращается целиком — резать нечего.
std::string starNameStem(const std::string& full);

// Та же основа кириллицей. Таблица фонем парная (латиница + кириллица), поэтому
// это не «догадка по буквам», а чтение той же таблицы жадно слева направо.
std::string starNameCyrillic(const std::string& latin);

// Класс звёздного кластера
class Cluster {
public:
    std::vector<ClusterStar> stars;
    // Геометрия скопления, посчитанная при генерации. Нужна там, где важно «за
    // сколько лет свет обойдёт ВСЁ скопление» (банковская сверка счёта), —
    // считать max по 10 000 звёзд на каждую транзакцию нельзя.
    double centreX = 0.0;
    double centreY = 0.0;
    double centreZ = 0.0;
    double radiusLy = 0.0;   // расстояние от центра до самой дальней звезды
    // Генерация кластера
    void generate(size_t num_stars, unsigned int seed);
    // Отдаёт основы имён в словарь локализации, чтобы «Varen-417» стало
    // «Варен-417» само собой, где бы имя ни печаталось. Зовётся из generate() и
    // ПОВТОРНО после загрузки сейва: там имена приходят из файла, а не из seed.
    void registerNames() const;
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
