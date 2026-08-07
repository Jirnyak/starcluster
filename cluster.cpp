
#include "cluster.h"
#include "i18n.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdint>

// Реализация конструктора ClusterStar
ClusterStar::ClusterStar(double x_, double y_, double z_, const std::string& name_)
    : x(x_), y(y_), z(z_), name(name_) {}

// ---------------------------------------------------------------------------
// ИМЕНА СИСТЕМ
//
// До этого система звалась `Star_417`, и восемь тысяч систем были неразличимы:
// запомнить «ту, где дёшево железо» игрок не мог — только записать номер. Номер
// остался (по нему ищут), но перед ним встало произносимое слово.
//
// Слово собирается ФОНЕМАМИ, а не буквами: начало слога (может быть пустым) +
// гласная + необязательный конец. Отсюда и «человечность» — в имени физически
// не может оказаться «аывгрш»: каждый слог открывается согласной и закрывается
// гласной, а стечения согласных берутся только из готового списка (br, str, kl).
//
// Таблицы ПАРНЫЕ: латинская и кириллическая записи стоят рядом. Поэтому имя не
// «транслитерируется наугад» — обе записи собираются из одних и тех же кусков,
// а starNameCyrillic просто перечитывает готовую латиницу той же таблицей.
// ---------------------------------------------------------------------------
namespace {

struct Phon {
    const char* lat;
    const char* cyr;
};

// Начала слогов. Первые SIMPLE_ONSETS — одиночные согласные, они выпадают
// вчетверо чаще: стечения вроде «str» хороши как редкая приправа, а не как
// правило, иначе скопление звучит как чихание. По той же причине шипящие
// вынесены за границу простых: одно «Чадо» на десяток имён — характер, десять
// подряд — акцент.
const Phon ONSETS[] = {
    {"b", "б"}, {"d", "д"}, {"f", "ф"}, {"g", "г"}, {"h", "х"}, {"k", "к"},
    {"l", "л"}, {"m", "м"}, {"n", "н"}, {"p", "п"}, {"r", "р"}, {"s", "с"},
    {"t", "т"}, {"v", "в"}, {"z", "з"},
    // --- редкие: шипящие и стечения ---
    {"sh", "ш"}, {"ch", "ч"}, {"ts", "ц"},
    {"br", "бр"}, {"dr", "др"}, {"gr", "гр"}, {"kr", "кр"}, {"pr", "пр"},
    {"tr", "тр"}, {"vr", "вр"}, {"kl", "кл"}, {"pl", "пл"}, {"fl", "фл"},
    {"gl", "гл"}, {"sl", "сл"}, {"sk", "ск"}, {"sp", "сп"}, {"st", "ст"},
    {"sn", "сн"}, {"tv", "тв"}, {"str", "стр"},
};
const size_t SIMPLE_ONSETS = 15;

// Гласные. Первые SIMPLE_VOWELS — чистые; дальше дифтонги, они дают имени
// длину и «дальность» звучания (Тайрон, Науми), но в меру.
const Phon VOWELS[] = {
    {"a", "а"}, {"e", "е"}, {"i", "и"}, {"o", "о"}, {"u", "у"},
    {"ai", "ай"}, {"au", "ау"}, {"ei", "ей"}, {"ou", "оу"}, {"ia", "иа"},
    {"io", "ио"}, {"ea", "еа"},
};
const size_t SIMPLE_VOWELS = 5;

// Концы слогов. Только сонорные и глухие смычные — то, что человек выговорит
// после любой гласной. Первые SIMPLE_CODAS — одиночные (сонорные повторены,
// чтобы выпадали чаще); двойные разрешены ТОЛЬКО в конце слова: в середине за
// ними идёт ещё согласная начала слога, и «Arkgullte» уже не прочитать.
const Phon CODAS[] = {
    {"n", "н"}, {"r", "р"}, {"s", "с"}, {"l", "л"}, {"m", "м"}, {"k", "к"},
    {"t", "т"}, {"n", "н"}, {"r", "р"}, {"l", "л"},
    {"sk", "ск"}, {"st", "ст"}, {"rn", "рн"}, {"nd", "нд"}, {"rk", "рк"},
    {"ss", "сс"}, {"nn", "нн"}, {"ll", "лл"}, {"x", "кс"},
};
const size_t SIMPLE_CODAS = 10;

// Чтение латиницы кириллицей. Порядок ВАЖЕН: длинные куски проверяются первыми,
// иначе «sh» распадётся на «сх». Всё, чего в таблице нет (цифры, дефис,
// апостроф), проходит насквозь.
const Phon TRANSLIT[] = {
    {"str", "стр"},
    {"sh", "ш"}, {"ch", "ч"}, {"ts", "ц"},
    {"ai", "ай"}, {"au", "ау"}, {"ei", "ей"}, {"ou", "оу"},
    {"ia", "иа"}, {"io", "ио"}, {"ea", "еа"},
    {"a", "а"}, {"b", "б"}, {"c", "к"}, {"d", "д"}, {"e", "е"}, {"f", "ф"},
    {"g", "г"}, {"h", "х"}, {"i", "и"}, {"j", "ж"}, {"k", "к"}, {"l", "л"},
    {"m", "м"}, {"n", "н"}, {"o", "о"}, {"p", "п"}, {"q", "к"}, {"r", "р"},
    {"s", "с"}, {"t", "т"}, {"u", "у"}, {"v", "в"}, {"w", "в"}, {"x", "кс"},
    {"y", "и"}, {"z", "з"},
};

// Детерминированный поток: имя должно зависеть ТОЛЬКО от (seed, index), иначе
// одна и та же звезда в сейве и в новой партии с тем же зерном звалась бы
// по-разному. Отдельный поток ещё и не сдвигает rng самого скопления.
struct NameRng {
    uint64_t state;
    explicit NameRng(uint64_t s) : state(s) {}
    uint64_t next() {
        uint64_t x = (state += 0x9E3779B97F4A7C15ull);
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
    size_t pick(size_t n) { return size_t(next() % uint64_t(n)); }
    // Доля [0,1): для «выпало ли редкое».
    double unit() { return double(next() % 100000ull) / 100000.0; }
};

// simpleOnly — когда предыдущий слог закрылся согласной: «рн» + «стр» подряд
// не выговорит никто, и имя перестаёт быть именем.
const Phon& pickOnset(NameRng& rng, bool simpleOnly) {
    const size_t total = sizeof(ONSETS) / sizeof(ONSETS[0]);
    if (simpleOnly || rng.unit() < 0.78) return ONSETS[rng.pick(SIMPLE_ONSETS)];
    return ONSETS[rng.pick(total)];
}

// simpleOnly — дифтонт на имя разрешён ОДИН. Два («Киамаучем») превращают слово
// в кашу, которую не повторишь вслух с первого раза.
const Phon& pickVowel(NameRng& rng, bool simpleOnly) {
    const size_t total = sizeof(VOWELS) / sizeof(VOWELS[0]);
    if (simpleOnly || rng.unit() < 0.78) return VOWELS[rng.pick(SIMPLE_VOWELS)];
    return VOWELS[rng.pick(total)];
}

std::string upperAsciiCopy(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'a' && out[i] <= 'z') out[i] = char(out[i] - 'a' + 'A');
    }
    return out;
}

