#include "shell.h"

#include "render2d.h"
#include "ui.h"
#include "i18n.h"
#include "stb_image.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

// Помощник поиска ассетов (рядом с бинарём, затем в CWD). Определён в main.cpp —
// это единственная точка, где оболочка опирается на точку входа, поэтому
// shell.cpp намеренно исключён из LIBSOURCES (см. Makefile).
extern std::string assetPath(const std::string& relative);

namespace Shell {
namespace {

using UI::P;

// ---------------------------------------------------------------------------
// Тайминги и константы поля клеток
// ---------------------------------------------------------------------------

const float STEP = 1.0f / 60.0f;      // фиксированный шаг: лента кадров индексируется им
const int   SCATTER_STEPS = 165;      // длина ленты разбегания (симулируется мгновенно)
const float REWIND_LEAD   = 0.45f;    // пауза перед началом промотки: успеть увидеть хаос
const float REWIND_RATE   = 0.85f;    // скорость промотки назад (кадров ленты за шаг)
const float HOLD_SECONDS  = 1.35f;    // сколько держим собранную надпись
const int   SUPER = 2;                // клеток на один пиксель шрифта 5x7
const int   COMIC_SLIDES = 5;
// Пунктов главного меню: NEW GAME / LOAD GAME / LANGUAGE / SOUND / EXIT.
const int   MENU_ITEMS = 5;

// --- Курсор как физический толкатель ---------------------------------------
// Скорость курсора считается в НАСТОЯЩИХ px/с (rel за кадр / dt), поэтому резкий
// взмах бьёт кратно сильнее медленного ведения — это и есть управление силой.
const float PUSH_RADIUS    = 110.0f;  // радиус влияния курсора, px
const float PUSH_RADIAL    = 900.0f;  // расталкивание из-под курсора, px/с²
const float PUSH_DRAG      = 4.5f;    // доля собственной скорости курсора, передаваемая клетке
const float PUSH_HELD_GAIN = 2.1f;    // во сколько раз давит сильнее с зажатой ЛКМ
const float PUNCH_RADIUS   = 240.0f;  // радиус щелчка ЛКМ
const float PUNCH_IMPULSE  = 1900.0f; // мгновенный импульс щелчка в центре, px/с
const float CELL_MAX_SPEED = 3200.0f; // потолок скорости клетки, px/с

// --- Инерция сбитых клеток ---------------------------------------------------
// Сбитая клетка — снаряд, а не «резинка на магните»: она летит дальше, трение
// медленно съедает импульс, и только по мере торможения включается тяга домой.
const float FRICTION     = 0.988f;    // множитель скорости за кадр (60 Гц) — «трение»
const float FLY_SPEED    = 850.0f;    // выше этой скорости тяга домой выключена
const float SPRING_K     = 26.0f;     // жёсткость возвращения
const float SPRING_DAMP  = 9.0f;      // ~критическое демпфирование при SPRING_K
const float EDGE_BOUNCE  = 0.78f;     // отскок от края экрана
const float CELL_BOUNCE  = 0.55f;     // отскок клетки от клетки
// «Горячая» клетка — та, что летит БЫСТРЕЕ, чем её тянула бы одна лишь пружина.
// Сталкиваются только пары, где хоть одна горячая: спокойно доезжающие до слотов
// проходят друг сквозь друга. Иначе на финальном сближении они заклинивают —
// пружина толкает внутрь, расталкивание наружу, и клетка встаёт рядом со слотом.
const float HOT_MARGIN   = 1.25f;     // во сколько раз быстрее «своей» скорости
const float HOT_FLOOR    = 140.0f;    // и не медленнее этого, px/с
const float REWIND_MAX   = 45.0f;     // потолок фазы промотки, с (защита от вечной игры)

// --- Фон: шаровое скопление -------------------------------------------------
// Не декорация, а та же физика, что в лоре игры: звёзды падают к общему центру
// масс, проскакивают разреженное ядро насквозь и уходят наружу с другой стороны.
// Потенциал Пламмера (смягчённый — иначе ускорение в центре обращается в
// бесконечность), интегратор полуявный Эйлер (симплектический: энергия не
// уплывает за минуты работы меню).
const int   CLUSTER_STARS = 720;
const float CLUSTER_CORE_K = 0.13f;   // радиус ядра как доля меньшей стороны окна
const float CLUSTER_VCIRC  = 62.0f;   // круговая скорость на радиусе ядра, px/с
const float CLUSTER_EDGE_K = 3.6f;    // обрез гало: дальше ядра во столько раз
const float CLUSTER_PUSH_R = 130.0f;  // радиус влияния курсора на звёзды, px

struct Star {
    float x, y, z;        // положение относительно центра масс, px
    float vx, vy, vz;
    unsigned char tone;   // спектральный оттенок (O..M), см. drawCluster
};

// Клетка поля. Никакого ООП: голые данные, свободные функции над вектором.
struct Cell {
    float x, y;        // текущая позиция (левый верхний угол клетки)
    float vx, vy;      // скорость, px/с
    float hx, hy;      // «домашний» слот в надписи
    float wob;         // фаза собственного дрожания (у звёзд фона)
    float cur;         // СВОЙ курсор по ленте: у каждой клетки свой темп отмотки
    float rate;        // множитель темпа (0.65..1.35) — слово собирается не разом
    unsigned char word;  // 0 = верхнее слово, 1 = нижнее, 2 = свободная звезда фона
    unsigned char freed; // 1 = физическая: сошла с ленты, живёт инерцией и отскоками
};

enum Phase {
    // Разбегание НЕ показывается: игрок с первого кадра видит уже рассыпанное
    // поле и сразу может мешать сборке. Лента блуждания симулируется мгновенно
    // при инициализации (primeLogo) — на экран попадает только её отмотка.
    PH_REWIND = 0,   // лента отматывается назад — клетки возвращаются в слово
    PH_HOLD,         // слово собрано, держим
    PH_MENU,         // главное меню (клетки пересобрались в заголовок игры)
    PH_COMIC,        // слайды пролога, мир генерится в фоне
    PH_LOADING,      // комикс кончился, мир ещё не готов
    PH_READY,        // мир готов, карточка управления, PRESS ENTER
    PH_DONE
};

// ---------------------------------------------------------------------------
// Раскладка надписи в клетки по шрифту 5x7 (render2d.cpp::glyph)
// ---------------------------------------------------------------------------

int wordCellWidth(const char* text) {
    const int len = int(std::strlen(text));
    return len > 0 ? len * 6 * SUPER - SUPER : 0;
}

// Каждый зажжённый пиксель глифа разворачивается в блок SUPER x SUPER клеток —
// поэтому надпись читается как крупное клеточное поле, а не как текст.
void layoutWord(const char* text, int cellPx, int originX, int originY,
                std::vector<SDL_Point>& out) {
    const int len = int(std::strlen(text));
    for (int i = 0; i < len; ++i) {
        const char* bits = UI::glyph(text[i]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (bits[row * 5 + col] != '1') continue;
                for (int sy = 0; sy < SUPER; ++sy) {
                    for (int sx = 0; sx < SUPER; ++sx) {
                        SDL_Point p;
                        p.x = originX + ((i * 6 + col) * SUPER + sx) * cellPx;
                        p.y = originY + (row * SUPER + sy) * cellPx;
                        out.push_back(p);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Состояние оболочки
// ---------------------------------------------------------------------------

struct State {
    int phase = PH_REWIND;
    float phaseTime = 0.0f;

    int winW = 1200, winH = 900;

    // Собственный ГПСЧ. Правило §2: ничто вне симуляции не смеет тянуть из
    // глобального `rng` — иначе один seed даёт разные миры (это уже ловили).
    std::mt19937 rnd;

    std::vector<Cell> cells;
    std::vector<short> tape;   // [шаг][клетка] -> (x, y), пишется в primeLogo
    int tapeSteps = 0;
    // Равномерная сетка для столкновений клетка-клетка (переиспользуется каждый шаг).
    std::vector<int> gridHead, gridNext;
    std::vector<unsigned char> hot;   // «летит быстрее собственной тяги» — см. HOT_MARGIN
    int logoCellPx = 9;
    int titlePx = 6;           // размер клетки заголовка меню (для раскладки подписи)
    float drawPx = 9.0f;       // текущий размер клетки (плавно меняется при пересборке)
    float targetPx = 9.0f;
    float stubborn = 0.0f;     // упрямство: чем дольше мешают, тем жёстче пружина

    // Курсор как физический толкатель
    float mx = -1000.0f, my = -1000.0f;
    float mvx = 0.0f, mvy = 0.0f;   // px/с (не «пикселей за кадр»)
    bool  lmbHeld = false;          // с зажатой ЛКМ курсор давит сильнее
    int pushed = 0;                 // сколько клеток игрок успел сбить

    // Фон — настоящее шаровое скопление (см. buildCluster/stepCluster)
    std::vector<Star> stars;
    // Скопление живёт только с меню: на заставке студии экран должен быть пустым,
    // чтобы клетки логотипа не тонули в звёздной пыли. 0 = скрыто и не считается.
    float clusterFade = 0.0f;
    float coreR = 120.0f;      // радиус ядра Пламмера, px
    float gm = 1.0e6f;         // GM скопления (подбирается от размера окна)

    // Меню
    int menuIndex = 0;
    bool hasSave = false;
    std::vector<SDL_Rect> menuRects;

    // Комикс
    int slide = 0;
    float slideText = 0.0f;
    SDL_Texture* slideTex[COMIC_SLIDES];
    bool slidesLoaded = false;

    // Фоновая генерация мира
    std::thread worker;
    std::atomic<bool> workerDone;
    std::atomic<bool> workerOk;
    bool workerRunning = false;
    float workElapsed = 0.0f;

    int outcome = OUTCOME_QUIT;
    bool quitRequested = false;

    State() : workerDone(false), workerOk(false) {
        for (int i = 0; i < COMIC_SLIDES; ++i) slideTex[i] = NULL;
    }
};

float frand(std::mt19937& r) {
    return float(std::uniform_real_distribution<double>(0.0, 1.0)(r));
}
float srand2(std::mt19937& r) { return frand(r) * 2.0f - 1.0f; }

// ---------------------------------------------------------------------------
// Сборка поля под текущий размер окна
// ---------------------------------------------------------------------------

void buildLogo(State& s) {
    s.cells.clear();
    s.tape.clear();
    s.tapeSteps = 0;

    const char* top = "TENEVIK";
    const char* bottom = "GAMES";
    const int topCells = wordCellWidth(top);
    const int botCells = wordCellWidth(bottom);
    int cellPx = int(s.winW * 0.62f) / std::max(1, topCells);
    if (cellPx < 3) cellPx = 3;
    if (cellPx > 16) cellPx = 16;
    s.logoCellPx = cellPx;
    s.drawPx = s.targetPx = float(cellPx);

    const int rowH = 7 * SUPER * cellPx;
    const int topY = s.winH / 2 - rowH - int(cellPx * 2.0f);
    const int botY = topY + rowH + int(cellPx * 3.0f);

    std::vector<SDL_Point> slots;
    layoutWord(top, cellPx, s.winW / 2 - topCells * cellPx / 2, topY, slots);
    const size_t topCount = slots.size();
    layoutWord(bottom, cellPx, s.winW / 2 - botCells * cellPx / 2, botY, slots);

    s.tape.reserve(size_t(SCATTER_STEPS) * slots.size() * 2);
    s.cells.reserve(slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        Cell c;
        c.x = c.hx = float(slots[i].x);
        c.y = c.hy = float(slots[i].y);
        c.vx = c.vy = 0.0f;
        c.wob = frand(s.rnd) * 6.28f;
        c.cur = 0.0f;
        // Свой темп отмотки у каждой клетки: иначе всё слово защёлкивается разом
        // и читается как один механический кадр, а не как сборка.
        c.rate = 0.65f + frand(s.rnd) * 0.70f;
        c.word = (unsigned char)(i < topCount ? 0 : 1);
        c.freed = 0;
        s.cells.push_back(c);
    }
}

// Пересборка: те же клетки получают новые слоты. Лишние становятся звёздами
// фона, недостающие досыпаются с краёв экрана. Так студийная заставка
// физически перетекает в заголовок игры — ни одна клетка не исчезает даром.
void retarget(State& s, const std::vector<SDL_Point>& slots, int cellPx) {
    s.targetPx = float(cellPx);
    const size_t n = slots.size();
    for (size_t i = 0; i < s.cells.size(); ++i) {
        Cell& c = s.cells[i];
        c.freed = 1;
        if (i < n) {
            c.hx = float(slots[i].x);
            c.hy = float(slots[i].y);
            c.word = 0;
        } else {
            c.hx = frand(s.rnd) * s.winW;
            c.hy = frand(s.rnd) * s.winH;
            c.word = 2;
        }
    }
    for (size_t i = s.cells.size(); i < n; ++i) {
        Cell c;
        c.hx = float(slots[i].x);
        c.hy = float(slots[i].y);
        c.x = frand(s.rnd) < 0.5f ? -20.0f : float(s.winW + 20);
        c.y = frand(s.rnd) * s.winH;
        c.vx = c.vy = 0.0f;
        c.wob = frand(s.rnd) * 6.28f;
        c.cur = 0.0f;
        c.rate = 1.0f;
        c.word = 0;
        c.freed = 1;
        s.cells.push_back(c);
    }
}

void retargetTitle(State& s) {
    const char* title = "STARCLUSTER";
    const int cells = wordCellWidth(title);
    int cellPx = int(s.winW * 0.66f) / std::max(1, cells);
    if (cellPx < 2) cellPx = 2;
    if (cellPx > 12) cellPx = 12;
    s.titlePx = cellPx;
    std::vector<SDL_Point> slots;
    layoutWord(title, cellPx, s.winW / 2 - cells * cellPx / 2, int(s.winH * 0.13f), slots);
    retarget(s, slots, cellPx);
}

// Все клетки — в свободный дрейф (фон комикса и экрана загрузки).
void retargetField(State& s) {
    std::vector<SDL_Point> none;
    retarget(s, none, std::max(2, s.logoCellPx / 2));
}

// Строит шаровое скопление. Радиусы — по профилю Пламмера (плотное ядро,
// разреженное гало), направления равномерны по сфере, скорость — СЛУЧАЙНАЯ доля
// круговой в СЛУЧАЙНОМ направлении. Отсюда весь диапазон орбит: от почти
// круговых до радиальных «нырков» сквозь центр. Периоды у всех разные, поэтому
// синхронного «дыхания» скопления не возникает — как и в настоящем.
void buildCluster(State& s) {
    s.stars.clear();
    s.stars.reserve(CLUSTER_STARS);
    s.coreR = std::max(40.0f, float(std::min(s.winW, s.winH)) * CLUSTER_CORE_K);
    // GM из желаемой круговой скорости на радиусе ядра:
    //   v_c^2(a) = GM*a^2 / (2a^2)^{3/2} = GM / (2^{3/2} a)
    s.gm = CLUSTER_VCIRC * CLUSTER_VCIRC * 2.8284271f * s.coreR;

    const float edge = s.coreR * CLUSTER_EDGE_K;
    for (int i = 0; i < CLUSTER_STARS; ++i) {
        float r = 0.0f;
        for (int tries = 0; tries < 24; ++tries) {
            // Обращение профиля Пламмера: r = a / sqrt(X^(-2/3) - 1)
            const float X = 0.02f + frand(s.rnd) * 0.95f;
            r = s.coreR / std::sqrt(std::pow(X, -2.0f / 3.0f) - 1.0f);
            if (r <= edge) break;
        }
        r = std::min(r, edge);

        const float u = srand2(s.rnd);                       // равномерно по сфере
        const float th = frand(s.rnd) * 6.2831853f;
        const float sr = std::sqrt(std::max(0.0f, 1.0f - u * u));
        Star st;
        st.x = r * sr * std::cos(th);
        st.y = r * sr * std::sin(th);
        st.z = r * u;

        // Скорость — из ТОЧНОЙ функции распределения Пламмера: плотность
        // вероятности g(q) = q^2 (1-q^2)^{3.5}, где q = v / v_esc(r). Отбор
        // методом исключения. Первая версия брала «долю круговой скорости в
        // случайном направлении» — это не равновесие: ядро получало почти нулевые
        // скорости, гало избыточные, и скопление за десяток секунд раздувалось и
        // растворялось. С настоящей ФР оно держит форму сколь угодно долго.
        const float rr = r * r + s.coreR * s.coreR;
        const float vesc = std::sqrt(2.0f * s.gm / std::sqrt(rr));
        float q = 0.0f;
        for (int tries = 0; tries < 64; ++tries) {
            q = frand(s.rnd);
            const float g = q * q * std::pow(1.0f - q * q, 3.5f);
            if (frand(s.rnd) * 0.1f < g) break;
        }
        const float vu = srand2(s.rnd);
        const float vth = frand(s.rnd) * 6.2831853f;
        const float vsr = std::sqrt(std::max(0.0f, 1.0f - vu * vu));
        const float speed = q * vesc;
        st.vx = speed * vsr * std::cos(vth);
        st.vy = speed * vsr * std::sin(vth);
        st.vz = speed * vu;
        st.tone = (unsigned char)(frand(s.rnd) * 4.0f);
        s.stars.push_back(st);
    }

    // Гасим суммарный импульс: скопление должно висеть на месте, а не уплывать.
    float mx = 0.0f, my = 0.0f, mz = 0.0f;
    for (size_t i = 0; i < s.stars.size(); ++i) {
        mx += s.stars[i].vx; my += s.stars[i].vy; mz += s.stars[i].vz;
    }
    const float inv = s.stars.empty() ? 0.0f : 1.0f / float(s.stars.size());
    for (size_t i = 0; i < s.stars.size(); ++i) {
        s.stars[i].vx -= mx * inv; s.stars[i].vy -= my * inv; s.stars[i].vz -= mz * inv;
    }
}

void stepCluster(State& s, float dt) {
    if (s.stars.empty()) return;
    const float a2 = s.coreR * s.coreR;
    const float cx = s.winW * 0.5f, cy = s.winH * 0.5f;
    const float pushR2 = CLUSTER_PUSH_R * CLUSTER_PUSH_R;
    const float held = s.lmbHeld ? PUSH_HELD_GAIN : 1.0f;
    const bool cursorOn = s.mx > -500.0f;

    float mvx = 0.0f, mvy = 0.0f, mvz = 0.0f;
    for (size_t i = 0; i < s.stars.size(); ++i) {
        Star& st = s.stars[i];
        // Пламмер: a = -GM * r / (r^2 + a^2)^{3/2}. Смягчение убирает сингулярность
        // в центре — звезда проходит ядро насквозь и уходит на другую сторону.
        const float r2 = st.x * st.x + st.y * st.y + st.z * st.z + a2;
        const float k = s.gm / (r2 * std::sqrt(r2));
        st.vx -= st.x * k * dt;
        st.vy -= st.y * k * dt;
        st.vz -= st.z * k * dt;

        if (cursorOn) {
            const float dx = (cx + st.x) - s.mx;
            const float dy = (cy + st.y) - s.my;
            const float d2 = dx * dx + dy * dy;
            if (d2 < pushR2) {
                const float d = std::sqrt(d2) + 0.001f;
                const float w = (1.0f - d / CLUSTER_PUSH_R) * held;
                st.vx += (dx / d * 260.0f + s.mvx * 1.1f) * w * dt;
                st.vy += (dy / d * 260.0f + s.mvy * 1.1f) * w * dt;
            }
        }

        // Скопление связано: курсор подкачивает энергию, но улететь звезда не
        // может — скорость режется чуть ниже местной второй космической.
        const float vesc = std::sqrt(2.0f * s.gm / std::sqrt(r2));
        const float v2 = st.vx * st.vx + st.vy * st.vy + st.vz * st.vz;
        if (v2 > vesc * vesc * 0.9f) {
            const float sc = vesc * 0.9487f / std::sqrt(v2);
            st.vx *= sc; st.vy *= sc; st.vz *= sc;
        }

        // Приливная граница. У настоящих шаровых скоплений её ставит галактика;
        // здесь она ещё и сливает энергию, которую накачал курсор, — иначе за
        // минуту возни мышью скопление раздулось бы во весь экран.
        const float rad = std::sqrt(st.x * st.x + st.y * st.y + st.z * st.z);
        if (rad > s.coreR * CLUSTER_EDGE_K && rad > 0.001f) {
            const float nx = st.x / rad, ny = st.y / rad, nz = st.z / rad;
            const float vr = st.vx * nx + st.vy * ny + st.vz * nz;
            if (vr > 0.0f) {
                const float d = vr * 1.82f;   // отражение, радиальная скорость x0.82
                st.vx -= nx * d; st.vy -= ny * d; st.vz -= nz * d;
            }
        }
        mvx += st.vx; mvy += st.vy; mvz += st.vz;
    }

    // Толчки курсора — внешняя сила, она сдвинула бы всё скопление за экран.
    // Снимаем общий импульс: внутренние движения целы, центр масс стоит.
    const float inv = 1.0f / float(s.stars.size());
    mvx *= inv; mvy *= inv; mvz *= inv;
    for (size_t i = 0; i < s.stars.size(); ++i) {
        Star& st = s.stars[i];
        st.vx -= mvx; st.vy -= mvy; st.vz -= mvz;
        st.x += st.vx * dt;
        st.y += st.vy * dt;
        st.z += st.vz * dt;
    }
}

void punchCluster(State& s, float px, float py) {
    const float cx = s.winW * 0.5f, cy = s.winH * 0.5f;
    for (size_t i = 0; i < s.stars.size(); ++i) {
        Star& st = s.stars[i];
        const float dx = (cx + st.x) - px;
        const float dy = (cy + st.y) - py;
        const float d2 = dx * dx + dy * dy;
        if (d2 > PUNCH_RADIUS * PUNCH_RADIUS || d2 < 1.0e-4f) continue;
        const float d = std::sqrt(d2);
        const float w = 1.0f - d / PUNCH_RADIUS;
        st.vx += dx / d * 300.0f * w;
        st.vy += dy / d * 300.0f * w;
    }
}

// ---------------------------------------------------------------------------
// Физика поля
// ---------------------------------------------------------------------------

void clampSpeed(Cell& c) {
    const float v2 = c.vx * c.vx + c.vy * c.vy;
    if (v2 > CELL_MAX_SPEED * CELL_MAX_SPEED) {
        const float k = CELL_MAX_SPEED / std::sqrt(v2);
        c.vx *= k;
        c.vy *= k;
    }
}

// Курсор толкает клетки: расталкивание из-под острия + передача СОБСТВЕННОЙ
// скорости курсора. Резкий взмах уносит клетки заметно дальше медленного
// ведения, зажатая ЛКМ давит вдвое сильнее. Сбитая клетка сходит с ленты и
// дальше возвращается домой сама.
void applyCursor(State& s, float dt) {
    if (s.mx < -500.0f) return;
    const float speed = std::sqrt(s.mvx * s.mvx + s.mvy * s.mvy);
    const float held = s.lmbHeld ? PUSH_HELD_GAIN : 1.0f;
    const float R = PUSH_RADIUS;
    for (size_t i = 0; i < s.cells.size(); ++i) {
        Cell& c = s.cells[i];
        const float dx = c.x - s.mx;
        const float dy = c.y - s.my;
        const float d2 = dx * dx + dy * dy;
        if (d2 > R * R) continue;
        const float d = std::sqrt(d2) + 0.001f;
        const float k = 1.0f - d / R;
        const float nx = dx / d, ny = dy / d;
        c.vx += (nx * PUSH_RADIAL * k + s.mvx * PUSH_DRAG * k) * held * dt;
        c.vy += (ny * PUSH_RADIAL * k + s.mvy * PUSH_DRAG * k) * held * dt;
        clampSpeed(c);
        if (!c.freed && (speed > 40.0f || k > 0.5f)) {
            c.freed = 1;
            ++s.pushed;
        }
    }
}

// Щелчок ЛКМ — разовый взрыв: широкий радиус, мгновенный импульс, никакого dt.
// Отдельно от ведения, потому что это другой жест: не «смести», а «разбить».
void applyPunch(State& s, float px, float py) {
    for (size_t i = 0; i < s.cells.size(); ++i) {
        Cell& c = s.cells[i];
        const float dx = c.x - px;
        const float dy = c.y - py;
        const float d2 = dx * dx + dy * dy;
        if (d2 > PUNCH_RADIUS * PUNCH_RADIUS) continue;
        const float d = std::sqrt(d2) + 0.001f;
        const float k = 1.0f - d / PUNCH_RADIUS;
        c.vx += dx / d * PUNCH_IMPULSE * k;
        c.vy += dy / d * PUNCH_IMPULSE * k;
        clampSpeed(c);
        if (!c.freed) {
            c.freed = 1;
            ++s.pushed;
        }
    }
}

// Столкновения клетка-клетка. Считаются ТОЛЬКО между физическими (сошедшими с
// ленты) клетками: те, что ещё едут по записи, телепортируются по кадрам, и
// толкать их бессмысленно. Соседи ищутся по равномерной сетке — попарный
// перебор 850 клеток стоил бы 360 тыс. пар за шаг.
void collide(State& s) {
    size_t physical = 0;
    for (size_t i = 0; i < s.cells.size(); ++i) physical += s.cells[i].freed ? 1 : 0;
    if (physical < 2) return;

    // Диаметр берём от ЦЕЛЕВОГО шага решётки: на своих слотах соседи стоят ровно
    // в targetPx друг от друга, и при большем диаметре собранная надпись сама
    // себя расталкивала бы вечно.
    const float D = std::max(3.0f, s.targetPx * 0.92f);
    const float inv = 1.0f / D;
    const float off = 32.0f;
    const int gw = int((s.winW + off * 2.0f) * inv) + 2;
    const int gh = int((s.winH + off * 2.0f) * inv) + 2;
    if (gw < 1 || gh < 1) return;
    s.gridHead.assign(size_t(gw) * size_t(gh), -1);
    s.gridNext.assign(s.cells.size(), -1);
    s.hot.assign(s.cells.size(), 0);

    for (size_t i = 0; i < s.cells.size(); ++i) {
        const Cell& c = s.cells[i];
        if (!c.freed) continue;
        const float dx = c.hx - c.x, dy = c.hy - c.y;
        const float own = std::sqrt(dx * dx + dy * dy) * (SPRING_K / SPRING_DAMP);
        const float sp2 = c.vx * c.vx + c.vy * c.vy;
        const float bar = own * HOT_MARGIN + HOT_FLOOR;
        s.hot[i] = sp2 > bar * bar ? 1 : 0;
    }

    for (size_t i = 0; i < s.cells.size(); ++i) {
        if (!s.cells[i].freed) continue;
        const int gx = std::max(0, std::min(gw - 1, int((s.cells[i].x + off) * inv)));
        const int gy = std::max(0, std::min(gh - 1, int((s.cells[i].y + off) * inv)));
        const size_t b = size_t(gy) * size_t(gw) + size_t(gx);
        s.gridNext[i] = s.gridHead[b];
        s.gridHead[b] = int(i);
    }

    for (size_t i = 0; i < s.cells.size(); ++i) {
        Cell& a = s.cells[i];
        if (!a.freed) continue;
        const int gx = std::max(0, std::min(gw - 1, int((a.x + off) * inv)));
        const int gy = std::max(0, std::min(gh - 1, int((a.y + off) * inv)));
        for (int oy = -1; oy <= 1; ++oy) {
            const int ny = gy + oy;
            if (ny < 0 || ny >= gh) continue;
            for (int ox = -1; ox <= 1; ++ox) {
                const int nx = gx + ox;
                if (nx < 0 || nx >= gw) continue;
                for (int j = s.gridHead[size_t(ny) * size_t(gw) + size_t(nx)];
                     j >= 0; j = s.gridNext[size_t(j)]) {
                    if (j <= int(i)) continue;          // каждую пару разбираем один раз
                    // Обе клетки спокойно едут по домам — пропускаем друг сквозь
                    // друга. Сталкивается только то, что реально летит.
                    if (!s.hot[i] && !s.hot[size_t(j)]) continue;
                    Cell& b = s.cells[size_t(j)];
                    const float dx = a.x - b.x, dy = a.y - b.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 >= D * D || d2 < 1.0e-4f) continue;
                    const float d = std::sqrt(d2);
                    const float nrmX = dx / d, nrmY = dy / d;
                    const float push = (D - d) * 0.5f;
                    a.x += nrmX * push; a.y += nrmY * push;
                    b.x -= nrmX * push; b.y -= nrmY * push;
                    const float vn = (a.vx - b.vx) * nrmX + (a.vy - b.vy) * nrmY;
                    if (vn >= 0.0f) continue;           // уже разлетаются
                    const float imp = -(1.0f + CELL_BOUNCE) * vn * 0.5f;
                    a.vx += nrmX * imp; a.vy += nrmY * imp;
                    b.vx -= nrmX * imp; b.vy -= nrmY * imp;
                    clampSpeed(a); clampSpeed(b);
                }
            }
        }
    }
}

void integrate(State& s, float dt, bool homing) {
    const float k = SPRING_K * (1.0f + 2.2f * s.stubborn);
    // Порог «снаряда» растёт вместе с упрямством: мешать можно долго, но не вечно.
    const float flyGate = FLY_SPEED * (1.0f + s.stubborn * 1.5f) / (1.0f + s.stubborn * 4.0f);
    const float damp = std::pow(FRICTION, dt * 60.0f);
    for (size_t i = 0; i < s.cells.size(); ++i) {
        Cell& c = s.cells[i];
        // ⚠️ Клетка НА ЛЕНТЕ физике не подчиняется: её положение целиком диктует
        // запись. Без этой строки тяга домой действовала и на неё — в паузе
        // перед промоткой (REWIND_LEAD) поле успевало наполовину собраться, а с
        // первым же кадром отмотки скачком возвращалось назад. Замер: meanDist
        // падал 335 -> 135 за 0.43 с и мгновенно откатывался на 324.
        if (homing && !c.freed) continue;
        if (homing) {
            const float dx = c.hx - c.x;
            const float dy = c.hy - c.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float sp = std::sqrt(c.vx * c.vx + c.vy * c.vy);
            if (c.word == 2) {
                // Звезда фона: медленный дрейф вокруг своей точки, без фиксации.
                c.wob += dt * 0.7f;
                c.vx += (dx * 0.9f + std::cos(c.wob) * 6.0f) * dt;
                c.vy += (dy * 0.9f + std::sin(c.wob * 1.3f) * 6.0f) * dt;
            } else {
                // Тяга домой ВЫКЛЮЧЕНА, пока клетка летит быстро: сбитая клетка
                // сначала снаряд (инерция + трение + отскоки), и только когда
                // трение съело импульс, она снова начинает искать свой слот.
                const float pull = 1.0f - std::min(1.0f, sp / flyGate);
                if (pull > 0.0f) {
                    const float noise = std::min(1.0f, dist / 220.0f) * 260.0f;
                    c.vx += ((dx * k - c.vx * SPRING_DAMP) + srand2(s.rnd) * noise) * pull * dt;
                    c.vy += ((dy * k - c.vy * SPRING_DAMP) + srand2(s.rnd) * noise) * pull * dt;
                }
                if (dist < 1.2f && sp < 40.0f) {
                    c.x = c.hx; c.y = c.hy;
                    c.vx *= 0.25f; c.vy *= 0.25f;
                }
            }
        }
        c.vx *= damp;
        c.vy *= damp;
        c.x += c.vx * dt;
        c.y += c.vy * dt;
        const float m = 4.0f;
        if (c.x < -m) { c.x = -m; c.vx = -c.vx * EDGE_BOUNCE; }
        if (c.y < -m) { c.y = -m; c.vy = -c.vy * EDGE_BOUNCE; }
        if (c.x > s.winW + m) { c.x = float(s.winW + m); c.vx = -c.vx * EDGE_BOUNCE; }
        if (c.y > s.winH + m) { c.y = float(s.winH + m); c.vy = -c.vy * EDGE_BOUNCE; }
    }
    collide(s);
}

// Разбегание: случайное блуждание + слабый исход из центра надписи. Каждый шаг
// пишется в ленту, чтобы потом её отмотать.
void stepScatter(State& s, float dt) {
    const float cx = s.winW * 0.5f, cy = s.winH * 0.5f;
    for (size_t i = 0; i < s.cells.size(); ++i) {
        Cell& c = s.cells[i];
        const float dx = c.x - cx, dy = c.y - cy;
        const float d = std::sqrt(dx * dx + dy * dy) + 1.0f;
        // По вертикали трясём сильнее: надпись шире, чем выше, и при изотропном
        // блуждании два слова так и остаются лежать двумя полосами — цвет полос
        // выдаёт ответ ещё до сборки. Перемешиваем их по высоте.
        c.vx += (srand2(s.rnd) * 900.0f + dx / d * 150.0f) * dt;
        c.vy += (srand2(s.rnd) * 1500.0f + dy / d * 150.0f) * dt;
    }
    integrate(s, dt, false);

    s.tape.resize(size_t((s.tapeSteps + 1) * int(s.cells.size()) * 2));
    short* row = &s.tape[size_t(s.tapeSteps) * s.cells.size() * 2];
    for (size_t i = 0; i < s.cells.size(); ++i) {
        row[i * 2 + 0] = short(std::max(-3000.0f, std::min(3000.0f, s.cells[i].x)));
        row[i * 2 + 1] = short(std::max(-3000.0f, std::min(3000.0f, s.cells[i].y)));
    }
    ++s.tapeSteps;
}

// Готовит заставку: строит слово, МГНОВЕННО прокручивает всё разбегание в ленту
// (165 шагов x ~850 клеток — доли миллисекунды, ни одного кадра на экране) и
// ставит поле в последний кадр ленты. Игрок с первого мгновения видит хаос и
// может мешать сборке — сам разбег ему смотреть незачем.
void primeLogo(State& s) {
    buildLogo(s);
    for (int i = 0; i < SCATTER_STEPS; ++i) stepScatter(s, STEP);
    const short* row = &s.tape[size_t(s.tapeSteps - 1) * s.cells.size() * 2];
    for (size_t i = 0; i < s.cells.size(); ++i) {
        s.cells[i].x = float(row[i * 2 + 0]);
        s.cells[i].y = float(row[i * 2 + 1]);
        s.cells[i].vx = s.cells[i].vy = 0.0f;
        s.cells[i].freed = 0;
        s.cells[i].cur = float(s.tapeSteps - 1);
    }
    s.stubborn = 0.0f;
    s.pushed = 0;
    s.phase = PH_REWIND;
    s.phaseTime = 0.0f;
}

// Промотка. Клетка на ленте едет строго по своей записи, сбитая — своим ходом.
// Возвращает true, когда слово собрано целиком.
bool stepRewind(State& s, float dt) {
    applyCursor(s, dt);

    const size_t n = s.cells.size();
    bool onTape = false;
    // Пауза перед стартом отмотки: глаз должен успеть прочитать хаос как хаос
    // (и рука — дотянуться до мыши), иначе сборка начинается «из ниоткуда».
    if (s.phaseTime <= REWIND_LEAD) {
        onTape = true;
    } else {
        for (size_t i = 0; i < n; ++i) {
            Cell& c = s.cells[i];
            if (c.freed) continue;
            c.cur -= REWIND_RATE * c.rate;
            if (c.cur <= 0.0f) {
                c.cur = 0.0f;
                c.x = c.hx; c.y = c.hy;
                c.vx = c.vy = 0.0f;
                c.freed = 1;          // доехала: дальше живёт физикой, её можно сбить
                continue;
            }
            onTape = true;
            // Позиция ИНТЕРПОЛИРУЕТСЯ между двумя кадрами ленты. Целочисленный
            // индекс при дробной скорости отмотки давал рывки: часть шагов кадр
            // не менялся, потом клетка прыгала — движение читалось дёрганым.
            const int f0 = int(c.cur);
            const int f1 = std::min(s.tapeSteps - 1, f0 + 1);
            const float t = c.cur - float(f0);
            const short* a = &s.tape[size_t(f0) * n * 2];
            const short* b = &s.tape[size_t(f1) * n * 2];
            c.x = float(a[i * 2 + 0]) * (1.0f - t) + float(b[i * 2 + 0]) * t;
            c.y = float(a[i * 2 + 1]) * (1.0f - t) + float(b[i * 2 + 1]) * t;
            c.vx = c.vy = 0.0f;
        }
    }
    if (!onTape) s.stubborn += dt * 0.55f;   // мешать можно, победить — нет
    integrate(s, dt, true);

    // Потолок фазы: игрок, который бесконечно расстреливает надпись щелчками,
    // не должен запирать себя в заставке навсегда.
    if (s.phaseTime > REWIND_MAX) return true;
    if (onTape) return false;
    for (size_t i = 0; i < n; ++i) {
        const float dx = s.cells[i].hx - s.cells[i].x;
        const float dy = s.cells[i].hy - s.cells[i].y;
        if (dx * dx + dy * dy > 4.0f) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------

// Скопление рисуется ОРТОГРАФИЧЕСКОЙ проекцией: в макро-слое игры перспективы
// нет (§2.7), и фон оболочки читается той же геометрией, что звёздная карта.
// Глубина z даёт только размер и яркость — ближние звёзды крупнее и ярче.
void drawCluster(SDL_Renderer* r, const State& s) {
    if (s.clusterFade <= 0.0f) return;
    // Спектральные оттенки O..M, приглушённые до фоновых.
    static const SDL_Color tones[4] = {
        {150, 196, 255, 255},   // горячая сине-белая
        {214, 228, 238, 255},   // белая
        {245, 205, 150, 255},   // жёлто-оранжевая
        {236, 150, 120, 255}    // красная
    };
    const float cx = s.winW * 0.5f, cy = s.winH * 0.5f;
    const float edge = s.coreR * CLUSTER_EDGE_K;
    for (size_t i = 0; i < s.stars.size(); ++i) {
        const Star& st = s.stars[i];
        const int px = int(cx + st.x);
        const int py = int(cy + st.y);
        if (px < -4 || py < -4 || px > s.winW + 4 || py > s.winH + 4) continue;
        const float depth = 0.5f + 0.5f * std::max(-1.0f, std::min(1.0f, st.z / edge));
        // Яркость — от глубины (ближе = ярче) И от близости к ядру: иначе
        // скопление читается ровной пылью, а у настоящего виден плотный центр.
        const float rad = std::sqrt(st.x * st.x + st.y * st.y + st.z * st.z);
        const float core = 1.0f - std::min(1.0f, rad / (s.coreR * 2.2f));
        const float lum = 0.45f + 0.55f * depth;
        const SDL_Color t = tones[st.tone & 3];
        const int size = (depth > 0.70f && core > 0.25f) ? 3 : (depth > 0.42f ? 2 : 1);
        const Uint8 a = Uint8((16.0f + 60.0f * lum + 74.0f * core * lum) * s.clusterFade);
        SDL_SetRenderDrawColor(r, t.r, t.g, t.b, a);
        SDL_Rect q = { px, py, size, size };
        SDL_RenderFillRect(r, &q);
    }
}

void drawCells(SDL_Renderer* r, const State& s, float fade) {
    const int size = std::max(2, int(s.drawPx) - 1);
    for (size_t i = 0; i < s.cells.size(); ++i) {
        const Cell& c = s.cells[i];
        const float dx = c.hx - c.x, dy = c.hy - c.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        // Яркость — функция расстояния до слота: поле само «конденсируется».
        float lit = 1.0f - std::min(1.0f, dist / 190.0f);
        SDL_Color base = c.word == 1 ? P.amber : P.cyan;
        if (c.word == 2) { base = P.dim; lit = 0.22f; }
        const float a = (46.0f + 209.0f * lit) * fade;
        SDL_SetRenderDrawColor(r,
            Uint8(52 + (base.r - 52) * lit),
            Uint8(70 + (base.g - 70) * lit),
            Uint8(96 + (base.b - 96) * lit),
            Uint8(std::max(0.0f, std::min(255.0f, a))));
        const int sz = c.word == 2 ? std::max(2, size / 2) : size;
        SDL_Rect q = { int(c.x), int(c.y), sz, sz };
        SDL_RenderFillRect(r, &q);
    }
}

void centerText(SDL_Renderer* r, int cx, int y, const std::string& t, SDL_Color c, int scale) {
    // Ширину берём у UI::textWidth: она меряет уже переведённую строку и считает
    // символы, а не байты (кириллица в UTF-8 двухбайтовая).
    UI::drawText(r, cx - UI::textWidth(t, scale) / 2, y, t, c, scale);
}

std::string wrapPlain(const std::string& text, int maxChars) {
    std::string out, line, word;
    size_t lineLen = 0, wordLen = 0;
    for (size_t k = 0; k <= text.size(); ++k) {
        const char ch = k < text.size() ? text[k] : ' ';
        if (ch == ' ') {
            if (word.empty()) continue;
            if (line.empty()) { line = word; lineLen = wordLen; }
            else if (int(lineLen + 1 + wordLen) <= maxChars) { line += " " + word; lineLen += 1 + wordLen; }
            else { out += line; out += "\n"; line = word; lineLen = wordLen; }
            word.clear();
            wordLen = 0;
        } else {
            word += ch;
            // Хвостовые байты UTF-8 (10xxxxxx) не считаются за символ — иначе
            // русская подпись переносилась бы вдвое раньше нужного.
            if (((unsigned char)ch & 0xC0) != 0x80) ++wordLen;
        }
    }
    if (!line.empty()) out += line;
    return out;
}

// ---------------------------------------------------------------------------
// Комикс
// ---------------------------------------------------------------------------

const char* SLIDE_TEXT[COMIC_SLIDES] = {
    ("ONE GLOBULAR CLUSTER. TEN THOUSAND SUNS INSIDE A HUNDRED LIGHT YEARS. "
     "PACKED SO TIGHT THAT A CARGO RUN COSTS YEARS INSTEAD OF CENTURIES - "
     "WHICH IS THE ONLY REASON AN ECONOMY EXISTS HERE AT ALL."),

    ("NOTHING OUTRUNS LIGHT. HULLS CRAWL AT A TENTH OF IT. NEWS CRAWLS AT ALL "
     "OF IT. EVERY OWNER, EVERY PRICE ON YOUR MAP IS A MEMORY OF SOMETHING "
     "THAT WAS TRUE WHEN THE SIGNAL LEFT."),

    ("SO THE REAL CARGO IS KNOWLEDGE. WHAT IRON SELLS FOR TWELVE LIGHT YEARS "
     "AWAY IS WORTH MORE THAN THE IRON. NOBODY WILL HAND YOU THAT MAP. YOU BURN "
     "FUEL FOR EVERY LINE OF IT."),

    ("ONCE A MILLENNIUM THE BANKS RECONCILE THE WHOLE CLUSTER AND EVERY TRADING "
     "LICENCE IS AUDITED. MEET YOUR QUOTA OR THE TERMINALS GO DARK - FOR YOU, "
     "AND ONLY FOR YOU."),

    ("ONE HULL. ONE LICENCE. ONE HUNDRED CREDITS. THE CLUSTER DOES NOT KNOW "
     "YOUR NAME YET.")
};

void loadSlides(SDL_Renderer* r, State& s) {
    if (s.slidesLoaded) return;
    s.slidesLoaded = true;
    for (int i = 0; i < COMIC_SLIDES; ++i) {
        char rel[64];
        std::snprintf(rel, sizeof(rel), "comic/slide%d.png", i + 1);
        int w = 0, h = 0, ch = 0;
        unsigned char* data = stbi_load(assetPath(rel).c_str(), &w, &h, &ch, 4);
        if (!data) continue;
        SDL_Surface* surf = SDL_CreateRGBSurfaceFrom((void*)data, w, h, 32, w * 4,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff
#else
            0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
#endif
        );
        if (surf) {
            s.slideTex[i] = SDL_CreateTextureFromSurface(r, surf);
            SDL_FreeSurface(surf);
        }
        stbi_image_free(data);
    }
}

void drawComic(SDL_Renderer* r, State& s) {
    const int margin = std::max(24, s.winW / 14);
    const int capH = 132;             // место под подпись под кадром
    const int artX = margin;
    const int artY = int(s.winH * 0.10f);
    const int artW = s.winW - margin * 2;
    const int frameH = std::max(140, s.winH - artY - capH - 92);

    UI::fillRect(r, artX, artY, artW, frameH, SDL_Color{6, 9, 18, 235});
    UI::strokeRect(r, artX, artY, artW, frameH, P.border);

    SDL_Texture* tex = s.slideTex[s.slide];
    if (tex) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
        const float scale = std::min(float(artW - 8) / float(std::max(1, tw)),
                                     float(frameH - 8) / float(std::max(1, th)));
        SDL_Rect dst;
        dst.w = int(tw * scale);
        dst.h = int(th * scale);
        dst.x = artX + (artW - dst.w) / 2;
        dst.y = artY + (frameH - dst.h) / 2;
        SDL_RenderCopy(r, tex, NULL, &dst);
    } else {
        // Кадр ещё не нарисован — рамка остаётся честно пустой, но подписанной.
        centerText(r, s.winW / 2, artY + frameH / 2 - 8, "PANEL PENDING", P.dim, 2);
    }

    // Подпись печатается посимвольно — та же эстетика, что у новеллы Тимертии.
    // Переводим ДО машинки: посимвольная обрезка английского оригинала не дала
    // бы словарю совпасть ни разу (он ищет целые слова и обороты).
    const std::string full = I18N::tr(SLIDE_TEXT[s.slide]);
    const size_t shown = std::min(UI::textLength(full), size_t(s.slideText));
    const int capY = artY + frameH + 18;
    const int maxChars = std::max(20, (s.winW - margin * 2 - 20) / 12);
    UI::drawText(r, artX + 4, capY, wrapPlain(UI::textPrefix(full, shown), maxChars), P.text, 2);

    // Счётчик кадров
    for (int i = 0; i < COMIC_SLIDES; ++i) {
        const SDL_Color c = i == s.slide ? P.cyan : P.dim;
        UI::fillRect(r, s.winW / 2 - COMIC_SLIDES * 9 + i * 18, s.winH - 42, 12, 4, c);
    }
    const bool typing = shown < UI::textLength(full);
    if ((SDL_GetTicks() / 420) % 2 == 0 || typing) {
        centerText(r, s.winW / 2, s.winH - 30,
                   typing ? "CLICK TO REVEAL" : "CLICK TO CONTINUE", P.dim, 1);
    }
    UI::drawText(r, margin, s.winH - 30, "ESC SKIP", P.dim, 1);
}

// ---------------------------------------------------------------------------
// Экран генерации мира
// ---------------------------------------------------------------------------

void drawProgress(SDL_Renderer* r, const State& s, int y, size_t starCount) {
    char line[96];
    std::snprintf(line, sizeof(line), "GENERATING CLUSTER - %d SYSTEMS", int(starCount));
    centerText(r, s.winW / 2, y, line, P.cyan, 2);

    // Честный индикатор: полоса неопределённая (бегущий сканер), рядом реально
    // прошедшее время. Врать процентами, которых мы не измеряем, не будем.
    const int w = std::min(520, s.winW - 120);
    const int x = s.winW / 2 - w / 2;
    UI::fillRect(r, x, y + 34, w, 10, SDL_Color{8, 12, 22, 230});
    UI::strokeRect(r, x, y + 34, w, 10, P.border);
    const float t = std::fmod(s.workElapsed * 0.55f, 1.0f);
    const int bw = w / 5;
    const int bx = x + int((w + bw) * t) - bw;
    for (int i = 0; i < bw; ++i) {
        const int px = bx + i;
        if (px <= x || px >= x + w) continue;
        const float k = std::sin(float(i) / bw * 3.14159f);
        UI::fillRect(r, px, y + 35, 1, 8, SDL_Color{82, 222, 246, Uint8(40 + 180 * k)});
    }
    std::snprintf(line, sizeof(line), "%.1FS", double(s.workElapsed));
    centerText(r, s.winW / 2, y + 54, line, P.dim, 1);
}

}  // namespace

void pumpMusic(MusicState& state, const std::vector<Mix_Music*>& playlist, bool soundOn) {
    if (!soundOn || playlist.empty() || Mix_PlayingMusic()) return;
    if (state.seed == 0) {
        // Живое зерно на запуск. Раньше здесь стояла константа 0x51A7C0DE — и
        // порядок треков был один и тот же в каждой партии.
        state.seed = (unsigned int)(SDL_GetPerformanceCounter() & 0xFFFFFFFFu) | 1u;
    }
    if (state.next >= state.order.size()) {
        state.order.clear();
        for (size_t i = 0; i < playlist.size(); ++i) state.order.push_back(int(i));
        std::mt19937 rnd(state.seed);
        state.seed = rnd();                       // следующая перестановка будет другой
        for (size_t i = state.order.size(); i > 1; --i) {
            const size_t j = std::uniform_int_distribution<size_t>(0, i - 1)(rnd);
            std::swap(state.order[i - 1], state.order[j]);
        }
        // Стык двух перестановок не должен повторить один трек дважды подряд.
        if (state.order.size() > 1 && state.order[0] == state.lastPlayed) {
            std::swap(state.order[0], state.order[state.order.size() - 1]);
        }
        state.next = 0;
    }
    const int idx = state.order[state.next++];
    state.lastPlayed = idx;
    Mix_PlayMusic(playlist[idx], 1);
}

// ---------------------------------------------------------------------------
// Главный цикл оболочки
// ---------------------------------------------------------------------------

int run(SDL_Window* window, SDL_Renderer* renderer, Game& game, size_t starCount,
        unsigned int worldSeed, const std::string& savePath, bool& soundOn,
        const std::vector<Mix_Music*>& playlist, MusicState& music, bool autopilot) {
    State s;
    int autoTick = 0;
    float autoElapsed = 0.0f;
    s.rnd.seed(0x7E4E71u);      // своё зерно: симуляционный rng не трогаем
    SDL_GetWindowSize(window, &s.winW, &s.winH);
    primeLogo(s);
    buildCluster(s);

    if (FILE* f = std::fopen(savePath.c_str(), "rb")) { std::fclose(f); s.hasSave = true; }
    if (!s.hasSave) s.menuIndex = 0;

    Mix_VolumeMusic(soundOn ? MIX_MAX_VOLUME : 0);

    float accum = 0.0f;
    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    bool running = true;

    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float realDt = float(double(now - last) / freq);
        last = now;
        if (realDt > 0.25f) realDt = 0.25f;

        if (!autopilot) pumpMusic(music, playlist, soundOn);

        // ------------------------------- события -------------------------------
        SDL_Event e;
        bool advance = false;    // «дальше» (клик или клавиша)
        bool keyAdvance = false; // именно клавиша: только ею пропускают заставку
        bool skipAll = false;
        bool clicked = false;    // именно клик мышью (в меню он должен попасть в пункт)
        bool moved = false;      // мышь двигалась — только тогда наведение меняет выбор
        float relX = 0.0f, relY = 0.0f;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { s.quitRequested = true; }
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                SDL_GetWindowSize(window, &s.winW, &s.winH);
                buildCluster(s);
                if (s.phase == PH_REWIND) {
                    // Лента привязана к прежней раскладке — она больше не годится.
                    primeLogo(s);
                } else if (s.phase == PH_MENU || s.phase == PH_HOLD) {
                    retargetTitle(s);
                } else {
                    retargetField(s);
                }
            }
            else if (e.type == SDL_MOUSEMOTION) {
                s.mx = float(e.motion.x);
                s.my = float(e.motion.y);
                relX += float(e.motion.xrel);
                relY += float(e.motion.yrel);
                moved = true;
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                advance = true;
                clicked = true;
                s.lmbHeld = true;
                applyPunch(s, float(e.button.x), float(e.button.y));
                if (s.clusterFade > 0.0f) punchCluster(s, float(e.button.x), float(e.button.y));
            }
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                s.lmbHeld = false;
            }
            else if (e.type == SDL_KEYDOWN) {
                const SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) skipAll = true;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) {
                    advance = true;
                    keyAdvance = true;
                }
                if (s.phase == PH_MENU) {
                    if (k == SDLK_UP || k == SDLK_w) s.menuIndex = (s.menuIndex + MENU_ITEMS - 1) % MENU_ITEMS;
                    if (k == SDLK_DOWN || k == SDLK_s) s.menuIndex = (s.menuIndex + 1) % MENU_ITEMS;
                }
            }
        }

