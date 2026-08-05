# Ship Mechanics Plan

This document defines the target ship model. It keeps the current minimal 3D
movement, but adds mass, fuel, upgrade pressure, and a reason to choose between
cargo, speed, range, armor, weapons, and sensors.

## Goal

Ships are expandable physical agents. A ship is not only a sprite and max speed:
it is a bundle of mass, thrust, fuel, cargo, modules, crew/automation, and
orders. The player and AI use the same ship rules.

The desired gameplay pressure:

- bigger cargo means more profit per trip but slower acceleration;
- faster ships need better drives and burn more fuel;
- armed or armored ships sacrifice cargo/range;
- fuel is a normal resource, bought and sold like everything else;
- route planning becomes a physical and economic decision.

## Current State

`Ship` currently has:

```text
name
x, y, z
speed
vx, vy, vz
acceleration
cargo[]
cargoCapacity
ownerFaction
targetStar
enRoute
```

This is already 3D. The missing layer is mass/fuel/modules.

## Implementation Status

Топливо больше не абстрактный счётчик. Реализовано:

- `Ship` имеет `driveIndex`, `fuel`/`fuelVolume`, `propellant`/`propellantVolume`,
  `throttleBias`.
- **Топливо и рабочее тело разделены** (см. «Fuel And Propellant»).
- **Бункер и бак — СМЕСИ**, такие же списки `Resource`, как трюм. Никаких
  «одна ёмкость — один элемент»: грузится что угодно, свойства считаются
  средневзвешенными по массе. Выбор вещества стал градиентом качества,
  а не набором запретов — залить железо в реактор можно, толку не будет.
- Ёмкости меряются в ОБЪЁМЕ. Сколько влезет по массе — решает плотность
  смеси. Это главный размен между синтезом и делением.
- Порог поджига движка — не гейт, а середина плавной сходимости
  (`driveIgnitionFraction`): что реактор не тянет, едет балластом.
- Ёмкости заполняются переливом из трюма (`shipLoadFuel`/`shipLoadPropellant`,
  обратно `shipDrainFuel`/`shipDrainPropellant`) либо станционным макросом
  «купи и залей» с наценкой `REFINERY_MARKUP` (+90%). Своё сырьё — бесплатно.
- Расход рабочего тела считается по Циолковскому; скорость истечения
  выбирается минимизацией стоимости по локальным ценам и ограничена сверху
  физикой пары движок/смесь. Расход снимается со смеси ПРОПОРЦИОНАЛЬНО
  долям компонентов.
- Сгоревшее топливо возвращается в трюм ЗОЛОЙ (`elementAshProduct`), покомпонентно.
- Слот `Drive` эксклюзивен: движок ровно один, новый вытесняет старый.
  Все проверки идут ДО снятия старого, иначе неудачная замена оставляла
  корабль без двигателя.
- UI: окно `HOLD / TANKS` — три колонки `БУНКЕР | ТРЮМ | БАК`, трюм в центре,
  потому что перелив всегда идёт через него. У каждой строки ЯВНЫЕ кнопки-стрелки:
  в трюме `<` грузит топливо и `>` рабочее тело, в ёмкостях стрелка к центру
  сливает обратно. Количество берётся из поля AMOUNT окна торговли, пустое = вся
  строка. Внизу сводка по установке: энергия смеси уже с учётом поджига, скорость
  истечения и средняя молярная масса струи.
  Выбор цели отделён от вылета: кнопка в окне системы называется `DESTINATION`
  и только НАЗНАЧАЕТ цель, а стартует отдельный `GO` на карте. Между ними
  игрок настраивает двигатель под эту цель, в том числе кнопкой `OPTIMAL`.
  Из окна торговли убраны кнопки перелива и мёртвая кнопка SHIPYARD (верфь
  открывается кнопкой YARD в окне системы). Кнопка `BUY FUEL+PROP` осталась —
  это автозаправка обеих ёмкостей по станционной цене с наценкой.
  Режим двигателя задаётся шкалой `THROTTLE` там же: щелчок по шкале ставит
  значение, засечка посередине — ценовой оптимум. Рядом видно, во что обходится
  типовой манёвр при текущем режиме, поэтому размен виден в цифрах.
  Шаг перелива задаётся полем `STEP/CLICK` в правом нижнем углу окна — это
  ТО ЖЕ значение, что AMOUNT окна торговли: одно число на все операции с
  веществом, отдельного состояния не заводим. Пустое поле = вся строка.
  Перегруз показан красным прямо в шапке колонки трюма.
  Геометрия стрелок и кнопки сброса закреплена регрессией в `ui_click_test`.