// Заглавная первая буква. Для кириллицы это сдвиг кодовой точки, а не байта:
// а-п лежат в 0xD0 0xB0..0xBF, р-я — в 0xD1 0x80..0x8F.
void capitaliseFirst(std::string& s) {
    if (s.empty()) return;
    const unsigned char b0 = (unsigned char)s[0];
    if (b0 >= 'a' && b0 <= 'z') { s[0] = char(b0 - 'a' + 'A'); return; }
    if (s.size() < 2) return;
    const unsigned char b1 = (unsigned char)s[1];
    if (b0 == 0xD0 && b1 >= 0xB0 && b1 <= 0xBF) {          // а..п
        s[1] = char(b1 - 0x20);
    } else if (b0 == 0xD1 && b1 >= 0x80 && b1 <= 0x8F) {   // р..я
        s[0] = char(0xD0);
        s[1] = char(b1 + 0x20);
    }
}

// Одна попытка собрать основу. Возвращает латиницу; кириллица получается из неё
// транслитерацией, поэтому здесь её собирать не нужно.
std::string buildStem(NameRng& rng) {
    // Длина: короткое имя (Корн) читается быстро, длинное (Талирион) —
    // запоминается. Нужны оба, поэтому слогов 1..3 с перекосом в двусложные.
    const double lenRoll = rng.unit();
    const int syllables = lenRoll < 0.12 ? 1 : (lenRoll < 0.66 ? 2 : 3);
    const bool markInside = syllables == 3 && rng.unit() < 0.10;
    // Потолок основы. Имя стоит в узких панелях рядом с номером, и длинное
    // слово выдавливает оттуда цифры; заодно потолок гасит редкие уродцы вроде
    // «Spopouhass», где всё выпало длинным сразу.
    const size_t maxLen = 9;

    std::string lat;
    bool diphthongUsed = false;
    bool afterCoda = false;
    for (int s = 0; s < syllables; ++s) {
        // Первый слог иногда начинается с гласной (Арен, Илон) — так имена
        // перестают быть на одну колодку. Гласная при этом только чистая:
        // «Иогевун» с дифтонга начинается уже не именем, а опечаткой.
        const bool bare = (s == 0) && rng.unit() < 0.18;
        if (!bare) {
            // Стык согласных — единственное место, где имя может сломаться.
            // Перебираем начало слога, пока оно не встанет к предыдущей
            // согласной: не удлинит её («Gill» + «la» = «Gillla»), не склеится
            // с ней в чужую фонему («s» + «h» = «sh», «t» + «s» = «ts») и не
            // добавит третью согласную придыханием («nn» + «h»).
            const Phon* onset = &pickOnset(rng, afterCoda);
            const char prev = lat.empty() ? '\0' : lat[lat.size() - 1];
            for (int tries = 0; tries < 4 && prev != '\0'; ++tries) {
                const char first = onset->lat[0];
                const bool tripled = first == prev;
                const bool glued = (prev == 's' && first == 'h') ||
                                   (prev == 't' && first == 's') ||
                                   (prev == 'c' && first == 'h');
                const bool breathy = afterCoda && first == 'h';
                if (!tripled && !glued && !breathy) break;
                onset = &pickOnset(rng, true);
            }
            lat += onset->lat;
        }
        const bool simpleVowel = bare || diphthongUsed || lat.size() + 3 > maxLen;
        const Phon& vowel = pickVowel(rng, simpleVowel);
        if (vowel.lat[1] != '\0') diphthongUsed = true;
        lat += vowel.lat;
        // Конец слога — у последнего часто, у внутренних редко: иначе слово
        // запирается согласными и теряет распевность.
        const bool last = s == syllables - 1;
        const double codaChance = last ? 0.55 : 0.18;
        afterCoda = false;
        if (syllables == 1 || rng.unit() < codaChance) {
            const size_t codaPool = last ? sizeof(CODAS) / sizeof(CODAS[0]) : SIMPLE_CODAS;
            const Phon& coda = CODAS[rng.pick(codaPool)];
            const size_t codaLen = coda.lat[1] == '\0' ? 1 : 2;
            const char prev = lat.empty() ? '\0' : lat[lat.size() - 1];
            const bool doubled = prev == coda.lat[0] && codaLen == 1;
            if (!doubled && lat.size() + codaLen <= maxLen) {
                lat += coda.lat;
                afterCoda = true;
            }
        }
        if (markInside && s == 0) lat += '\'';
        if (lat.size() + 2 > maxLen) break;  // на ещё один слог места нет
    }
    capitaliseFirst(lat);
    return lat;
}

}  // namespace