        // Скорость курсора в НАСТОЯЩИХ px/с: смещение за кадр, делённое на его
        // длительность. Раньше здесь копились «пиксели за кадр», и сила толчка
        // зависела от частоты кадров, а не от того, как быстро игрок ведёт мышь.
        if (moved) {
            const float invDt = 1.0f / std::max(realDt, 1.0f / 240.0f);
            s.mvx = s.mvx * 0.35f + relX * invDt * 0.65f;
            s.mvy = s.mvy * 0.35f + relY * invDt * 0.65f;
        } else {
            s.mvx *= 0.45f;
            s.mvy *= 0.45f;
        }

        // Автопилот: сам жмёт «дальше» и водит курсором по полю клеток (чтобы
        // под санитайзером отработала и толкучка), с жёстким потолком времени.
        if (autopilot) {
            autoElapsed += realDt;
            // Первые 7 секунд не жмём ничего: пусть заставка отработает целиком
            // (разбегание -> промотка -> сборка), иначе она выпадет из покрытия.
            if (++autoTick % 30 == 0 && autoElapsed > 7.0f) { advance = true; keyAdvance = true; }
            s.mx = float((autoTick * 17) % std::max(1, s.winW));
            s.my = float((autoTick * 11) % std::max(1, s.winH));
            s.mvx = 2600.0f; s.mvy = -2100.0f;
            s.lmbHeld = (autoTick % 40) < 12;
            if (autoTick % 25 == 0) { applyPunch(s, s.mx, s.my); if (s.clusterFade > 0.0f) punchCluster(s, s.mx, s.my); }
            if (autoElapsed > 120.0f) { s.quitRequested = true; }
        }

