#pragma once
#include <vector>
#include <string>
#include "resource.h"

// Локальный рынок ресурсов.
//
// Цена НЕ приписана элементу: она рождается из конкуренции кандидатов за
// функции-нужды системы (см. econ.h). Запас тает от потребления, растёт от
// добычи; цена тянется туда, где спрос сходится с предложением. Раскупили
// лидера — доля спроса перетекает на суррогат, и волна идёт по семейству.
class Market {
public:
    std::vector<Resource> supply; // Запас на рынке (масса)
    std::vector<Resource> demand; // Годовой спрос (для совместимости — масса)
    std::vector<double> prices;   // Цены по индексам элементов
    std::vector<double> productionRate; // Местная добыча/производство, масса в год
    std::vector<double> demandRate;     // Потребление из модели замещения, масса в год
    std::vector<double> eventMul; // временные ценовые множители (рыночные события)
    std::vector<double> demandNoise; // лёгкая рябь предпочтений
    std::vector<double> needs;    // EF_COUNT: услуг в год, которых требует система
    double serviceCostAvg = 0.0;  // средняя цена единицы услуги на этом рынке
    std::vector<double> rationing;// EF_COUNT: доля нужды, которую общество ещё может себе позволить
    std::vector<double> pref;     // мягкая локальная преференция по элементам
    std::string role;
    double strain = 0.0;          // 0..1 — доля незакрытых нужд (система чахнет)
    double tradeAccess = 0.5;     // 0..1 — насколько плотно систему обслуживают чужие торговцы

    Market();
    void seed(const std::vector<Resource>& localResources, const std::vector<double>& demandBias, const std::string& role_, double population, double industry);
    void update(double dt);
    void updatePrices(); // Один шаг релаксации цен без движения запасов
    double pricePressure() const;

    // Сделка на этом рынке: deltaUnits > 0 — товар пришёл (продажа игроком),
    // < 0 — ушёл (закупка). Двигает и запас, и цену — рынок имеет ГЛУБИНУ,
    // поэтому крупный рейс сам портит себе цену.
    void applyTrade(int elementIndex, double deltaUnits);

    // --- Чтение модели (для UI и ИИ) ---
    // Текущая цена одной единицы услуги f: минимум p(i)/q(i,f) по кандидатам.
    double serviceCost(int functionIndex) const;
    // Справедливая цена элемента: чего он стоит по своей лучшей услуге.
    double fairPrice(int elementIndex) const;
    // Доля элемента в закрытии функции при текущих ценах, [0,1].
    double marketShare(int elementIndex, int functionIndex) const;
    // Запас в годах текущего потребления (большое число = завал).
    double coverageYears(int elementIndex) const;
    // Индекс функции, которой этот рынок в основном закрывает данный элемент.
    int dominantNeedFunction() const;
};

// Опорная цена скопления: чего элемент стоит на «типичном» рынке.
// Отклонение локальной цены от опорной — сигнал арбитража для игрока.
double marketReferencePrice(int elementIndex);
