# Economy Plan

This document defines the target economy. The game is an economic trade
simulation first: local prices, local scarcity, fuel costs, travel delay, stale
information, and faction control produce decisions.

## Current Implementation

First market-memory layer:

- each faction has `faction * star` market snapshot metadata;
- each faction has flat `faction * star * element` remembered prices;
- local observation captures current market prices immediately for the player;
- NPC observations emit delayed `MarketReport` signals to the nearest faction
  relay;
- trader and player auto-trade route scoring use remembered destination prices,
  not live remote market truth;
- unknown destination markets are ignored by auto-trade;
- stale market snapshots add route scoring penalty.

Still missing:

- supply and demand pressure snapshots;
- confidence ranges for stale market prices;
- local ledgers and bank settlement signals;
- tariffs from relation matrix;
- blockade/piracy risk effects on route scoring.

## Goal

The player starts as one trader trying to get rich. The same systems must scale
to faction treasuries, autonomous traders, war logistics, colonies, quests, and
ship upgrades.

Core loop:

```text
observe local market
compare with known/stale remote markets
buy cargo and fuel
choose a route
spend years traveling
sell, refuel, accept contracts, upgrade
eventually found/capture systems and control fleets
```

## Market Rules

There is no global market. Each star has one local market:

```text
supply[resource]
demand[resource]
price[resource]
productionRate[resource]
consumptionRate[resource]
localTariff
blockadeFactor
lastUpdateTime
```

Price:

```text
scarcity = (demand + demandRate * lookahead + 1) /
           (supply + productionRate * lookahead + 1)

price = basePrice * scarcity^elasticity * localModifiers
```

Suggested:

```text
elasticity = 0.55..0.75
lookahead = 60..100 ticks
```

## Base Price

Base price comes from `elements.md`:

```text
basePrice =
    scarcityTrait
  * (energyValue + chemicalValue + industrialValue + stabilityValue)
  * runPriceNoise
```

Hydrogen, helium, heavy fission resources, oxidizers, reducers, stable
conductors, and carbon-chain-like elements should become valuable because of
derived traits, not because the market names them.

## Local Demand

Demand is generated from system role and state:

```text
demandWeight =
    populationNeed
  + industryNeed
  + shipyardNeed
  + militaryNeed
  + lifeSupportNeed
  + energyNeed
  + constructionNeed
  + luxuryPrestigeNeed
```

Each term consumes element traits:

```text
energyNeed       -> nuclearEnergyTrait, lowMassFuelTrait
lifeSupportNeed  -> oxidizerTrait, reducerTrait, volatileTrait
constructionNeed -> structuralTrait, metallicTrait
electronicsNeed  -> conductorTrait, stabilityTrait
militaryNeed     -> energyTrait, densityTrait, structuralTrait
prestigeNeed     -> prestigeMaterialTrait
```

All local demand terms should be coefficients in data or generated fields, not
hardcoded per element.

## Trade Profit

Route value:

```text
grossSpread =
    remoteSellPrice - localBuyPrice

expectedProfit =
    grossSpread * cargoAmount
  - fuelCost
  - maintenanceCost
  - tariffs
  - riskExpectedLoss
  - capitalTimeCost
  - staleInfoPenalty
```

Travel time:

```text
distance = dist3d(origin, destination)
travelTime = shipTravelEstimate(distance, shipMass, driveStats)
```

Fuel cost must be included before a route looks profitable.

## Stale Market Knowledge

Normal player and AI should not read all live markets unless they have debug
truth. Market reports are signals.

Market snapshot:

```cpp
struct MarketSnapshot {
    int star;
    double observedTime;
    std::vector<float> prices;
    std::vector<float> supplyPressure;
    std::vector<float> demandPressure;
};
```

Displayed uncertainty:

```text
age = now - observedTime
confidence = exp(-age / marketMemoryTau)
priceLow  = knownPrice * exp(-volatility * sqrt(age))
priceHigh = knownPrice * exp( volatility * sqrt(age))
```

AI can take risks using stale data, but the scoring must penalize age.

## Faction Economy

Faction budget is not a magic global number. It is the result of local ledgers
and delayed settlements:

```text
localLedger[star][faction]
pendingSettlement[]
estimatedTreasury[faction]
```

Local colonies spend local funds immediately. Strategic faction AI uses
estimated treasury, which can be stale.

## Taxes And Tariffs

Each faction controls tariff policy:

```text
tariff =
    baseTariffByOwner
  + relationTariff
  + warTariff
  + blockadeTariff
```

The player faction follows the same rule.

## Production And Consumption

Production should be slow-ticked, not per frame.

```text
supply += productionRate * dt
demand += consumptionRate * dt
```

Production rate depends on:

- local resource reservoir;
- colony infrastructure;
- energy availability;
- labor/automation;
- imports of required industrial inputs;
- war/blockade damage.