- **Перегруз трюма РАЗРЕШЁН.** `cargoCapacity` — паспортная норма, а не
  физическая стена: залить можно что угодно и сколько угодно, слив из баков
  и зола вместимостью не режутся. Цена перегруза — `startJourney` не выпускает
  корабль, пока масса груза выше нормы. Без этого игрок не мог вылить бак,
  чтобы сменить топливную схему, и застревал намертво. Выход из перегруза
  вдали от рынка — кнопка `X` (за борт) в окне HOLD.
- Save-версия 12; старые сейвы отвергаются.

Deferred:

- heat/radiators;
- weapons/armor mass.

## Target Fields

Add gradually:

```cpp
struct Ship {
    double dryMass;
    double moduleMass;
    double cargoMass;
    double fuelMass;
    double maxCargoMass;
    double fuelCapacityMass;

    double driveThrust;       // mass * c / year
    double driveEfficiency;   // 0..1
    double maxSpeed;          // fraction of c
    double heatCapacity;
    double radiatorPower;

    int fuelElement = -1;     // element index used by current drive
    double fuelReserve;       // amount units, converted to mass by element

    std::vector<ShipModule> modules;
};
```

Keep the current `speed` field temporarily as `maxSpeed` compatibility.

## Cargo Quantity And Mass

The final model should separate quantity from mass. Current `Resource.amount`
acts like a cargo amount. Migration plan:

```text
cargoMass(resource) =
    resource.amount
  * element.atomicMass
  * cargoMassScale
```

Start with:

```text
cargoMassScale = 0.01
```

The scale is a gameplay unit bridge. It lets current markets continue to work
while ship physics begins using mass.

Total mass:

```text
shipMass =
    dryMass
  + moduleMass
  + cargoMass
  + fuelMass
```

## Acceleration

Use Newton-like proportionality without exact SI units:

```text
availableAcceleration = driveThrust / max(minMass, shipMass)
acceleration = min(designAccelerationCap, availableAcceleration)
```

This is enough for gameplay:

- cargo haulers accelerate slowly when full;
- empty ships handle better;
- warships are heavy if armored;
- high-thrust drives are expensive and fuel-hungry.

## Fuel And Propellant

Ядерный двигатель — это ИСТОЧНИК ЭНЕРГИИ плюс СПОСОБ превратить её в тягу.
Это два РАЗНЫХ вещества, и путать их нельзя:

- **Топливо** — источник энергии. Остаётся в активной зоне и перегорает.
  Расходуется медленно. Зола падает обратно в трюм.
- **Рабочее тело** — реактивная масса. Улетает в сопло безвозвратно.
  Расходуется по Циолковскому, то есть много.

Энергия топлива — одна формула на синтез и деление, расстояние до пика
кривой связи (`bindingPerNucleon` по Вайцзеккеру уже есть в элементах):

```text
specificEnergy(Z) = bindingPeak - bindingPerNucleon(Z)     // МэВ/нуклон
```

Ноль ровно на железе, растёт в обе стороны. H = 8.79, U = 1.14, Fe = 0.16.
Перевод в доли c² — делением на `NUCLEON_REST_MEV`, поэтому ядерная и
кинетическая энергия считаются в одних единицах без подгоночных множителей.

Размен, который держит выбор топлива живым:

| | на единицу МАССЫ | на единицу ОБЪЁМА |
|---|---|---|
| синтез (H) | 8.79 | 1.5 |
| деление (U) | 1.14 | 18.7 |

Синтез в 7.7x сильнее на массу, деление в 12.6x на объём. Бункер ограничен
объёмом — поэтому стартовый корабль это ЯРД на тории с водородным рабочим
телом, ровно как NERVA. Обе проверки закреплены в `balance_test`.

Три семейства двигателей (`drive.h`):

