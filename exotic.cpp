#include "exotic.h"
#include "cluster.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Экзотическая материя: где рождается, кто потребляет, сколько стоит (§31).
//
// Здесь нет ни одного «балансного» числа, привязанного к сюжету: цена собрана
// из тех же двух величин, из которых собрана цена на обычном рынке, — сколько
// вещества здесь ПОЯВЛЯЕТСЯ и сколько его здесь ТРАТЯТ. Всё остальное —
// физика звезды, уже посчитанная генератором.
// ---------------------------------------------------------------------------

const double EXOTIC_RECOVERY_YEARS = 260.0;
const double EXOTIC_ANNIHILATION_MEV = 1863.0;   // 2 * m_p c^2, МэВ на нуклон пары

namespace {

// Опорные цены. Порядок величины выбран не «по ощущению», а из масштаба поздней
// игры: система стоит порядка 1e9 Cr (§13), полный отсек экзотики должен стоить
// порядка 1e8, отсек держит 250 единиц ⇒ единица порядка 1e5..1e6 Cr. Обычный
// элемент стоит единицы кредитов, поэтому это ДРУГОЙ этаж экономики, а не
// «дорогой товар»: возить его начинают тогда, когда трюм с иридием перестаёт
// что-либо решать.
const ExoticDef DEFS[EX_COUNT] = {
    {"AM", "ANTIMATTER", 240000.0, 0.02,
     "ANNIHILATION CATALYST - MIXES INTO THE BUNKER"},
    {"NM", "NEUTRONIUM", 95000.0, 4.00,
     "DEGENERATE MATTER - THE DENSEST ARMOUR THERE IS"},
    {"QC", "CONDENSATE", 420000.0, 0.35,
     "COHERENT SUBSTRATE - THE BODY OF A CHROMOCORE"},
};

// Порог для ПОТРЕБИТЕЛЯ: рынок экзотики есть либо там, где вещество РОЖДАЕТСЯ
// (источник редок сам по себе), либо там, где его столько тратят, что имеет
// смысл держать причал под ввоз. Второе — привилегия крупнейших промышленных
// узлов, и порог взят по замеру: это примерно 98-й процентиль стока по
// скоплению, то есть около 2% систем на вещество.
const double SINK_GATE = 4.2;

double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Сколько вещества звезда даёт САМА. Ноль — здесь его не рождается.
double sourceStrength(const ClusterStar& star, int kind) {
    const double popW = starPopulationWeight(star);
    if (kind == EX_ANTIMATTER) {
        // Антивещество не находят — его ДЕЛАЮТ, и цена его равна цене энергии.
        // Значит, нужен и завод, и то, что этот завод питает: светимость.
        // Слабая промышленность фабрику античастиц не тянет ни при какой звезде.
        if (star.industry < 1.35) return 0.0;
        const double power = std::pow(std::max(star.luminosity, 1.0e-6), 0.25);
        if (power < 0.85) return 0.0;
        return (star.industry - 1.35) * power * std::pow(std::max(popW, 0.02), 0.30);
    }
    if (kind == EX_NEUTRONIUM) {
        // Вырожденное вещество не производится вообще. Его СОБИРАЮТ: с коры
        // нейтронной звезды или из аккреционного диска чёрной дыры. Больше
        // взять его негде, и это единственная причина туда лететь.
        if (star.stellarClass == 1) return 1.6 + star.mass * 0.9;
        if (star.stellarClass == 2) return 1.1 + std::min(star.mass, 15.0) * 0.12;
        return 0.0;
    }
    // Конденсат живёт когерентностью, а когерентность живёт покоем. Нужна
    // система, где НИЧЕГО не происходит: ни вспышек, ни звёздного ветра, ни
    // приливов, — то есть система при МЁРТВОЙ звезде. Белый карлик и есть
    // такая: горячий, но крошечный и абсолютно тихий. Металличность говорит,
    // из чего строить решётку.
    //
    // ⚠️ Сначала сюда же были записаны «просто холодные» звёзды, и это было
    // ошибкой масштаба: красных карликов в скоплении три четверти, конденсат
    // оказался у 58% систем и перестал быть поводом куда-то лететь. Мёртвых
    // звёзд в скоплении полпроцента — вот это и есть повод.
    if (star.stellarClass != 3) return 0.0;
    return 1.5 * (0.45 + star.metallicity) * std::sqrt(std::max(star.industry, 0.1));
}

// Кто вещество ТРАТИТ. От этого зависит, дорого ли оно здесь стоит.
double sinkStrength(const ClusterStar& star, int kind) {
    const double popW = starPopulationWeight(star);
    if (kind == EX_ANTIMATTER) {
        // Жгут его двигатели, а двигателей много там, где много движения:
        // промышленность и люди.
        return std::max(0.0, star.industry - 0.5) * std::pow(std::max(popW, 0.02), 0.35);
    }
    if (kind == EX_NEUTRONIUM) {
        // Броня нужна тем, кто её носит: верфи и гарнизоны. Множитель 0.08
        // приводит оборону к той же шкале, в которой мерится население у двух
        // других веществ, — чтобы порог `SINK_GATE` был один на всех.
        return std::max(0.0, star.industry - 0.6) * (0.35 + star.defense) * 0.08;
    }
    // Хромокоры собирают там, где есть кому их собирать.
    return std::max(0.0, star.industry - 0.7) * std::pow(std::max(popW, 0.02), 0.45);
}

// Множитель цены от расклада «источник против потребителя». Чистый источник
// продаёт дёшево, чистый потребитель покупает дорого: ровно это и есть повод
// возить вещество между ними.
double supplyDemandFactor(double supply, double sink) {
    // Вилка 0.34 .. 2.94, то есть до 8.6x номинала между чистым источником и
    // чистым потребителем. Замер: при вилке вдвое уже полный отсек давал 1.07x
    // капитала за рейс — на фоне обычной торговли (типичная сделка 3.0x, §17)
    // хайтек-этаж оказывался ХУЖЕ трюма с рудой, и лететь к мёртвой звезде было
    // незачем. С этой вилкой рейс даёт около 2.5x при отсеке в 360 единиц.
    return 0.34 + 2.60 * sink / (sink + supply + 0.30);
}

// Множитель цены от ВЫРАБОТАННОСТИ запаса. Выбранный подчистую источник не
// «кончается»: он дорожает, и в какой-то момент везти оттуда становится
// незачем. Обратная сторона — сброс большой партии сбивает цену.
double scarcityFactor(double stock, double target) {
    if (target <= 0.0) return 1.0;
    const double fill = clampd(stock / target, 0.02, 6.0);
    return clampd(std::pow(fill, -0.55), 0.34, 4.0);
}

}  // namespace