        // ------------------------------- шаги физики ---------------------------
        accum += realDt;
        int steps = 0;
        while (accum >= STEP && steps < 4) {
            accum -= STEP;
            ++steps;
            s.phaseTime += STEP;
            s.drawPx += (s.targetPx - s.drawPx) * 0.06f;

            // Скопление проявляется вместе с меню (за ~0.9 с) и до него не
            // считается вовсе: на заставке фона нет.
            if (s.phase >= PH_MENU) s.clusterFade = std::min(1.0f, s.clusterFade + STEP / 0.9f);
            if (s.clusterFade > 0.0f) stepCluster(s, STEP);

            switch (s.phase) {
            case PH_REWIND:
                if (stepRewind(s, STEP)) { s.phase = PH_HOLD; s.phaseTime = 0.0f; }
                break;
            case PH_HOLD:
                applyCursor(s, STEP);
                integrate(s, STEP, true);
                if (s.phaseTime > HOLD_SECONDS) {
                    retargetTitle(s);
                    s.stubborn = 0.35f;
                    s.phase = PH_MENU;
                    s.phaseTime = 0.0f;
                }
                break;
            default:
                applyCursor(s, STEP);
                integrate(s, STEP, true);
                break;
            }
        }
        if (accum > 0.5f) accum = 0.0f;