```text
Thermal  ve ~ sqrt(2*T/A)      греет рабочее тело; нужно лёгкое; высокая тяга
Ion      ve ~ sqrt(V)/A^0.22   разгоняет полем; выгодно плотное; тяга мизерная
Torch    ve ~ sqrt(2*E/c²)     топливо И ЕСТЬ выхлоп; второй бак не нужен
```

Что движок вообще примет как топливо, решает не `specificEnergy`, а
`max(fusionFuelTrait, fissionFuelTrait)`: энергия в ядре может БЫТЬ, но
кулоновский барьер и окно делимости до неё не пускают. Поэтому свинец
реактором не накормить, а сверхтяжёлые отсекаются сами.

## Movement Fuel Cost

В сопло уходит ТОЛЬКО рабочее тело. Топливо перегорает в активной зоне в золу
практически той же массы и остаётся на борту — значит топливо разгоняет само
себя, и уравнение самоссылочно. Пусть `P` — полезная нагрузка (корпус и груз),
`W` — рабочее тело, `F` — топливо, `x = deltaV * DELTAV_SCALE / ve`:

```text
W = (P + F) * (e^x - 1)          Циолковский: улетает только W
F = W * ve^2 / (2 * E)           энергия на разгон этого W
=>  F = k*P / (1 - k),   k = ve^2 / (2E) * (e^x - 1)
```

`E` — энергия топлива на единицу массы в долях c², то есть `specificEnergy`
(уже с учётом доли поджига), делённая на `NUCLEON_REST_MEV`, умноженная на КПД
струи.

**При `k >= 1` решения нет вообще.** Топливо, нужное чтобы везти топливо,
съедает само себя. Это не переполнение и не ошибка — это честный предел
достижимости: такой манёвр не выполнить ни при каком запасе, нужен другой
режим, другое рабочее тело или другой движок. Именно этот член делает деление
непригодным для релятивистских скоростей истечения, а синтез — пригодным.

Расход в полёте считается в ДИФФЕРЕНЦИАЛЬНОЙ форме, от массы ТЕКУЩЕЙ:

```text
burn = m0 * (1 - e^(-dv * DELTAV_SCALE / ve))
```

Это критично, потому что полёт интегрируется по тикам. Если брать за точку
отсчёта одну и ту же конечную массу на каждом тике, сумма `sum(e^xi - 1)`
окажется НАМНОГО меньше `e^(sum xi) - 1` — экспонента выпуклая. На замере такой
расход занижался в шесть раз. В дифференциальной форме тики телескопируются
точно в общий Циолковский.

`DELTAV_SCALE` сжимает маршрутный delta-V перед экспонентой. Форма физики
сохранена целиком (экспонента, оптимум, предел достижимости), без множителя
межзвёздные delta-V делают недостижимым вообще всё.

Скорость истечения — свободный параметр с настоящим ценовым оптимумом:

```text
cost(ve) = P_prop * W(ve) + P_fuel * F(ve)
```

Слева экспонента наказывает низкую ve, справа энергия наказывает высокую.
Минимум внутри, и его положение зависит от ЛОКАЛЬНЫХ цен на топливо и рабочее
тело — то есть выгодный режим двигателя меняется от системы к системе.

Найденная точка ФИКСИРУЕТСЯ в `Ship::cruiseExhaust` (см. `shipTuneDrive`) при
вылете, при движении ручки и при смене состава баков. Это принципиально: если
выбирать ve заново на каждом вызове, планировщик и реальный расход считают по
разным ценам и по разным dV, и прогноз перестаёт сходиться с фактом.

Ручка `throttle` (0..1) выбирает точку внутри РЕАЛЬНО достижимой полосы —
границы задаются условием `k < 1` и объёмом баков, а не сырым потолком сопла:

```text
0.0  нижний край полосы: рабочего тела много, топлива мало, корабль тяжелее
0.5  ценовой оптимум среди достижимых режимов (значение по умолчанию)
1.0  верхний край полосы: рабочего тела минимум, топлива максимум
```

Вторая ручка — `cruiseFraction` (0.2..1), доля от потолка скорости корпуса.
Лететь медленнее объективно дешевле: бюджет быстроты равен `2*artanh(peak)`,
поэтому снижение крейсера режет расход ОБОИХ расходников (замер: 40% крейсера
вместо 100% дают расход втрое меньше). Раньше корабль всегда шёл на полном
потолке и этот размен игроку был недоступен.