const std::vector<ExoticDef>& exoticDefs() {
    static const std::vector<ExoticDef> defs(DEFS, DEFS + EX_COUNT);
    return defs;
}

double exoticTargetStock(const ClusterStar& star, int kind) {
    if (kind < 0 || kind >= EX_COUNT) return 0.0;
    const double s = sourceStrength(star, kind);
    const double d = sinkStrength(star, kind);
    // Либо здесь это рождается, либо здесь этого столько тратят, что держат
    // причал под ввоз. Всё остальное скопление про экзотику даже не слышало.
    if (s <= 0.0 && d < SINK_GATE) return 0.0;
    // ГЛУБИНА рынка. Источник держит склад того, что накопал; потребитель —
    // запас того, что жжёт. Второе слагаемое не декоративное: без него ввозной
    // буфер был 45 единиц против отсека в 360, полный корабль обваливал цену в
    // три раза и рейс давал 1.07x капитала — то есть хайтек-этаж существовал,
    // но не имел смысла. Промышленный узел, сжигающий антивещество тоннами,
    // обязан быть способен принять корабль целиком.
    return 45.0 + 640.0 * s + 90.0 * d;
}

double exoticStockSourceStrength(const ClusterStar& star, int kind) {
    if (kind < 0 || kind >= EX_COUNT) return 0.0;
    return sourceStrength(star, kind);
}

bool exoticStarHasMarket(const ClusterStar& star) {
    for (int k = 0; k < EX_COUNT; ++k) {
        if (exoticTargetStock(star, k) > 0.0) return true;
    }
    return false;
}

double exoticPriceAt(const ClusterStar& star, int kind, double stock, double priceLevel) {
    if (kind < 0 || kind >= EX_COUNT) return 0.0;
    const double target = exoticTargetStock(star, kind);
    if (target <= 0.0) return 0.0;
    const double s = sourceStrength(star, kind);
    const double d = sinkStrength(star, kind);
    const double level = (priceLevel > 0.0 && std::isfinite(priceLevel)) ? priceLevel : 1.0;
    return DEFS[kind].basePrice * level * supplyDemandFactor(s, d) * scarcityFactor(stock, target);
}

double exoticExecutionPrice(const ClusterStar& star, int kind, double stock,
                            double priceLevel, double units, bool selling) {
    if (kind < 0 || kind >= EX_COUNT || units <= 0.0) return 0.0;
    const double target = exoticTargetStock(star, kind);
    if (target <= 0.0) return 0.0;
    // Цена — функция ЗАПАСА, поэтому среднюю цену исполнения честнее всего
    // взять интегралом по ходу сделки. Экспоненты тут нет (закон степенной),
    // и интеграл берётся численно — но дёшево: сделка на рынке экзотики
    // случается несколько раз за партию, а не в каждом кадре.
    const int steps = 12;
    double sum = 0.0;
    double s = std::max(0.0, stock);
    const double step = units / steps;
    for (int i = 0; i < steps; ++i) {
        sum += exoticPriceAt(star, kind, s, priceLevel);
        s = exoticStockAfterTrade(star, kind, s, selling ? step : -step);
    }
    return sum / steps;
}

double exoticStockAfterTrade(const ClusterStar& star, int kind, double stock, double deltaUnits) {
    const double target = exoticTargetStock(star, kind);
    if (target <= 0.0) return stock;
    // Дно не ноль, а «почти ноль»: запас, упавший в точный ноль, обнулил бы и
    // цену исполнения на следующем шаге (деление на выработанность), а физика
    // говорит другое — последние крохи стоят дороже всего.
    return clampd(stock + deltaUnits, target * 0.02, target * 6.0);
}

double exoticRelaxStock(const ClusterStar& star, int kind, double stock, double years) {
    const double target = exoticTargetStock(star, kind);
    if (target <= 0.0) return stock;
    if (!(years > 0.0)) return stock;
    const double k = std::exp(-years / EXOTIC_RECOVERY_YEARS);
    return target + (stock - target) * k;
}