## Implementation Steps

1. Move element category pricing to universal traits.
2. Add route fuel and time cost.
3. Add market snapshots to player knowledge.
4. Make player auto-trade use known snapshots, not omniscient market truth.
5. Add tariffs by owner faction.
6. Add local ledgers for factions.
7. Add settlement signals for faction capital.
8. Add blockade and piracy effects on route risk and prices.


## Элементы как расходники двигателя

Элемент — универсальный товар, и это не метафора: один и тот же список
`Resource` лежит в трюме, в топливном бункере и в баке рабочего тела.
Поэтому у каждого элемента ДВА независимых спроса.

1. **Промышленный** — из химических и структурных траитов (см. `elements.md`).
2. **Двигательный** — из ядерных: `specificEnergy` для топлива, атомная масса
   и плотность для рабочего тела (см. `ship.md`).

Отсюда экономические следствия, которых не было у абстрактного топлива:

- Актиниды дороги вдвойне: они и топливо, и сырьё. Их цена держит верх рынка.
- Водород дёшев и вездесущ, но как рабочее тело он рыхлый — его возят
  объёмом, а не массой, и на дальних плечах он становится узким местом.
- **Зола — товар.** Сгоревшее топливо возвращается в трюм новым элементом
  (актиниды делятся к середине таблицы, лёгкие ядра сливаются вверх), и его
  можно продать или залить обратно как рабочее тело. Это делает сам полёт
  источником сырья, а не только расходом.
- Станционная заправка идёт с наценкой `REFINERY_MARKUP` (+90%), поэтому
  собственная топливная логистика — отдельная и осмысленная статья дохода.

Оптимальная скорость истечения зависит от ЛОКАЛЬНОГО отношения цен топлива и
рабочего тела (`shipRouteCost`), то есть выгодный режим двигателя меняется от
системы к системе. Это связывает экономику и физику полёта в один контур.

---

## Второй этаж: экзотическая материя (§31)

Химия кончается на кривой связи: около 8 МэВ на нуклон — всё, что можно вынуть
из ядра. Поздняя игра живёт ЗА этим пределом, и это не «редкие товары», а три
состояния вещества, каждое со своим редким типом звезды:

| | Откуда | Доля систем |
|---|---|---|
| `ANTIMATTER` | производится: завод плюс яркая звезда | рынок 8.9%, источников 275 из 8192 |
| `NEUTRONIUM` | собирается с коры нейтронной звезды или из диска ЧД | рынок 3.6%, источников 37 |
| `CONDENSATE` | только система при МЁРТВОЙ звезде (белый карлик) | рынок 4.9%, источников 28 |

Цена собрана из тех же двух величин, что и на обычном рынке — сколько вещества
здесь появляется и сколько его здесь тратят, — с вилкой до 8.6x между чистым
источником и чистым потребителем. Запас лениво релаксирует к цели за 260 лет:
выбранный подчистую источник не кончается, а дорожает.

Экономические следствия:

- **Антивещество — не апгрейд, а расходник.** Оно едет в бункере вместе с
  топливом и входит в смесь: 1863 МэВ на нуклон против 1…8 у ядерного топлива,
  поэтому доля в тысячную по массе утраивает энергию смеси. И улетает с рейсом.
- **Нейтрониум — масса.** Единица вчетверо тяжелее любого элемента таблицы, и
  наваренный на корпус слой реально сажает разгон. Живучесть покупается ходом.
- **Конденсат — валюта прокачки.** Кузница даёт ядро ВЫБРАННОГО стата, в отличие
  от рулетки исследований, и платит за него не кошелёк, а рейс к мёртвой звезде.

Тариф лицензии удерживается с экзотики так же, как с руды: один рейс с
конденсатом закрывает тысячелетнюю квоту целиком. Это и есть выход из квотной
ловушки поздней игры.

## Акции держав (§33)

Собственность на систему (§13) — это миллиард и это МЕСТО: за кассой надо
прилетать. Акция географии не имеет и продаётся в любой момент, но платит хуже:
владелец берёт всю пошлину с оборота, акционер — только ту четверть, которую
держава распределяет, а не тратит на флоты и войны. Система окупается за полвека,
акция за 279 лет.

⚠️ Цена идёт от ДОХОДА, а не от активов: `(казна + доход × 50) / 1e6` за акцию.
Первая версия считала казну плюс цену всех подвластных систем по формуле §13 — и
дала 20 млн Cr за акцию при дивиденде 18 Cr, то есть окупаемость в миллион лет.
Цена системы собрана из контекстных множителей, доход идёт от оборота, и
отношение между ними гуляет на пять порядков.

Книга публикуется по одной державе за такт фракций, поэтому котировка отстаёт от
жизни на несколько лет. Репутация возчика (тир 0.55) держит отчёт свежим — это
единственный, кроме размера заказов, выход у репутации.