Кнопка `OPTIMAL` перебирает пару (ручка, крейсер) под НАЗНАЧЕННУЮ цель и
берёт самую дешёвую связку, которая долетает и влезает в баки.

Интерполяция логарифмическая. Крайне левое положение — НЕ самое дешёвое:
минимум расхода топлива лежит внутри полосы, а дальше влево топливо снова
растёт, потому что рабочего тела приходится швырять слишком много. Поэтому ось
подписана не «эконом/форсаж», а честно — ЧЕМ платим: `PROP` слева, `FUEL` справа.

## Relativity

Скорости складывать нельзя — складывать можно БЫСТРОТУ `w = artanh(v/c)`.
Именно она входит в релятивистское уравнение Циолковского, поэтому маршрутный
бюджет считается как `2*artanh(peak)`, а не `2*peak`. Расход в полёте берёт
приращение быстроты `dw = dv / (1 - v²)`: тот же прирост скорости стоит тем
дороже, чем ближе корабль к световой. Кинетическая энергия струи считается как
`(gamma - 1)`, а не `0.5*v²`.

Надбавка невелика на наших скоростях (+0.08% при 0.05c, +2.7% при 0.28c,
+9.9% при 0.5c), но она снимает бессмыслицу: без неё бюджет «разгон до 0.5c
плюс торможение» получался ровно `1.0c` — величина, которой не существует.

Честно про масштаб: `DELTAV_SCALE = 0.004` сжимает delta-V в 250 раз, и это
несопоставимо больше релятивистской поправки. Для рейса «разгон до 0.28c и
торможение» при скорости истечения 0.0264c честный Циолковский требует
массовое число `e^21.2` — это 1.6 МИЛЛИАРДА килограммов рабочего тела на
килограмм корабля. Величина подобрана НЕ по вкусу, а по сквозной метрике: при 0.015 перевозка
стоила дороже торговой маржи, и половина флота вставала («в пути» падало с 95%
до 46%), а простаивающие агенты каждый такт заново гоняли дорогой поиск сделок —
кадр симуляции удваивался. При 0.004 в пути 88% и кадр 36 мс.
Без этой подгонки межзвёздный полёт невозможен ни на каком топливе — что,
собственно, правда и о реальности. Сжатие сохраняет всю ФОРМУ физики
(экспоненту, оптимум, стену достижимости) и переносит только масштаб.

## Cruise Is Free

Профиль полёта — разгон, крейсер, торможение. На крейсерском участке двигатель
не работает: в вакууме нет сопротивления, скорость держится сама. Раньше
интегратор жёг delta-V каждый тик и на крейсере тоже, а прирост скорости тут же
срезался ограничением `ship.speed` — топливо горело в пустоту, и расход не
сходился с маршрутной оценкой, которая считает только разгон и торможение.

## Route Viability

Before accepting a route, estimate:

```text
routeDistance = dist3d(origin, destination)
deltaVBudget = routeDistanceFactor(routeDistance, maxSpeed, acceleration)
fuelNeeded = estimateFuelForDeltaV(shipMass, deltaVBudget, fuelElement)
```

If current fuel is insufficient:

- player route UI shows "insufficient fuel";
- AI either buys fuel, chooses another route, or waits;
- emergency drift can be added later but should be rare.

## Hull Speed Ladder

Потолок скорости корпуса — часть лестницы прогресса, а не константа. Раньше
`ShipClass` вообще не имел поля скорости: покупка следующего корабля не давала
ни единицы хода. Теперь скорость ВЫВОДИТСЯ из двух полей той же таблицы:

```text
tier    = clamp01((log10(price) - 4) / 11)              // 10^4 .. 10^15
agility = clamp01((driveThrust/dryMass - 0.15) / 1.05)  // 0.15 .. 1.2
maxSpeed = 0.10 + 0.42 * clamp01(0.65*tier + 0.35*agility)
```

Ступень цены задаёт общий уровень, тяговооружённость — роль внутри ступени.
Отсюда стартовый «Hauler» идёт 0.121c, «Interceptor» 0.235c, «Blockade Runner»
0.302c, «Titan» 0.488c, а грузовозы на своей ступени всегда медленнее бойцов.
Спектр сплошной: худшая дыра между соседними по скорости корпусами 0.052c,
то есть расти по ходу можно постепенно, а не прыжком. Всё это закреплено
регрессией «лестница скоростей гладкая» — руками в таблицу ничего не забито,
поэтому лестница не может разъехаться при правке класса.