std::string starNameCyrillic(const std::string& latin) {
    const size_t count = sizeof(TRANSLIT) / sizeof(TRANSLIT[0]);
    std::string out;
    out.reserve(latin.size() * 2);
    for (size_t i = 0; i < latin.size();) {
        const char c = latin[i];
        const char lower = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
        if (lower < 'a' || lower > 'z') { out += c; ++i; continue; }
        bool matched = false;
        for (size_t k = 0; k < count; ++k) {
            const size_t len = std::char_traits<char>::length(TRANSLIT[k].lat);
            if (i + len > latin.size()) continue;
            bool same = true;
            for (size_t n = 0; n < len && same; ++n) {
                const char a = latin[i + n];
                const char al = (a >= 'A' && a <= 'Z') ? char(a - 'A' + 'a') : a;
                same = al == TRANSLIT[k].lat[n];
            }
            if (!same) continue;
            out += TRANSLIT[k].cyr;
            i += len;
            matched = true;
            break;
        }
        if (!matched) { out += c; ++i; }
    }
    capitaliseFirst(out);
    return out;
}

std::string starNameFor(unsigned int seed, size_t index) {
    NameRng rng(uint64_t(seed) * 0x100000001B3ull + uint64_t(index) * 0x9E3779B1ull + 0xC0FFEEull);
    std::string stem;
    // Имя не должно совпасть со словом интерфейса: словарь перевода не знает,
    // что «MINE» здесь — имя собственное, и превратил бы систему в «ШАХТУ».
    // Совпадения редки, поэтому хватает нескольких перекатов; на последнем
    // просто добавляем слог, чтобы цикл был конечным при любом словаре.
    for (int attempt = 0; attempt < 6; ++attempt) {
        stem = buildStem(rng);
        if (stem.size() >= 3 && !I18N::isInterfaceWord(upperAsciiCopy(stem))) break;
        if (attempt == 5) stem += pickVowel(rng, true).lat + std::string(1, 'n');
    }
    return stem + "-" + std::to_string(index);
}

std::string starNameStem(const std::string& full) {
    const size_t cut = full.find_last_of('-');
    if (cut == std::string::npos || cut == 0) return full;
    return full.substr(0, cut);
}

void Cluster::registerNames() const {
    for (size_t i = 0; i < stars.size(); ++i) {
        const std::string& full = stars[i].name;
        const std::string stem = starNameStem(full);
        if (stem.size() == full.size()) continue;  // имя без номера — сейв до §25
        I18N::registerProperNoun(stem, starNameCyrillic(stem));
    }
}

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

        stars.emplace_back(x, y, z, starNameFor(seed, i));

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

    // Новое скопление — новый набор имён: старый реестр относится к миру,
    // которого больше нет, и незачем тащить его через рестарт.
    I18N::clearProperNouns();
    registerNames();

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