        // ------------------------------- логика фаз ----------------------------
        if (s.phase == PH_REWIND || s.phase == PH_HOLD) {
            // Пропуск — только клавишей. ЛКМ на заставке занята: это толчок.
            if (keyAdvance || skipAll) {
                // Пропуск заставки: слово сразу на месте, дальше меню.
                for (size_t i = 0; i < s.cells.size(); ++i) {
                    s.cells[i].x = s.cells[i].hx;
                    s.cells[i].y = s.cells[i].hy;
                    s.cells[i].vx = s.cells[i].vy = 0.0f;
                }
                retargetTitle(s);
                s.stubborn = 0.35f;
                s.phase = PH_MENU;
                s.phaseTime = 0.0f;
            }
        } else if (s.phase == PH_MENU) {
            // Наведение выбирает пункт, но только когда мышь ДВИГАЛАСЬ: иначе
            // застывший над пунктом курсор блокировал бы стрелки.
            bool onItem = false;
            for (size_t i = 0; i < s.menuRects.size(); ++i) {
                const SDL_Rect& q = s.menuRects[i];
                if (s.mx >= q.x && s.mx < q.x + q.w && s.my >= q.y && s.my < q.y + q.h) {
                    if (moved) s.menuIndex = int(i);
                    onItem = true;
                }
            }
            // Клик мимо пунктов ничего не выбирает — им играют с полем клеток.
            if (clicked && !onItem) advance = false;
            if (advance) {
                if (s.menuIndex == 0 || (s.menuIndex == 1 && s.hasSave)) {
                    const bool load = s.menuIndex == 1;
                    s.outcome = load ? OUTCOME_LOAD_GAME : OUTCOME_NEW_GAME;
                    game.seed = worldSeed;
                    s.workerDone.store(false);
                    s.workerOk.store(false);
                    s.workerRunning = true;
                    s.workElapsed = 0.0f;
                    // Генерация/загрузка мира — в фоновом потоке. Главный поток
                    // в это время рисует комикс и НЕ читает `game` (правило
                    // владения: до join() мир принадлежит рабочему потоку).
                    Game* gp = &game;
                    const std::string path = savePath;
                    std::atomic<bool>* doneFlag = &s.workerDone;
                    std::atomic<bool>* okFlag = &s.workerOk;
                    const size_t n = starCount;
                    s.worker = std::thread([gp, load, path, n, doneFlag, okFlag]() {
                        bool ok = true;
                        if (load) {
                            ok = gp->loadFromFile(path);
                            if (!ok) gp->init(n);
                        } else {
                            gp->init(n);
                        }
                        okFlag->store(ok);
                        doneFlag->store(true);
                    });
                    retargetField(s);
                    s.phase = load ? PH_LOADING : PH_COMIC;
                    s.phaseTime = 0.0f;
                    s.slide = 0;
                    s.slideText = 0.0f;
                    loadSlides(renderer, s);
                } else if (s.menuIndex == 2) {
                    // Переключатель языка. Выбор переживает запуск — лежит
                    // рядом с сейвом (см. I18N::savePreference в main.cpp).
                    I18N::setLang(I18N::lang() == I18N::LANG_RU ? I18N::LANG_EN : I18N::LANG_RU);
                    I18N::savePreference(savePath + ".lang");
                } else if (s.menuIndex == 3) {
                    soundOn = !soundOn;
                    Mix_VolumeMusic(soundOn ? MIX_MAX_VOLUME : 0);
                    if (!soundOn) Mix_HaltMusic();
                } else if (s.menuIndex == 4) {
                    s.quitRequested = true;
                }
            }
        } else if (s.phase == PH_COMIC) {
            s.slideText += realDt * 78.0f;
            const size_t full = UI::textLength(I18N::tr(SLIDE_TEXT[s.slide]));
            if (skipAll) {
                s.phase = PH_LOADING;
                s.phaseTime = 0.0f;
            } else if (advance) {
                if (size_t(s.slideText) < full) {
                    s.slideText = float(full);
                } else if (s.slide + 1 < COMIC_SLIDES) {
                    ++s.slide;
                    s.slideText = 0.0f;
                } else {
                    s.phase = PH_LOADING;
                    s.phaseTime = 0.0f;
                }
            }
        } else if (s.phase == PH_LOADING) {
            if (s.workerDone.load()) {
                if (s.worker.joinable()) s.worker.join();
                s.workerRunning = false;
                s.phase = PH_READY;
                s.phaseTime = 0.0f;
            }
        } else if (s.phase == PH_READY) {
            if (advance || skipAll) { s.phase = PH_DONE; running = false; }
        }