## Local Flight Is Free

Внутрисистемный полёт (клавиша `L`) топливо НЕ ЖЖЁТ вообще. Топливо и рабочее
тело тратятся только на межзвёздные плечи: `moveShipToward` и торможение дрейфа
в глубоком космосе — единственные места расхода. Файлы `local*.cpp` про баки не
знают вовсе. Это гарантия по построению, и именно поэтому она закреплена
проверкой «полёт внутри системы бесплатен»: сломать такое молча проще всего.

## Speed Cap

Max speed remains a design cap:

```text
if speed3d > maxSpeed:
    velocity *= maxSpeed / speed3d
```

Mass affects acceleration and fuel consumption, not the absolute speed cap.
This keeps the model readable.

## Ship Modules

Modules are data-driven:

```cpp
struct ShipModuleDef {
    std::string id;
    double mass;
    double price;
    double cargoBonus;
    double fuelCapacityBonus;
    double thrustBonus;
    double efficiencyBonus;
    double sensorBonus;
    double weaponPower;
    double armor;
};
```

No inheritance tree is needed. A ship sums module fields.

Initial module categories:

- cargo bay;
- fuel tank;
- drive;
- reactor;
- radiator;
- sensor;
- armor;
- weapon;
- automation core.

## Player Decisions

Ship UI should expose:

```text
mass dry/cargo/fuel/total
max cargo
fuel resource
fuel remaining in years/routes
acceleration empty/full
max speed
estimated fuel cost to selected star
profit after fuel cost
```

This makes trading about route economics, not just price spread.

## AI Requirements

AI route scoring must include:

```text
expectedProfit
- fuelCost
- timeCost
- riskCost
- opportunityCost
```

AI must not pick routes it cannot fuel.

## Implementation Steps

1. Add `atomicMass` to elements.
2. Add `shipMass()` and `cargoMass()` helpers.
3. Add fuel fields to `Ship`, defaulting to a valid starter fuel.
4. Consume fuel during acceleration and braking.
5. Prevent player/AI routes when fuel is insufficient.
6. Add local fuel buying UI in trade window.
7. Add basic module definitions and ship stat recomputation.
8. Add shipyard market behavior and upgrade screen.

## Physical Levers Not Yet Used

Модель выводит всё из электронной конфигурации и кривой связи, и часть
получившихся величин пока не работает как ось развития. Список — чтобы не
изобретать заново.

Уже посчитано, но задействовано слабо:

| величина | где используется сейчас | чем могла бы стать |
|---|---|---|
| `specificEnergy` | вся двигательная модель | единственный по-настоящему выведенный параметр; задаёт потолок всему |
| `density` | объём баков | размен fusion/fission уже держится на ней; могла бы решать и компоновку трюма |
| `ionizationEase` | КПД ионного движка | отдельное семейство рабочих тел, ионники под конкретный элемент |
| `handlingRisk` | режет КПД струи | радиация экипажу, износ корпуса, контрабандный статус груза, запрет на стоянку у обитаемых миров |
| `nuclearStability` | выбор стартового топлива | распад запаса со временем: нестабильное топливо нельзя копить, только жечь сразу |

Не посчитано, но выводится из тех же полей без единого спецкейса:

- **удельная теплоёмкость** по Дюлонгу-Пти (`~3R/A`) — физический предел
  нагрева камеры, то есть настоящий потолок термодвижка вместо нынешнего
  подогнанного `chamberEnergy`;
- **температура плавления** (из энергии связи решётки: `dShellFill`,
  `metallicTrait`, `atomicMass`) — материалы камеры и сопла, верхняя граница
  тепловой ветки и повод для отдельной ветки апгрейдов;
- **сечение захвата нейтронов** (из чётности `Z`/`N` и близости к магическим
  числам) — какие элементы годятся в замедлитель и отражатель активной зоны,
  то есть третий расходник рядом с топливом и рабочим телом.

Общий принцип, которому всё это должно следовать: величина считается для всех
118 элементов одной формулой, порог превращается в плавную сходимость, а не в
запрет, и ни один элемент не упоминается по имени.