        if (s.workerRunning) s.workElapsed += realDt;

        // Закрытие окна: мир может ещё генериться в фоне — дожидаемся, иначе
        // поток переживёт свой `Game` и это неопределённое поведение.
        if (s.quitRequested) {
            s.outcome = OUTCOME_QUIT;
            if (!s.workerRunning) running = false;
            else if (s.workerDone.load()) {
                if (s.worker.joinable()) s.worker.join();
                s.workerRunning = false;
                running = false;
            }
        }

        // ------------------------------- отрисовка -----------------------------
        SDL_SetRenderDrawColor(renderer, 3, 5, 14, 255);
        SDL_RenderClear(renderer);
        drawCluster(renderer, s);

        float fade = 1.0f;
        if (s.phase == PH_COMIC || s.phase == PH_READY) fade = 0.45f;
        drawCells(renderer, s, fade);

        if (s.phase == PH_HOLD && s.pushed > 30) {
            // Расплата за игру с логотипом: клетки всё равно вернулись.
            char line[96];
            std::snprintf(line, sizeof(line), "%d CELLS SCATTERED - ALL OF THEM CAME BACK", s.pushed);
            centerText(renderer, s.winW / 2, int(s.winH * 0.78f), line, P.dim, 1);
        }
        if (s.phase == PH_MENU) {
            // Отступ считаем от размера клетки ЗАГОЛОВКА, а не от текущего
            // (drawPx ещё едет к нему после пересборки — подпись налезала бы).
            centerText(renderer, s.winW / 2, int(s.winH * 0.13f) + 7 * SUPER * s.titlePx + 18,
                       "A TENEVIK GAMES SIMULATION", P.dim, 1);
            const char* labels[MENU_ITEMS] = { "NEW GAME", "LOAD GAME", "LANGUAGE", "SOUND", "EXIT" };
            s.menuRects.clear();
            const int itemH = 40;
            const int top = int(s.winH * 0.50f);
            for (int i = 0; i < MENU_ITEMS; ++i) {
                std::string label = labels[i];
                // Язык всегда подписан обеими метками: игрок, открывший меню на
                // чужом языке, всё равно видит, куда жать.
                if (i == 2) label += I18N::lang() == I18N::LANG_RU ? ": RUS / ENG" : ": ENG / RUS";
                if (i == 3) label += soundOn ? " ON" : " OFF";
                const bool enabled = !(i == 1 && !s.hasSave);
                const bool sel = i == s.menuIndex;
                const int w = UI::textWidth(label, 2) + 80;
                SDL_Rect q = { s.winW / 2 - w / 2, top + i * itemH, w, itemH - 8 };
                s.menuRects.push_back(q);
                if (sel && enabled) {
                    UI::fillRect(renderer, q.x, q.y, q.w, q.h, SDL_Color{16, 30, 52, 210});
                    UI::strokeRect(renderer, q.x, q.y, q.w, q.h, P.cyan);
                }
                const SDL_Color c = !enabled ? SDL_Color{70, 82, 100, 255}
                                             : (sel ? P.text : P.dim);
                centerText(renderer, s.winW / 2, q.y + (q.h - 14) / 2, label, c, 2);
            }
            char foot[96];
            std::snprintf(foot, sizeof(foot), "SEED %u   -   F1 SHOWS CONTROLS IN GAME", worldSeed);
            centerText(renderer, s.winW / 2, s.winH - 28, foot, P.dim, 1);
        } else if (s.phase == PH_COMIC) {
            drawComic(renderer, s);
            if (!s.workerDone.load()) {
                UI::drawText(renderer, s.winW - 210, s.winH - 30, "CLUSTER FORMING...", P.dim, 1);
            }
        } else if (s.phase == PH_LOADING) {
            drawProgress(renderer, s, int(s.winH * 0.46f), starCount);
        } else if (s.phase == PH_READY) {
            UI::drawControlsCard(renderer, s.winW, s.winH);
            if ((SDL_GetTicks() / 420) % 2 == 0) {
                centerText(renderer, s.winW / 2, s.winH - 34,
                           s.outcome == OUTCOME_LOAD_GAME ? "PRESS ENTER TO RESUME"
                                                          : "PRESS ENTER TO BEGIN", P.cyan, 2);
            }
        }

        SDL_RenderPresent(renderer);
        const Uint64 frameEnd = SDL_GetPerformanceCounter();
        const double elapsed = double(frameEnd - now) / freq;
        if (elapsed < 1.0 / 100.0) SDL_Delay(Uint32((1.0 / 100.0 - elapsed) * 1000.0));
    }

    if (s.worker.joinable()) s.worker.join();
    for (int i = 0; i < COMIC_SLIDES; ++i) {
        if (s.slideTex[i]) SDL_DestroyTexture(s.slideTex[i]);
    }
    // Загрузка сейва могла провалиться — тогда рабочий поток уже поднял свежий
    // мир, и честнее начать новую партию, чем делать вид, что сейв применился.
    if (s.outcome == OUTCOME_LOAD_GAME && !s.workerOk.load()) s.outcome = OUTCOME_NEW_GAME;
    return s.outcome;
}

}
