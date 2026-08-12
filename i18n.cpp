#include "i18n.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace I18N {

namespace {

Lang gLang = LANG_EN;

struct Entry {
    const char* en;
    const char* ru;
};

// ---------------------------------------------------------------------------
// 1. ТОЧНЫЕ СТРОКИ
//
// Сюда идёт то, что словами не собирается: предложения со знаками препинания и
// форматы, которые переводятся ДО snprintf (диалоги Тимертии, подписи комикса).
// Ключ — строка целиком, как она написана в коде.
// ---------------------------------------------------------------------------
const Entry EXACT[] = {
    // --- пролог-комикс (shell.cpp) ---
    {"ONE GLOBULAR CLUSTER. TEN THOUSAND SUNS INSIDE A HUNDRED LIGHT YEARS. "
     "PACKED SO TIGHT THAT A CARGO RUN COSTS YEARS INSTEAD OF CENTURIES - "
     "WHICH IS THE ONLY REASON AN ECONOMY EXISTS HERE AT ALL.",
     "ОДНО ШАРОВОЕ СКОПЛЕНИЕ. ДЕСЯТЬ ТЫСЯЧ СОЛНЦ В СТА СВЕТОВЫХ ГОДАХ. "
     "НАБИТО ТАК ПЛОТНО, ЧТО РЕЙС С ГРУЗОМ СТОИТ ЛЕТ, А НЕ ВЕКОВ - "
     "ТОЛЬКО ПОЭТОМУ ЗДЕСЬ ВООБЩЕ ВОЗМОЖНА ЭКОНОМИКА."},

    {"NOTHING OUTRUNS LIGHT. HULLS CRAWL AT A TENTH OF IT. NEWS CRAWLS AT ALL "
     "OF IT. EVERY OWNER, EVERY PRICE ON YOUR MAP IS A MEMORY OF SOMETHING "
     "THAT WAS TRUE WHEN THE SIGNAL LEFT.",
     "СВЕТ НЕ ОБОГНАТЬ. КОРПУСА ПОЛЗУТ НА ДЕСЯТОЙ ЕГО ДОЛЕ. НОВОСТИ ИДУТ РОВНО "
     "СО СКОРОСТЬЮ СВЕТА. КАЖДЫЙ ВЛАДЕЛЕЦ, КАЖДАЯ ЦЕНА НА ТВОЕЙ КАРТЕ - ЭТО "
     "ПАМЯТЬ О ТОМ, ЧТО БЫЛО ПРАВДОЙ В МОМЕНТ УХОДА СИГНАЛА."},

    {"SO THE REAL CARGO IS KNOWLEDGE. WHAT IRON SELLS FOR TWELVE LIGHT YEARS "
     "AWAY IS WORTH MORE THAN THE IRON. NOBODY WILL HAND YOU THAT MAP. YOU BURN "
     "FUEL FOR EVERY LINE OF IT.",
     "ЗНАЧИТ, НАСТОЯЩИЙ ГРУЗ - ЭТО ЗНАНИЕ. ПОЧЁМ ИДЁТ ЖЕЛЕЗО ЗА ДВЕНАДЦАТЬ "
     "СВЕТОВЫХ ЛЕТ ОТСЮДА - ДОРОЖЕ САМОГО ЖЕЛЕЗА. ЭТУ КАРТУ НИКТО НЕ ПОДАРИТ. "
     "КАЖДУЮ ЕЁ СТРОКУ ТЫ ВЫЖИГАЕШЬ ТОПЛИВОМ."},

    {"ONCE A MILLENNIUM THE BANKS RECONCILE THE WHOLE CLUSTER AND EVERY TRADING "
     "LICENCE IS AUDITED. MEET YOUR QUOTA OR THE TERMINALS GO DARK - FOR YOU, "
     "AND ONLY FOR YOU.",
     "РАЗ В ТЫСЯЧЕЛЕТИЕ БАНКИ СВОДЯТ БАЛАНС ВСЕГО СКОПЛЕНИЯ, И КАЖДУЮ ТОРГОВУЮ "
     "ЛИЦЕНЗИЮ ПРОВЕРЯЮТ. ВЫПОЛНИ КВОТУ - ИЛИ ТЕРМИНАЛЫ ПОГАСНУТ. ДЛЯ ТЕБЯ. "
     "И ТОЛЬКО ДЛЯ ТЕБЯ."},

    {"ONE HULL. ONE LICENCE. ONE HUNDRED CREDITS. THE CLUSTER DOES NOT KNOW "
     "YOUR NAME YET.",
     "ОДИН КОРПУС. ОДНА ЛИЦЕНЗИЯ. СТО КРЕДИТОВ. СКОПЛЕНИЕ ПОКА НЕ ЗНАЕТ "
     "ТВОЕГО ИМЕНИ."},

    {"A TENEVIK GAMES SIMULATION", "СИМУЛЯЦИЯ ОТ TENEVIK GAMES"},
    {"%d CELLS SCATTERED - ALL OF THEM CAME BACK",
     "РАЗБРОСАНО КЛЕТОК: %d - И ВСЕ ВЕРНУЛИСЬ"},
    {"SEED %u   -   F1 SHOWS CONTROLS IN GAME",
     "ЗЕРНО %u   -   F1 ПОКАЖЕТ УПРАВЛЕНИЕ В ИГРЕ"},
    {"GENERATING CLUSTER - %d SYSTEMS", "СОБИРАЕМ СКОПЛЕНИЕ - %d СИСТЕМ"},

    // --- обучающая новелла Тимертии (ui.cpp) ---
    {"Master, I am Timertia - your AI core Agent.",
     "Хозяин, я Тимертия - твой ИИ-агент на кристалле."},
    {"Congratulations on obtaining your trading licence!",
     "Поздравляю с получением торговой лицензии!"},
    {"You can view your balance here.", "Твой баланс - вот здесь."},
    {"The tanks are dry, Master. Nothing can be planned from here until we fill them - the HOLD window buys matter cheaper than the station does.",
     "Баки сухие, хозяин. Отсюда ничего не спланировать, пока их не зальют, - в окне ТРЮМ вещество выходит дешевле, чем на станции."},
    {"The licence is a lease, not a gift. The banks audit it every period: pay the QUOTA out of your tariff, or settle the rest in cash at the brokerage. Let it lapse and the terminals go dark.",
     "Лицензия - это аренда, а не подарок. Раз в период банки её проверяют: плати КВОТУ из тарифа или доплати остаток на бирже. Прозеваешь - терминалы погаснут."},
    {"You own 1 space ship unit for now.", "Пока у тебя один корабль."},
    {"My subagents will monitor its system states here.",
     "Мои субагенты будут следить за его системами здесь."},
    {"Your vessel is currently at %s. You can access a model of local star system here.",
     "Сейчас твой корабль в системе %s. Модель местной звёздной системы открывается здесь."},
    {"With your trading licence you can perform HIGH-FREQUENCY BROKERAGE on local market.",
     "С торговой лицензией тебе доступна ВЫСОКОЧАСТОТНАЯ БИРЖЕВАЯ ТОРГОВЛЯ на местном рынке."},
    {"A periodic table based on standard supersymmetrical model is common CONVENTION of interstellar market.",
     "Таблица элементов по стандартной суперсимметричной модели - общая КОНВЕНЦИЯ межзвёздного рынка."},
    {"NASH EQUILIBRIUM proves it is best to buy on supply and sell on demand.",
     "РАВНОВЕСИЕ НЭША доказывает: покупай там, где предложение, продавай там, где спрос."},
    // Совет обучения. Числа в реплике настоящие: цена, отношение добычи к
    // потреблению, дальность, годы, прибыль — всё из `Game::playerBestRun`.
    {"The local model says: take %s. This port digs up %.1f times more of it than it burns, so it goes for a mere %.2f Cr.",
     "Местная модель говорит: берите %s. Этот порт добывает его в %.1f раза больше, чем сжигает, оттого он и стоит всего %.2f Cr."},
    {"The local model says: take %s - here it goes for a mere %.2f Cr.",
     "Местная модель говорит: берите %s - здесь он стоит всего %.2f Cr."},
    {"The local model finds nothing here worth a hold, Master. That happens: a port can be poor. Read the prices yourself - what is cheap here is what you load.",
     "Местная модель не находит здесь ничего, достойного трюма, хозяин. Так бывает: порт беден. Читайте цены сами - что здесь дёшево, то и грузят."},
    {"In %s they pay %.2f for it - %.1f ly, %.0f years under way, about %.0f Cr of profit. Remember that name, Master: %s!",
    // ⚠️ Годы пишутся буквой Y, как везде в интерфейсе, а не словом: русское
    // числительное требует согласования («24 года», но «25 лет»), а число сюда
    // приходит из расчёта. «%.1f св.года» безопасно — там всегда дробь.
     "В системе %s за него дают %.2f - это %.1f св.года, %.0f Y пути и около %.0f Cr прибыли. Запомните имя, хозяин: %s!"},
    {"Where to sell is a question the ledger answers, Master: press E for the exchange. It weighs every market we have seen - and so far we have seen only this one.",
     "Где сбыть - на это отвечает сводка, хозяин: биржа на E. Она взвешивает все рынки, что мы видели, - а видели мы пока только этот."},
    // Топливный блок (§12): термины взяты ровно те, что подписаны в окне
    // ТРЮМ/БАКИ — БУНКЕР, ТРЮМ, БАК, ТЯГА, КРЕЙСЕР, ОПТИМУМ. Иначе игрок
    // прочитает объяснение и не найдёт в окне того, о чём ему рассказали.
    {"Nothing crosses the void for free, though. This is the hold: BUNKER on the left, CARGO in the middle, TANK on the right.",
     "Только пустота даром никого не пропускает. Вот твои ёмкости: слева БУНКЕР, посередине ТРЮМ, справа БАК."},
    {"Fuel in the bunker is what BURNS. Propellant in the tank is what is THROWN. The drive spends the energy of the fuel to hurl the propellant astern - and only thrown mass moves a hull.",
     "Топливо в бункере - то, что ГОРИТ. Рабочее тело в баке - то, что ВЫБРАСЫВАЕТСЯ. Двигатель тратит энергию топлива, чтобы швырнуть рабочее тело назад: корпус двигает только выброшенная масса."},
    {"In port the short answer is the BUY FUEL+PROP button on the market window: I will choose sane elements and fill both for you.",
     "В порту короткий путь - кнопка КУПИТЬ ТОПЛ+РАБ.ТЕЛО в окне рынка: я сама подберу разумные элементы и залью обе ёмкости."},
    {"The long answer is these arrows. Buy any element into the cargo, then pour it left into the bunker or right into the tank. Light elements throw best, energetic ones burn best.",
     "Длинный путь - вот эти стрелки. Купи любой элемент в трюм, а потом перелей его влево в бункер или вправо в бак. Лёгкие элементы лучше бросать, энергичные - жечь."},
    {"THROTTLE decides what you spend: to the left the drive throws more propellant and spares fuel, to the right it burns fuel hard and spares propellant. CRUISE decides your speed, and speed is paid for out of both.",
     "Ручка ТЯГА решает, чем ты платишь: влево - двигатель бросает больше рабочего тела и бережёт топливо, вправо - жжёт топливо и бережёт рабочее тело. Ручка КРЕЙСЕР задаёт скорость, а за скорость платят обе ёмкости."},
    {"Name a destination, then press OPTIMAL and I will set both knobs at local prices. Once you are under way they LOCK: a route is costed by the engine you left port with.",
     "Назначь цель и нажми ОПТИМУМ - я выставлю обе ручки по здешним ценам. С уходом в рейс они ЗАПИРАЮТСЯ: маршрут считается по тому двигателю, с которым ты покинул порт."},
    {"Read the tanks before every hop, Master. A hull out of propellant does not drift home, it simply drifts - and if the route is beyond your tanks, the terminal will refuse to plot it at all.",
     "Смотри на ёмкости перед каждым прыжком, хозяин. Корпус без рабочего тела не дрейфует домой - он просто дрейфует. А если маршрут не по бакам, терминал откажется его прокладывать."},

    // Заказы и репутация (§23, §24).
    {"Ports also post JOBS: a cargo, a destination, a deadline. Deliver and your name grows; miss the date and it shrinks.",
     "Ещё порты вывешивают ЗАКАЗЫ: груз, точка назначения, срок. Довезёшь - имя растёт, просрочишь - падает."},
    {"Weigh every job in credits per year of flight against what a free run would earn on the same road. Nothing else about it matters.",
     "Меряй любой заказ в кредитах за год полёта и сравнивай со свободным рейсом по той же дороге. Больше в нём ничего не важно."},
    {"Your name is the ceiling of everything: better standing means heavier jobs offered and more hulls allowed in your fleet.",
     "Имя - потолок всего: чем выше репутация, тем крупнее предлагают заказы и тем больше корпусов разрешено держать во флоте."},

    {"Press L to fall into the system itself. There you fly the hull by hand, mine rock with M and dock with K - the belts are where cheap matter comes from.",
     "Нажми L, чтобы провалиться в саму систему. Там корпус ведёшь руками, камень бурится на M, стыковка на K - дешёвое вещество берут в поясах."},
    {"When the vault allows it, press C and buy a system outright. It pays you rent, berths your fleet, and takes any name you care to write on it.",
     "Когда касса позволит, нажми C и купи систему целиком. Она платит ренту, даёт стоянку флоту и носит любое имя, какое ты ей напишешь."},
    {"Finally, the new technology of applied color superconductivity has produced novel AI cores.",
     "И последнее: новая технология цветовой сверхпроводимости дала ИИ-кристаллы нового типа."},
    {"They are still prototypes and very rare. Be sure to privatise every one you find.",
     "Это пока прототипы, и они очень редки. Приватизируй каждый, который найдёшь."},
    {"By the way, you can also upgrade your vessel and purchase more trading licenses.",
     "Кстати, корабль можно улучшать, а лицензии - докупать."},
    {"F1 lists every control. I am at your service with more insights at any time, Master. [V]",
     "F1 покажет всё управление. Я всегда на связи и готова подсказать ещё, хозяин. [V]"},
    // Сводка по прибытии (шаг 100). Считается ТОЛЬКО по разведанным рынкам,
    // поэтому у неё три исхода: рейс есть, рынок пока один, и «видели много,
    // а везти отсюда нечего».
    {"A market report, Master: %s goes for %.2f here and fetches %.2f in %s - %.1f ly, %.0f years, some %.0f Cr. Best of everything we have seen.",
    // ⚠️ Порядок %s и чисел обязан совпадать с английским ключом: snprintf берёт
    // аргументы по счёту, а не по смыслу. Перестановка «%s ... %.2f» на
    // «%.2f ... %s» здесь читала double как указатель и валила игру.
     "Сводку по рынку, хозяин: %s здесь идёт по %.2f, а берут по %.2f в системе %s - это %.1f св.года, %.0f Y и около %.0f Cr. Лучшее из всего, что мы видели."},
    {"No report yet, Master: this is the only market we have seen, and there is nothing to weigh it against. Fly, and I will keep the ledger.",
     "Сводки пока нет, хозяин: этот рынок у нас единственный, и взвесить его не с чем. Летите, а счёт я поведу."},
    {"No report yet, Master: this is the only market we have seen. They have a surplus of %s here at %.2f - fly and find who lacks it, and I will keep the ledger.",
     "Сводки пока нет, хозяин: этот рынок у нас единственный. Здесь избыток - %s по %.2f. Летите и найдите, кому его не хватает, а счёт я поведу."},
    {"Nothing worth the fuel, Master: of the %d markets we have seen, none pays for a run from here.",
     "Ничего, что стоило бы топлива, хозяин: ни один из виденных нами рынков (%d) не окупает рейс отсюда."},
    {"Nothing worth the fuel, Master: of the %d markets we have seen, none pays for a run from here. They do have a surplus of %s at %.2f - somewhere it will be wanted.",
     "Ничего, что стоило бы топлива, хозяин: ни один из виденных нами рынков (%d) не окупает рейс отсюда. Зато здесь избыток - %s по %.2f, где-то он понадобится."},
    // Глубокий просчёт по V (§28) — та же сводка, но вчетверо дальше и мимо
    // разведки, за топливо реактора.
    {"Deep model, Master - and the reactor pays for it: %s goes for %.2f here and fetches %.2f in %s, %.1f ly, %.0f years, some %.0f Cr. Nothing within reach beats it.",
     "Глубокий просчёт, хозяин, и платит за него реактор: %s здесь идёт по %.2f, а берут по %.2f в системе %s, это %.1f св.года, %.0f Y и около %.0f Cr. Ближе ничего лучше нет."},
    {"I ran the model, Master, and it comes back empty: with this hold and this purse there is no run out of here worth its fuel.",
     "Просчёт сделан, хозяин, и он пуст: с таким трюмом и таким кошельком отсюда не выходит ни одного рейса, который окупил бы топливо."},
    {"The bunker is dry, Master: there is nothing to think with. A model costs energy like everything else aboard.",
     "Бункер сух, хозяин: думать не на чем. Просчёт стоит энергии, как и всё остальное на борту."},
    {"model run: %.3F fuel burned", "просчёт: сожжено %.3F топлива"},
    {"model run refused: bunker dry", "просчёт не вышел: бункер сух"},

    {"no destination set: open a system and press DESTINATION",
     "цель не задана: откройте систему и нажмите НАЗНАЧЕНИЕ"},
    // Стрелка-указка новеллы. Одной строкой, потому что «TARGET» в одиночку
    // словарём не переводится, а по-русски здесь нужно «СЮДА», а не «ЦЕЛЬ».
    {"<-- TARGET", "<-- СЮДА"},

    // --- шапка биржевой таблицы ---
    //
    // Колонки строк считаются форматом "%-5s %-15s %6.0F %5.1F %6.1F %8.0F
    // %3.0F %4.0FY %3.0F%%", поэтому русская шапка собрана по тем же смещениям
    // — иначе таблица разъезжается. Куски литерала = колонки.
    {"ELEM  DESTINATION      UNITS   BUY  MODEL   PROFIT  LY  AGE CONF",
     "ЭЛ    " "НАЗНАЧЕНИЕ      " "  ЕДИН " "  ПОК " "МОДЕЛЬ " " ПРИБЫЛЬ " " СЛ " "  ЛЕТ " " ДОВ"},

    {"CLICK AMOUNT + TYPE NUMBER / EMPTY=MAX / RMB CELL QUICK BUY",
     "КЛИК ПО ОБЪЁМУ + ЧИСЛО / ПУСТО=МАКС / ПКМ ПО ЯЧЕЙКЕ - БЫСТРАЯ ПОКУПКА"},
    {"NOTHING SURVEYED YET.", "ПОКА НИЧЕГО НЕ РАЗВЕДАНО."},
    {"NO TRANSACTIONS RECORDED.", "ЖУРНАЛ ПУСТ."},
    {"CLUSTER FORMING...", "СКОПЛЕНИЕ СОБИРАЕТСЯ..."},
    {"ARE YOU SURE YOU WANT TO CHANGE YOUR HULL?", "ТОЧНО МЕНЯЕМ КОРПУС?"},
    {"THIS WILL PERMANENTLY DESTROY YOUR PREVIOUS HULL.", "ПРЕЖНИЙ КОРПУС БУДЕТ УНИЧТОЖЕН НАВСЕГДА."},
    {"-- SHIP MODULES (FIT WHILE DOCKED) --", "-- МОДУЛИ КОРАБЛЯ (СТАВЯТСЯ В ДОКЕ) --"},
    {"W/S THRUST  A/D YAW  R/F PITCH  Q/E ROLL  SHIFT WARP  M MINE  K DOCK  TAB LOCK  C VIEW  L EXIT",
     "W/S ТЯГА  A/D РЫСК  R/F ТАНГАЖ  Q/E КРЕН  SHIFT ВАРП  M ДОБЫЧА  K СТЫК  TAB ЗАХВАТ  C ВИД  L ВЫХОД"},
    {"<  LOAD FUEL      X JETTISON      LOAD PROPELLANT  >",
     "<  ЗАЛИТЬ ТОПЛИВО      X СБРОС      ЗАЛИТЬ РАБ. ТЕЛО  >"},
    {"X -  Y -  Z -", "X -  Y -  Z -"},
    {"Press M in a system to mine ore, then sell it for credits.",
     "Нажмите M в системе, чтобы добыть руду, потом продайте её за кредиты."},
    {"Welcome, Captain. Trade, mine, and grow your fleet.",
     "С прибытием, капитан. Торгуйте, добывайте, растите флот."},
    {"Millennial relativistic market correction: ledgers reconciled cluster-wide.",
     "Тысячелетняя релятивистская коррекция рынка: балансы сведены по всему скоплению."},
    {"Licence bought back. Trading resumed.", "Лицензия выкуплена. Торговля возобновлена."},
    {"no free licence - buy one at the brokerage (E)",
     "нет свободной лицензии - купите её на бирже (E)"},
    {"route blocked: drive cannot reach on this propellant",
     "маршрут закрыт: на этом рабочем теле двигатель не дотянет"},
    {"no drive setting reaches that system",
     "ни один режим двигателя не дотягивает до этой системы"},
    {"buy blocked: dock in the system first", "покупка закрыта: сперва пристыкуйтесь"},
    {"vault blocked: dock in your own system", "касса закрыта: нужно быть в своей системе"},
    {"Cargo hold full \u2014 mining stopped", "Трюм полон \u2014 добыча остановлена"},
    {"Nebula echo decoded \u2014 research surge", "Эхо туманности расшифровано \u2014 всплеск науки"},
    {"BUNKER (FUEL)", "БУНКЕР (ТОПЛИВО)"},
    {"TANK (PROPELLANT)", "БАК (РАБОЧЕЕ ТЕЛО)"},
    {"TANK (UNUSED BY TORCH)", "БАК (ФАКЕЛУ НЕ НУЖЕН)"},
};

// ---------------------------------------------------------------------------
// 2. СЛОВА И ОБОРОТЫ
//
// Ключ — кусок исходной строки в верхнем регистре: от первого слова оборота до
// последнего, ВМЕСТЕ со знаками между ними («HOLD / TANKS», «ADRIFT - NO PORT»).
// Совпадение жадное: сперва пробуется самый длинный оборот, поэтому
// «BUY SYSTEM» выигрывает у «BUY». Ключ не должен начинаться или кончаться
// знаком препинания — такой оборот словами не соберётся, ему место в EXACT.
//
// ⚠️ ДВУХБУКВЕННЫЕ КЛЮЧИ. Символы элементов (In, No, Se, Ta, Cr, Pb...) идут в
// интерфейс как обычный текст и попали бы под пословный перевод. Поэтому здесь
// НЕТ ключей IN, NO, SE, TA, CR и им подобных: «CR» (кредиты) и символы
// элементов остаются латиницей намеренно — это международные обозначения.
// ---------------------------------------------------------------------------
const Entry WORDS[] = {
    // ---------------- меню и оболочка ----------------
    {"NEW GAME", "НОВАЯ ИГРА"},
    {"LOAD GAME", "ЗАГРУЗИТЬ ИГРУ"},
    {"SOUND", "ЗВУК"},
    {"LANGUAGE", "ЯЗЫК"},
    {"EXIT", "ВЫХОД"},
    {"ON", "ВКЛ"},
    {"OFF", "ВЫКЛ"},
    {"PRESS ENTER TO BEGIN", "НАЖМИТЕ ENTER - НАЧАТЬ"},
    {"PRESS ENTER TO RESUME", "НАЖМИТЕ ENTER - ПРОДОЛЖИТЬ"},
    {"CLICK TO CONTINUE", "КЛИК - ДАЛЬШЕ"},
    {"CLICK TO REVEAL", "КЛИК - ПОКАЗАТЬ ВСЁ"},
    {"ESC SKIP", "ESC ПРОПУСТИТЬ"},
    {"PANEL PENDING", "КАДР НЕ ГОТОВ"},
    {"GENERATING CLUSTER", "СОБИРАЕМ СКОПЛЕНИЕ"},
    {"F1 SHOWS CONTROLS IN GAME", "F1 ПОКАЖЕТ УПРАВЛЕНИЕ В ИГРЕ"},

    // ---------------- карточка управления ----------------
    {"CONTROLS", "УПРАВЛЕНИЕ"},
    {"F1 / ESC CLOSE", "F1 / ESC ЗАКРЫТЬ"},
    {"FLIGHT", "ПОЛЁТ"},
    {"ENTER SYSTEM", "ВОЙТИ В СИСТЕМУ"},
    {"GO TO SELECTED", "ЛЕТЕТЬ К ВЫБРАННОЙ"},
    {"STOP SHIP", "ОСТАНОВИТЬ КОРАБЛЬ"},
    {"FOLLOW SHIP", "СЛЕДИТЬ ЗА КОРАБЛЁМ"},
    {"NEXT AGENT", "СЛЕД. АГЕНТ"},
    {"ENTER OPEN SYSTEM", "ENTER ОТКРЫТЬ СИСТЕМУ"},
    {"SPACE PAUSE", "SPACE ПАУЗА"},
    {"SIM SPEED", "СКОРОСТЬ СИМ."},
    {"AUTO TRADE", "АВТОТОРГОВЛЯ"},
    {"TRANSACTION LOG", "ЖУРНАЛ СДЕЛОК"},
    {"BUY BACK LICENCE", "ВЫКУПИТЬ ЛИЦЕНЗИЮ"},
    {"CYCLE ELEMENT", "СМЕНИТЬ ЭЛЕМЕНТ"},
    {"MINE ORE", "ДОБЫТЬ РУДУ"},
    {"REPAIR HULL", "ЧИНИТЬ КОРПУС"},
    {"SCAN ANOMALY", "СКАН АНОМАЛИИ"},
    {"BUY SYSTEM", "КУПИТЬ СИСТЕМУ"},
    {"SWITCH SHIP", "СМЕНИТЬ КОРАБЛЬ"},
    {"PLAYER SHIP", "КОРАБЛЬ ИГРОКА"},
    {"RESET VIEW", "СБРОС ВИДА"},
    {"THIS CARD", "ЭТА КАРТОЧКА"},
    {"SET ROUTE", "ЗАДАТЬ КУРС"},
    {"DRAG PAN", "ТАЩИТЬ КАРТУ"},
    {"WHEEL ZOOM", "КОЛЕСО ЗУМ"},
    {"ARROWS PAN", "СТРЕЛКИ СДВИГ"},
    {"LMB", "ЛКМ"}, {"RMB", "ПКМ"}, {"MMB", "СКМ"},
    {"SELECT", "ВЫБОР"},
    {"ROTATE", "ПОВОРОТ"},
    {"ROB", "ОГРАБИТЬ"},
    {"SAVE", "СОХРАНИТЬ"},
    {"LOAD", "ЗАГРУЗИТЬ"},
    {"ZOOM", "ЗУМ"},
    {"PLAY", "ПУСК"},
    {"PAUSE", "ПАУЗА"},
    {"PAUSED", "ПАУЗА"},
    {"CLOSE", "ЗАКРЫТЬ"},

    // ---------------- общие понятия ----------------
    {"STARCLUSTER", "STARCLUSTER"},
    {"SYSTEM", "СИСТЕМА"},
    {"SYSTEMS", "СИСТЕМ"},
    {"MY SYS", "МОЯ СИС"},
    {"SEED", "ЗЕРНО"},
    {"MAP", "КАРТА"},
    {"VIEW", "ВИД"},
    {"GAME", "ИГРА"},
    {"SHIP", "КОРАБЛЬ"},
    {"SHIPS", "КОРАБЛИ"},
    {"HULL", "КОРПУС"},
    {"HULLS", "КОРПУСА"},
    {"CARGO", "ТРЮМ"},
    {"TRADE", "ТОРГ"},
    {"TRADES", "СДЕЛКИ"},
    {"BROKER", "БРОКЕР"},
    {"BROKERAGE", "БИРЖА"},
    {"SHIPYARD", "ВЕРФЬ"},
    {"YARD", "ВЕРФЬ"},
    {"COLONY", "КОЛОНИЯ"},
    {"COLONIES", "КОЛОНИИ"},
    {"MARKET", "РЫНОК"},
    {"MARKETS", "РЫНКОВ"},
    {"PRICE", "ЦЕНА"},
    {"PRICES", "ЦЕНЫ"},
    {"CREDITS", "КРЕДИТЫ"},
    {"AGENT", "АГЕНТ"},
    {"AGENTS", "АГЕНТЫ"},
    {"FACTIONS", "ФРАКЦИИ"},
    {"OWNER", "ВЛАД"},
    {"OWNERS", "ВЛАДЕЛЬЦЫ"},
    {"ELEMENT", "ЭЛЕМЕНТ"},
    {"ELEMENTS", "ЭЛЕМЕНТЫ"},
    {"UNITS", "ЕДИНИЦ"},
    {"MASS", "МАССА"},
    {"VOL", "ОБЪЁМ"},
    {"SPEED", "СКОРОСТЬ"},
    {"ROUTE", "МАРШРУТ"},
    {"DESTINATION", "НАЗНАЧЕНИЕ"},
    {"ORIGIN", "ОТКУДА"},
    {"STEP", "ШАГ"},
    {"NONE", "НЕТ"},
    {"UNKNOWN", "НЕИЗВЕСТНО"},
    {"UNCLAIMED", "НИЧЕЙНАЯ"},
    {"EMPTY", "ПУСТО"},
    {"FREE", "СВОБОДНО"},
    {"BUILT", "ПОСТРОЕНО"},
    {"LIVE", "ОНЛАЙН"},
    {"LAST", "ПОСЛЕДНЕЕ"},
    {"LAST SEEN", "ВИДЕЛИ"},
    {"YES", "ДА"},
    {"DONE", "ГОТОВО"},
    {"OK", "ОК"},
    {"MAX", "МАКС"},
    {"OPTIMAL", "ОПТИМУМ"},
    {"OPTIMUM", "ОПТИМУМ"},
    {"AMOUNT", "ОБЪЁМ"},
    {"SHARE", "ДОЛЯ"},
    {"STOCK", "ЗАПАС"},
    {"SUPPLY", "ПРЕДЛОЖЕНИЕ"},
    {"DEMANDS", "СПРОС"},
    {"NEED", "НУЖНО"},
    // ЗАПАС / РАСХОД / ОХВАТ — одна тройка: сколько лежит на складе, сколько
    // система съедает за год, на сколько лет этого хватит. «ПРИМЕНЕНИЕ» читалось
    // как «для чего годится» (это соседние строки Q/ДОЛЯ) и вдобавок не влезало.
    {"USE", "РАСХОД"},
    {"TYPE", "ТИП"},
    {"ROLE", "РОЛЬ"},
    {"COVER", "ОХВАТ"},
    {"RICH", "БОГАТСТВО"},
    {"INFRA", "ИНФРА"},
    {"INCOME", "ДОХОД"},
    {"VAULT", "КАЗНА"},
    {"DEPOSIT", "ВНЕСТИ"},
    {"WITHDRAW", "СНЯТЬ"},
    {"TAKE", "ВЗЯТЬ"},
    {"TAKE ALL", "ВЗЯТЬ ВСЁ"},
    {"SELL ALL", "ПРОДАТЬ ВСЁ"},
    {"GIVE ALL", "ОТДАТЬ ВСЁ"},
    {"GIVE", "ОТДАТЬ"},
    {"BUY", "КУПИТЬ"},
    {"SELL", "ПРОДАТЬ"},
    {"BOARD", "ДОСКА"},
    {"JOB", "ЗАКАЗ"},
    {"JOBS", "ЗАКАЗЫ"},
    {"LOG", "ЖУРНАЛ"},
    {"GO", "ЛЕТЕТЬ"},
    {"DOCK", "СТЫК"},
    {"DOCKED", "В ДОКЕ"},
    {"ENTER", "ВОЙТИ"},
    {"MINE", "ДОБЫЧА"},
    {"REPAIR", "РЕМОНТ"},
    {"STOP", "СТОП"},
    {"FOLLOW", "СЛЕЖЕНИЕ"},
    {"EQUIP", "ПОСТАВИТЬ"},
    {"UNEQUIP", "СНЯТЬ"},
    {"FIT", "МОДУЛИ"},
    {"UP", "ВВЕРХ"},
    {"DOWN", "ВНИЗ"},
    {"DN", "ВНИЗ"},
    {"MOVING", "В ПУТИ"},
    {"EN ROUTE", "В ПУТИ"},
    {"WAIT", "ЖДЁМ"},
    {"WAIT SIGNAL", "ЖДЁМ СИГНАЛ"},
    {"IDLE", "ПРОСТОЙ"},
    {"SLOW", "МЕДЛ"},
    {"FAST", "БЫСТР"},
    {"BURN", "ТЯГА"},
    {"CORES", "ЯДРА"},
    {"KNOWN", "ИЗВЕСТНО"},
    {"LVL", "УР"},
    {"OF", "ИЗ"},
    {"HERE", "ЗДЕСЬ"},
    {"EVERY ONE OF", "КАЖДЫЙ ИЗ"},
    {"SURVEYED MARKETS BELOW", "РАЗВЕДАННЫХ РЫНКОВ НИЖЕ"},
    {"SURVEYED MARKETS", "РАЗВЕДАННЫХ РЫНКОВ"},
    {"POPULATION", "НАСЕЛЕНИЕ"},
    {"POP", "НАС"},
    {"INDUSTRY", "ПРОМЫШЛЕННОСТЬ"},
    {"IND", "ПРОМ"},
    {"HABITABILITY", "ОБИТАЕМОСТЬ"},
    {"HABITABLE", "ОБИТАЕМАЯ"},
    {"HAB", "ОБИТ"},
    {"RESOURCES", "РЕСУРСЫ"},
    {"OBJECTIVES", "ЗАДАЧИ"},
    {"PLAYER STATUS", "СТАТУС ИГРОКА"},
    {"NO PLAYER", "НЕТ ИГРОКА"},
    {"STAR", "ЗВЕЗДА"},
    {"TIMERTIA", "ТИМЕРТИЯ"},
    {"DEEP SPACE", "ГЛУБОКИЙ КОСМОС"},
    {"ADRIFT - NO PORT", "ДРЕЙФ - НЕТ ПОРТА"},
    {"UNKNOWN NODE", "НЕИЗВЕСТНЫЙ УЗЕЛ"},
    {"AN ADJACENT NODE", "СОСЕДНИЙ УЗЕЛ"},
    {"NOWHERE", "НИКУДА"},
    {"ISOTOPES", "ИЗОТОПЫ"},

    // ---------------- заголовки и статусы окон ----------------
    {"SYSTEM DATA UNKNOWN UNTIL ARRIVAL", "ДАННЫЕ СИСТЕМЫ БУДУТ ПО ПРИБЫТИИ"},
    {"REMOTE SYSTEM DATA UNKNOWN UNTIL ARRIVAL", "ДАННЫЕ ЧУЖОЙ СИСТЕМЫ БУДУТ ПО ПРИБЫТИИ"},
    {"REMOTE SYSTEM DATA FROM SIGNALS", "ДАННЫЕ СИСТЕМЫ - ПО СИГНАЛАМ"},
    {"REMOTE DATA FROM SIGNALS", "ДАННЫЕ - ПО СИГНАЛАМ"},
    {"NO SYSTEM SELECTED", "СИСТЕМА НЕ ВЫБРАНА"},
    {"NO SYSTEM", "НЕТ СИСТЕМЫ"},
    {"NO MARKET", "НЕТ РЫНКА"},
    {"NO LIVE MARKET - DOCK IN THIS SYSTEM", "НЕТ ЖИВОГО РЫНКА - ПРИСТЫКУЙТЕСЬ ЗДЕСЬ"},
    {"NO LIVE SHIPYARD - DOCK IN THIS SYSTEM", "НЕТ ЖИВОЙ ВЕРФИ - ПРИСТЫКУЙТЕСЬ ЗДЕСЬ"},
    {"DOCK IN THIS SYSTEM FOR LIVE QUOTES", "ПРИСТЫКУЙТЕСЬ ЗДЕСЬ РАДИ ЖИВЫХ КОТИРОВОК"},
    {"NOT ON SITE - DOCK HERE TO ACT", "ВАС ЗДЕСЬ НЕТ - ПРИСТЫКУЙТЕСЬ, ЧТОБЫ ДЕЙСТВОВАТЬ"},
    {"NO LOCAL CONTRACTS RIGHT NOW", "МЕСТНЫХ ЗАКАЗОВ СЕЙЧАС НЕТ"},
    {"NO VISIBLE CONTRACT SIGNALS", "СИГНАЛОВ О ЗАКАЗАХ НЕ ВИДНО"},
    {"NO LOCAL OR SIGNAL JOB BOARD", "НЕТ НИ МЕСТНОЙ ДОСКИ, НИ СИГНАЛОВ"},
    {"LOCAL JOB BOARD", "МЕСТНАЯ ДОСКА ЗАКАЗОВ"},
    {"LAST-KNOWN JOB SIGNAL / GO ORIGIN TO ACCEPT", "СТАРЫЙ СИГНАЛ ЗАКАЗА / ЛЕТИТЕ К ИСТОЧНИКУ"},
    {"ACTIVE JOBS + VISIBLE LISTINGS", "АКТИВНЫЕ ЗАКАЗЫ + ВИДИМЫЕ ОБЪЯВЛЕНИЯ"},
    {"LAST-KNOWN OWNER MAY BE STALE", "ВЛАДЕЛЕЦ ПО СТАРЫМ ДАННЫМ"},
    {"LAST-KNOWN OWNER/MARKET MAY BE STALE", "ВЛАДЕЛЕЦ/РЫНОК ПО СТАРЫМ ДАННЫМ"},
    {"OWNER/MARKET MAY BE STALE", "ВЛАДЕЛЕЦ/РЫНОК ПО СТАРЫМ ДАННЫМ"},
    {"OWNER OVERLAY IS LAST-KNOWN", "СЛОЙ ВЛАДЕЛЬЦЕВ - ПО СТАРЫМ ДАННЫМ"},
    {"KNOWN OWNER REPORTS", "ИЗВЕСТНЫЕ СВОДКИ ВЛАДЕЛЬЦЕВ"},
    {"OWNER UNKNOWN", "ВЛАДЕЛЕЦ НЕИЗВЕСТЕН"},
    {"LOCAL AGENTS HIDDEN", "МЕСТНЫЕ АГЕНТЫ СКРЫТЫ"},
    {"MARKET UNKNOWN / NO SNAPSHOT", "РЫНОК НЕИЗВЕСТЕН / НЕТ СНИМКА"},
    {"MARKET SNAPSHOT", "СНИМОК РЫНКА"},
    {"MARKET LIVE/LOCAL CONF", "РЫНОК ЖИВОЙ/МЕСТНЫЙ ДОВ"},
    {"MARKET CONF NONE", "ДОВЕРИЕ К РЫНКУ НУЛЕВОЕ"},
    {"CONF", "ДОВ"},
    {"RISK", "РИСК"},
    {"THREATS", "УГРОЗЫ"},
    {"AGE", "ВОЗР"},
    {"BEST DEALS ACROSS", "ЛУЧШИЕ СДЕЛКИ ПО"},
    {"NOT A LIVE FEED", "ЭТО НЕ ЖИВАЯ ЛЕНТА"},
    {"PICK AN ELEMENT TO LIST EVERY SURVEYED MARKET FOR IT",
     "ВЫБЕРИТЕ ЭЛЕМЕНТ - ПОКАЖЕМ ВСЕ РАЗВЕДАННЫЕ РЫНКИ ПО НЕМУ"},
    {"FILTER BY ELEMENT", "ФИЛЬТР ПО ЭЛЕМЕНТУ"},
    {"ALL ELEMENTS", "ВСЕ ЭЛЕМЕНТЫ"},
    {"BACK TO LIST", "НАЗАД К СПИСКУ"},
    {"VISIT NEIGHBOURING SYSTEMS - EACH ONE YOU DOCK AT",
     "ЛЕТАЙТЕ К СОСЕДНИМ СИСТЕМАМ - КАЖДАЯ, ГДЕ ВЫ ПРИСТЫКУЕТЕСЬ,"},
    {"IS ADDED HERE AND STAYS IN YOUR MODEL", "ПОПАДЁТ СЮДА И ОСТАНЕТСЯ В ВАШЕЙ МОДЕЛИ"},
    {"TRANSACTION HISTORY", "ЖУРНАЛ"},
    // Строки журнала (§23). Собираются в game.cpp по-английски и переводятся
    // здесь: имена звёзд, номера заказов и числа словарь не трогает.
    {"Deep Space", "ОТКРЫТЫЙ КОСМОС"},
    {"COLONY SUPPLY", "СНАБЖЕНИЕ КОЛОНИИ"},
    // --- Репутация возчика и тиры заказов (§24) -----------------------------
    // Звания идут ровно по `Game::jobRankName`. Слова подобраны короткие:
    // строка панели фракций и без того тесная, а русский длиннее английского.
    {"STANDING AND KNOWN OWNER REPORTS", "РЕПУТАЦИЯ И СВОДКИ ВЛАДЕЛЬЦЕВ"},
    {"PRIME CARRIER", "ПЕРВЫЙ ВОЗЧИК"},
    {"CONTRACTOR", "ПОДРЯДЧИК"},
    {"TRUSTED", "ДОВЕРЕННЫЙ"},
    {"NOBODY", "НИКТО"},
    {"KNOWN", "ЗНАКОМЫЙ"},
    {"MASTER", "МАСТЕР"},
    {"LEGEND", "ЛЕГЕНДА"},
    {"JOBS DONE", "ЗАКАЗОВ СДАНО"},
    {"MAX LOAD", "МАКС. ГРУЗ"},
    {"FLEET HOLD HERE", "ТРЮМ ФЛОТА ЗДЕСЬ"},
    {"FLEET", "ФЛОТ"},
    {"RUSH", "СРОЧНО"},
    {"EARLY", "ДОСРОЧНО"},
    {"REP", "РЕП"},
    {"fleet still inbound", "ФЛОТ ЕЩЁ В ПУТИ"},
    {"hauling contract", "ВЕЗЁТ ЗАКАЗ"},
    {"TARGET STALE", "ЦЕЛЬ УСТАРЕЛА"},
    {"YEAR", "ГОД"},
    {"TOOK", "ВЗЯТ"},
    {"DUE", "СРОК"},
    {"EMPTY THE HOLD FIRST", "СНАЧАЛА ОСВОБОДИТЕ ТРЮМ"},
    {"HOLD TOO SMALL", "ТРЮМ МАЛ"},
    {"FILL FUEL+PROP", "ЗАЛИТЬ ТОПЛ+РАБ.ТЕЛО"},
    {"TANKS FULL", "БАКИ ПОЛНЫ"},
    {"FILL", "ЗАЛИТЬ"},
    {"EXPIRED", "ПРОСРОЧЕН"},
    {"LATE", "С ОПОЗДАНИЕМ"},
    {"AVAILABLE IN CARGO", "ЕСТЬ В ТРЮМЕ"},
    {"INSTALLED", "УСТАНОВЛЕНО"},
    {"SHIP SYSTEMS", "СИСТЕМЫ КОРАБЛЯ"},
    {"SHIP UPGRADES", "УЛУЧШЕНИЯ КОРАБЛЯ"},
    {"BUY SHIPS / FIT MODULES", "ПОКУПКА КОРАБЛЕЙ / МОДУЛИ"},
    {"NO INDUSTRIAL USE", "ПРОМЫШЛЕННОГО ПРИМЕНЕНИЯ НЕТ"},
    {"NO DESTINATION - OPEN A SYSTEM AND PRESS DESTINATION",
     "ЦЕЛЬ НЕ ЗАДАНА - ОТКРОЙТЕ СИСТЕМУ И НАЖМИТЕ НАЗНАЧЕНИЕ"},
    {"DEST SET", "ЦЕЛЬ ЗАДАНА"},
    {"LOCAL MARKET", "МЕСТНЫЙ РЫНОК"},

    // ---------------- подсказки-действия ----------------
    {"FLY THE SYSTEM: PRESS L", "ЛЕТАТЬ В СИСТЕМЕ: L"},
    {"REPAIR HULL: DOCK + PRESS J", "РЕМОНТ КОРПУСА: СТЫКОВКА + J"},
    {"SCAN ANOMALY: PRESS K", "СКАН АНОМАЛИИ: K"},
    {"UPGRADE: SHIPYARD", "УЛУЧШЕНИЕ: ВЕРФЬ"},
    {"RESEARCH A CHROMOCORE", "ИЗУЧИТЬ ХРОМОЯДРО"},
    {"TRADE: BUY", "ТОРГОВЛЯ: КУПИТЬ"},
    {"NO REACTOR FUEL", "РЕАКТОР БЕЗ ТОПЛИВА"},
    {"MINING OFF", "ДОБЫЧА ВЫКЛ"},
    {"MINING", "ДОБЫЧА"},
    {"ROCK SHATTERED", "ГЛЫБА РАСКОЛОТА"},
    {"CARGO FULL", "ТРЮМ ПОЛОН"},
    {"ACCEPT", "ПРИНЯТЬ"},

    // ---------------- квота, лицензия, тариф ----------------
    {"QUOTA", "КВОТА"},
    {"QUOTA MET", "КВОТА ЗАКРЫТА"},
    {"SETTLE QUOTA", "ЗАКРЫТЬ КВОТУ"},
    {"TARIFF", "ТАРИФ"},
    {"LEFT", "ОСТАЛОСЬ"},
    {"LICENCE", "ЛИЦЕНЗИЯ"},
    {"LICENCES", "ЛИЦЕНЗИИ"},
    {"NEED LICENCE", "НУЖНА ЛИЦЕНЗИЯ"},
    // Залог за заказ (§37.3): в строке доски и в объяснении отказа.
    {"BOND", "ЗАЛОГ"},
    {"DUE", "СРОК"},
    {"DEPOSIT TOO HIGH", "ЗАЛОГ НЕ ПО КАРМАНУ"},
    {"LICENCE REVOKED - BUY BACK", "ЛИЦЕНЗИЯ ОТОЗВАНА - ВЫКУП"},

    // ---------------- автопилот флота (§35) ----------------
    {"AUTO ON", "АВТО ВКЛ"},
    {"AUTO OFF", "АВТО ВЫКЛ"},
    {"HULL PUT ON AUTOPILOT", "БОРТ ПОСТАВЛЕН НА АВТОПИЛОТ"},
    {"HULL BACK UNDER MANUAL ORDERS", "БОРТ СНОВА ПОД РУЧНЫМ УПРАВЛЕНИЕМ"},
    {"CANNOT CHANGE ORDERS IN TRANSIT", "В ПОЛЁТЕ ПРИКАЗ НЕ СМЕНИТЬ"},
    {"THIS HULL IS CARRYING A JOB", "ЭТОТ БОРТ ВЕЗЁТ ЗАКАЗ"},
    {"THIS IS THE HULL YOU ARE FLYING", "ЭТИМ БОРТОМ ВЫ УПРАВЛЯЕТЕ САМИ"},
    {"BOUNTY TARGET DOWN", "ЦЕЛЬ ЗАКАЗА СБИТА"},
    {"SHIP DOWN - NO BOUNTY LEFT HERE", "БОРТ СБИТ - НАГРАД ЗДЕСЬ БОЛЬШЕ НЕТ"},

    // ---------------- лестница целей (§34) ----------------
    {"SURVEY 10 MARKETS", "РАЗВЕДАТЬ 10 РЫНКОВ"},
    {"TAKE A JOB: JOBS BOARD", "ВЗЯТЬ ЗАКАЗ: ДОСКА ЗАКАЗОВ"},
    {"MEET THE QUOTA - OR SETTLE IT IN CASH (E)", "ЗАКРЫТЬ КВОТУ - ИЛИ ОТКУПИТЬСЯ (E)"},
    {"BUY A LICENCE (E), THEN A HULL (U)", "КУПИТЬ ЛИЦЕНЗИЮ (E), ПОТОМ КОРПУС (U)"},
    // Имена статов в панели корабля (§37.8). Коротко — колонка 12 знаков.
    {"MIND", "УМ"},
    {"CHARM", "ОБАЯНИЕ"},
    {"TACTICS", "ТАКТИКА"},
    {"SENSOR", "СЕНСОРЫ"},
    {"FRAME", "КАРКАС"},
    // «ENTER» — ИМЯ КЛАВИШИ, а не глагол: без этой строки словарь переводил его
    // как «ВОЙТИ», и подсказка в коробке новеллы предлагала куда-то войти.
    {"ENTER >", "ВВОД >"},
    {"LUCK", "УДАЧА"},
    {"AMOUNT IN UNITS", "ОБЪЁМ В ЕДИНИЦАХ"},
    {"ATOMIC MASS", "АТОМНАЯ МАССА"},
    {"MINED", "ДОБЫТО"},
    {"STARS", "ЗВЁЗД"},
    {"SHIPS", "БОРТОВ"},
    {"YEAR", "ГОД"},
    {"FIT", "ГОДНОСТЬ"},
    // Полные имена нужд рынка: раньше в панели стоял трёхбуквенный код.
    {"FUSION FUEL", "СИНТЕЗ"},
    {"FISSION FUEL", "ДЕЛЕНИЕ"},
    {"STRUCTURE", "КОНСТРУКЦИИ"},
    {"CONDUCTORS", "ПРОВОДНИКИ"},
    {"CATALYSIS", "КАТАЛИЗ"},
    {"LIFE SUPPORT", "ЖИЗНЕОБЕСПЕЧЕНИЕ"},
    {"REAGENTS", "РЕАГЕНТЫ"},
    {"SHIELDING", "ЗАЩИТА"},
    {"REFRACTORY", "ОГНЕУПОРЫ"},
    {"INERT / NOBLE", "ИНЕРТНЫЕ"},
    {"PRESS ESC AGAIN TO SAVE AND QUIT", "ЕЩЁ РАЗ ESC - СОХРАНИТЬ И ВЫЙТИ"},
    {"CLOSE / QUIT", "ЗАКРЫТЬ / ВЫХОД"},
    {"IN SYSTEM", "В СИСТЕМЕ"},
    {"LICENCE WORKED OFF - TRADING RESUMED", "ЛИЦЕНЗИЯ ОТРАБОТАНА - ТОРГОВЛЯ ОТКРЫТА"},
    {"TOWED TO", "ОТБУКСИРОВАН В"},
    {"THE TUG TOOK ITS SHARE", "БУКСИР ЗАБРАЛ СВОЮ ДОЛЮ"},
    {"BUY A SECOND HULL (E)", "КУПИТЬ ВТОРОЙ КОРПУС (E)"},
    {"BUY A SYSTEM (C)", "КУПИТЬ СИСТЕМУ (C)"},
    {"FIT A CONTAINMENT BAY (Y)", "ПОСТАВИТЬ ЯЧЕЙКУ УДЕРЖАНИЯ (Y)"},
    {"RUN EXOTIC MATTER (Y)", "СВОЗИТЬ ЭКЗОТИКУ (Y)"},
    {"FORGE A CORE YOU CHOSE (Y)", "СКОВАТЬ ЯДРО ПО ВЫБОРУ (Y)"},
    {"BUY INTO A POWER (E)", "ВОЙТИ В ДОЛЮ К ДЕРЖАВЕ (E)"},
    {"PLATE THE HULL IN NEUTRONIUM", "ОБШИТЬ КОРПУС НЕЙТРОНИУМОМ"},

    // ---------------- биржа держав (§33) ----------------
    {"POWER          PER SHARE  PER YEAR     HELD     VALUE    PROFIT",
     "ДЕРЖАВА        ЗА АКЦИЮ    В ГОД   НА РУКАХ   СТОИТ    ПРИБЫЛЬ"},
    {"NO REPORT PUBLISHED YET", "ОТЧЁТА ЕЩЁ НЕ БЫЛО"},
    {"YOUR OWN FREEHOLD", "ВАШЕ СОБСТВЕННОЕ ВЛАДЕНИЕ"},
    {"YOU CANNOT BUY SHARES IN YOUR OWN FREEHOLD", "АКЦИИ СОБСТВЕННОГО ВЛАДЕНИЯ НЕ КУПИТЬ"},
    {"NO PUBLISHED REPORT FOR THIS POWER", "ПО ЭТОЙ ДЕРЖАВЕ ОТЧЁТА НЕТ"},
    // ⚠️ Ключей с «%s» здесь быть не может: в интерфейсе строка приходит в
    // drawText УЖЕ собранной, и словарь видит подставленные числа. Формат-ключи
    // работают только там, где переводят САМ формат (реплики Тимертии).
    {"PORTFOLIO", "ПОРТФЕЛЬ"},
    // Сводка состояния (§40): всё имущество игрока одной строкой.
    {"NET WORTH", "СОСТОЯНИЕ"},
    {"WALLETS", "КОШЕЛЬКИ"},
    {"IN FLIGHT", "В ПУТИ"},
    {"VAULTS", "КАССЫ"},
    {"HULLS", "КОРПУСА"},
    {"REPORT", "ОТЧЁТ"},
    {"YOUR STANDING KEEPS IT FRESH", "ВАША РЕПУТАЦИЯ ДЕРЖИТ ЕГО СВЕЖИМ"},
    {"DIVIDENDS ARRIVE ON THE ACCOUNT AT THE SPEED OF LIGHT",
     "ДИВИДЕНДЫ ПРИХОДЯТ НА СЧЁТ СО СКОРОСТЬЮ СВЕТА"},
    {"A POWER IS PRICED BY WHAT IT EARNS - HELP IT AND YOUR STAKE GROWS",
     "ДЕРЖАВА СТОИТ СТОЛЬКО, СКОЛЬКО ЗАРАБАТЫВАЕТ - ПОМОГИ ЕЙ, И ДОЛЯ ВЫРАСТЕТ"},
    {"BACK TO DEALS", "НАЗАД К СДЕЛКАМ"},
    {"SHARES", "АКЦИИ"},
    {"STAKE CAPPED AT A QUARTER OF THE POWER", "БОЛЬШЕ ЧЕТВЕРТИ ДЕРЖАВЫ НЕ ПРОДАЮТ"},
    {"NOT ENOUGH CREDITS FOR SHARES", "НА АКЦИИ НЕ ХВАТАЕТ КРЕДИТОВ"},
    {"NO SHARES TO SELL", "АКЦИЙ НА РУКАХ НЕТ"},

    // Реплики Тимертии про хайтек-этаж (§37.2). Коробка держит ПЯТЬ строк —
    // длину меряет `make uishots` на узком окне и на обоих языках.
    {"Master, this port trades matter that is not on the periodic table at all. Antimatter, neutronium, coherent condensate - press Y.",
     "Хозяин, в этом порту торгуют веществом, которого нет в таблице элементов вовсе. Антивещество, нейтрониум, когерентный конденсат — клавиша Y."},
    {"The table of elements ends where chemistry ends. Everything beyond it is made at dead stars and industrial furnaces, and it costs accordingly.",
     "Таблица элементов кончается там же, где кончается химия. Всё, что за ней, родится у мёртвых звёзд и в заводских печах, и стоит соответственно."},
    {"You will need a containment bay before you can carry any of it. Fit one at a shipyard, and the second floor of this economy opens up.",
     "Везти это не в чем без ячейки удержания. Поставьте её на верфи — и откроется второй этаж здешней экономики."},

    // ---------------- хайтек-этаж: экзотическая материя (§31) ----------------
    // Слова, которых в игре не было вовсе: три вещества, отсек удержания,
    // кузница ядер. Двухбуквенные коды AM/NM/QC в словарь НЕ идут — они
    // столкнулись бы с символами элементов (Am — америций!), и остаются как есть.
    {"FORGE A CHROMOCORE OF YOUR CHOICE", "ХРОМОКОР ПО ВАШЕМУ ВЫБОРУ"},
    {"MATTER      HELD    RESERVE      UNIT PRICE",
     "ВЕЩЕСТВО   НА БОРТУ     ЗАПАС     ЦЕНА ЕДИНИЦЫ"},
    {"NOT TRADED HERE", "ЗДЕСЬ НЕ ТОРГУЮТ"},
    {"REFIT AND FORGE NEED A SHIPYARD - THIS SYSTEM HAS NONE",
     "ПЕРЕОСНАСТКЕ И КУЗНИЦЕ НУЖНА ВЕРФЬ - ЗДЕСЬ ЕЁ НЕТ"},
    {"NOT ON SITE - DOCK HERE TO TRADE", "ВАС ЗДЕСЬ НЕТ - ПРИЧАЛЬТЕ, ЧТОБЫ ТОРГОВАТЬ"},
    {"HIGH-TECH EXCHANGE", "ХАЙТЕК-БИРЖА"},
    {"EXOTICS", "ЭКЗОТИКА"},
    {"ANTIMATTER", "АНТИВЕЩЕСТВО"},
    {"NEUTRONIUM", "НЕЙТРОНИУМ"},
    {"CONDENSATE", "КОНДЕНСАТ"},
    {"MATTER", "ВЕЩЕСТВО"},
    {"HELD", "НА БОРТУ"},
    {"RESERVE", "ЗАПАС"},
    {"ANNIHILATION CATALYST - MIXES INTO THE BUNKER",
     "КАТАЛИЗАТОР АННИГИЛЯЦИИ - ИДЁТ В БУНКЕР С ТОПЛИВОМ"},
    {"DEGENERATE MATTER - THE DENSEST ARMOUR THERE IS",
     "ВЫРОЖДЕННОЕ ВЕЩЕСТВО - ПЛОТНЕЕ БРОНИ НЕ БЫВАЕТ"},
    {"COHERENT SUBSTRATE - THE BODY OF A CHROMOCORE",
     "КОГЕРЕНТНАЯ ПОДЛОЖКА - ТЕЛО ХРОМОКОРА"},
    {"NO CONTAINMENT BAY - EXOTIC MATTER CANNOT BE HELD",
     "НЕТ ЯЧЕЙКИ УДЕРЖАНИЯ - ЭКЗОТИКУ ВЕЗТИ НЕ В ЧЕМ"},
    {"PRICE FOLLOWS THE LOCAL RESERVE - BUYING IT UP RAISES IT",
     "ЦЕНА ИДЁТ ОТ МЕСТНОГО ЗАПАСА - СКУПАЯ ЕГО, ВЫ ЕЁ ПОДНИМАЕТЕ"},
    {"BAY MAXED", "ЯЧЕЙКА ПРЕДЕЛЬНА"},
    {"PLATING MAXED", "БРОНЯ ПРЕДЕЛЬНА"},
    {"BAY", "ЯЧЕЙКА"},
    {"PLATE", "СЛОЙ"},
    {"UNITS", "ЕДИНИЦ"},
    {"NO EXOTICS MARKET HERE", "ЗДЕСЬ НЕТ РЫНКА ЭКЗОТИКИ"},
    {"NO EXOTICS MARKET IN THIS SYSTEM", "В ЭТОЙ СИСТЕМЕ НЕТ РЫНКА ЭКЗОТИКИ"},
    {"NO CONTAINMENT BAY - REFIT FIRST", "НЕТ ЯЧЕЙКИ УДЕРЖАНИЯ - СНАЧАЛА ПЕРЕОСНАСТКА"},
    {"CONTAINMENT BAY FITTED", "ЯЧЕЙКА УДЕРЖАНИЯ ПОСТАВЛЕНА"},
    {"CONTAINMENT BAY NEEDS MORE CREDITS", "НА ЯЧЕЙКУ УДЕРЖАНИЯ НЕ ХВАТАЕТ КРЕДИТОВ"},
    {"CONTAINMENT AT MAXIMUM", "ЯЧЕЙКА УЖЕ ПРЕДЕЛЬНАЯ"},
    {"NEEDS SHIPYARD LVL 2 TO FIT CONTAINMENT", "ЯЧЕЙКУ СТАВЯТ НА ВЕРФИ УРОВНЯ 2"},
    {"NEEDS SHIPYARD LVL 2 TO WELD PLATING", "БРОНЮ ВАРЯТ НА ВЕРФИ УРОВНЯ 2"},
    {"NEEDS NEUTRONIUM IN THE CONTAINMENT BAY", "НУЖЕН НЕЙТРОНИУМ В ЯЧЕЙКЕ"},
    {"HULL PLATED WITH NEUTRONIUM", "КОРПУС ОБШИТ НЕЙТРОНИУМОМ"},
    {"PLATING NEEDS MORE CREDITS", "НА БРОНЮ НЕ ХВАТАЕТ КРЕДИТОВ"},
    {"PLATING AT MAXIMUM", "БРОНЯ УЖЕ ПРЕДЕЛЬНАЯ"},
    {"CORE FORGE NEEDS CONDENSATE", "КУЗНИЦЕ ЯДРА НУЖЕН КОНДЕНСАТ"},
    {"CORE FORGE NEEDS MORE CREDITS", "КУЗНИЦЕ ЯДРА НЕ ХВАТАЕТ КРЕДИТОВ"},
    {"CORE FORGE NEEDS SHIPYARD LVL 2", "КУЗНИЦА ЯДРА ТРЕБУЕТ ВЕРФЬ УРОВНЯ 2"},
    {"NOTHING TO BUY HERE", "ЗДЕСЬ НЕЧЕГО КУПИТЬ"},
    {"NOTHING TO SELL", "НЕЧЕГО ПРОДАВАТЬ"},
    {"CONTAINMENT BAY FITTED: EXOTIC MATTER CAN BE CARRIED",
     "ЯЧЕЙКА УДЕРЖАНИЯ ПОСТАВЛЕНА: ЭКЗОТИКУ ЕСТЬ В ЧЁМ ВЕЗТИ"},
    {"NEUTRONIUM WELDED ONTO THE HULL", "НЕЙТРОНИУМ НАВАРЕН НА КОРПУС"},

    // ---------------- владение системой ----------------
    {"BUY THE SYSTEM WHOLE - IT KEEPS LIVING", "СИСТЕМА ПОКУПАЕТСЯ ЦЕЛИКОМ - И ЖИВЁТ ДАЛЬШЕ"},
    {"SYSTEM FOR SALE", "СИСТЕМА ПРОДАЁТСЯ"},
    {"ASKING PRICE", "ЦЕНА"},
    {"PAID IN FULL TO THE SELLING FACTION", "ПОЛНОСТЬЮ ВЫПЛАЧИВАЕТСЯ ФРАКЦИИ-ПРОДАВЦУ"},
    {"YOUR COLONY, ALL PRICES 0", "ВАША КОЛОНИЯ, ВСЕ ЦЕНЫ 0"},
    {"TAKE FREELY - BUT SELLING HERE PAYS NOTHING AND COUNTS FOR NO QUOTA",
     "БЕРИТЕ ДАРОМ - НО ПРОДАЖА ЗДЕСЬ НЕ ДАЁТ НИ КРЕДИТА И НЕ ИДЁТ В КВОТУ"},
    {"YOURS", "ВАША"},
    {"VS YOURS", "ПРОТИВ ВАШЕГО"},
    {"VS CLUSTER", "ПРОТИВ СКОПЛЕНИЯ"},
    {"FREE MARKET - TAKE AND GIVE AT ZERO", "СВОБОДНЫЙ РЫНОК - БЕРИТЕ И ОТДАВАЙТЕ ПО НУЛЮ"},
    {"COLONY VAULT", "КАЗНА КОЛОНИИ"},
    // Переименование своей системы (§25). Подсказки уложены в 28 знаков — это
    // ширина колонки действий в окне собственности.
    {"SYSTEM NAME", "ИМЯ СИСТЕМЫ"},
    {"CLICK TO RENAME", "КЛИК - ПЕРЕИМЕНОВАТЬ"},
    {"RENAME ON SITE ONLY", "ПЕРЕИМЕНОВАТЬ НА МЕСТЕ"},
    {"ENTER APPLIES / ESC CANCELS", "ВВОД ПРИМЕНИТ / ESC ОТМЕНИТ"},
    {"THE COLONY SPENDS ITS OWN VAULT", "КОЛОНИЯ ТРАТИТ СВОЮ КАЗНУ"},
    {"IDLE - IT WILL START A BUILD WHEN FUNDED", "ПРОСТОЙ - НАЧНЁТ СТРОЙКУ, КОГДА БУДУТ СРЕДСТВА"},
    {"QUEUE EMPTY", "ОЧЕРЕДЬ ПУСТА"},
    {"QUEUE", "ОЧЕРЕДЬ"},
    {"COST", "ЦЕНА"},
    {"BUILDING", "СТРОИТСЯ"},
    {"UNMET NEEDS", "НЕЗАКРЫТЫЕ НУЖДЫ"},
    {"COLONY STARVING", "КОЛОНИЯ ГОЛОДАЕТ"},
    {"CONTEST", "СПОР"},
    {"AUTOMATION", "АВТОМАТИЗАЦИЯ"},
    {"DEFENSE", "ОБОРОНА"},
    {"DEF", "ОБОР"},
    {"BOUNTY", "НАГРАДА"},
    {"MANHUNT", "ОХОТА"},
    {"WANTED", "В РОЗЫСКЕ"},
    {"STANDING", "РЕПУТАЦИЯ"},

    // ---------------- двигатель, топливо, трюм ----------------
    {"FUEL", "ТОПЛИВО"},
    {"PROP", "РАБ.ТЕЛО"},
    {"PROPELLANT", "РАБОЧЕЕ ТЕЛО"},
    {"BUNKER", "БУНКЕР"},
    {"TANK", "БАК"},
    {"HOLD / TANKS", "ТРЮМ / БАКИ"},
    {"HOLD/HEAVY", "ТРЮМ/ТЯЖЕЛО"},
    {"HOLD", "ТРЮМ"},
    {"TANKS", "БАКИ"},
    {"FILL FUEL+PROP", "ЗАЛИТЬ ТОПЛ+РАБ.ТЕЛО"},
    {"BUY FUEL+PROP", "КУПИТЬ ТОПЛ+РАБ.ТЕЛО"},
    {"PER HOP", "НА ПРЫЖОК"},
    {"DRIVE", "ДВИГАТЕЛЬ"},
    {"SPECIAL", "ОСОБОЕ"},
    {"THROTTLE", "ТЯГА"},
    {"CRUISE", "КРЕЙСЕР"},
    {"BRAKING", "ТОРМОЖЕНИЕ"},
    // Двигатель настраивается только на стоянке (§12.4): в полёте рабочая точка
    // зафиксирована, и обе ручки окна HOLD подписываются этим словом.
    {"LOCKED", "ЗАПЕРТО"},
    // Кошелёк лежит на борту: деньги между своими бортами перевозят руками.
    {"GIVE CR", "ОТДАТЬ CR"},
    {"TAKE CR", "ВЗЯТЬ CR"},
    {"HAS", "ИМЕЕТ"},
    // Счёт фракции: деньги ходят светом, поэтому цифры две.
    {"TO ACCOUNT", "НА СЧЁТ"},
    {"FROM ACCOUNT", "СО СЧЁТА"},
    {"ACCOUNT", "СЧЁТ"},
    {"CLEARED", "ПОДТВЕРЖДЕНО"},
    {"IN FLIGHT", "В ПУТИ"},
    // Окупаемость системы — следствие цены и выпуска, а не отдельная крутилка.
    {"YIELD", "ДОХОД"},
    {"PAYS BACK IN", "ОКУПИТСЯ ЗА"},
    {"NO WORKING ECONOMY YET", "ХОЗЯЙСТВА ПОКА НЕТ"},
    {"CLEARING HOUSE", "КЛИРИНГОВАЯ ПАЛАТА"},
    {"BURNING FUEL TO STOP", "ЖЖЁМ ТОПЛИВО НА ТОРМОЖЕНИЕ"},
    {"OVERLOAD", "ПЕРЕГРУЗ"},
    {"SHORT BY", "НЕ ХВАТАЕТ"},
    {"SHORT", "НЕ ХВАТАЕТ"},
    {"MEAN A", "СРЕДН. A"},
    {"JETTISON", "СБРОС"},

    // ---------------- локальный полёт (localdraw.cpp) ----------------
    {"CONTACT", "КОНТАКТ"},
    {"ALLY", "СОЮЗНИК"},
    {"ENEMY", "ВРАГ"},
    {"FRIENDLY", "ДРУЖЕСТВЕННЫЙ"},
    {"HOSTILE", "ВРАЖДЕБНЫЙ"},
    {"NEUTRAL", "НЕЙТРАЛЬНЫЙ"},
    {"TENSE", "НАПРЯЖЁННЫЙ"},
    {"ESCORT", "ЭСКОРТ"},
    {"HUNTING YOU", "ОХОТИТСЯ ЗА ВАМИ"},
    {"IN PURSUIT", "В ПОГОНЕ"},
    {"PACK HUNT", "СТАЯ"},
    {"UNDER PACK", "ПОД СТАЕЙ"},
    {"BACKUP INBOUND", "ПОДМОГА ИДЁТ"},
    {"WARP OFF", "ВАРП ВЫКЛ"},
    {"WARP", "ВАРП"},
    {"LOCK", "ЗАХВАТ"},
    {"DETECTOR", "ДЕТЕКТОР"},
    {"DIST", "ДИСТ"},
    {"SPD", "СКР"},
    {"SHD", "ЩИТ"},
    {"CLS", "СБЛ"},
    {"SIG", "СИГ"},
    {"ORE", "РУДА"},
    {"GRADE", "СОРТ"},
    {"PRESS K TO DOCK", "СТЫКОВКА (K)"},
    {"PRESS M TO MINE", "ДОБЫЧА (M)"},

    // ---------------- роли и фракции ----------------
    {"TRADER", "ТОРГОВЕЦ"},
    {"SCOUT", "РАЗВЕДЧИК"},
    {"PIRATE", "ПИРАТ"},
    {"COLONIST", "КОЛОНИСТ"},
    {"ADVENTURER", "АВАНТЮРИСТ"},
    {"PLAYER", "ИГРОК"},
    {"MILITARY", "ВОЕННАЯ"},
    {"REFINERY", "ПЕРЕРАБОТКА"},
    {"RESEARCH", "НАУКА"},
    {"RSCH", "НАУКА"},
    {"HABITAT", "ЖИЛАЯ"},
    {"FRONTIER", "ФРОНТИР"},
    {"COURIER", "КУРЬЕР"},
    {"BULK", "БАЛКЕР"},
    {"RAID", "НАЛЁТ"},
    {"DELIVERY", "ДОСТАВКА"},

    // ---------------- хозяйственные функции элементов ----------------
    {"FUSION FUEL", "ТОПЛИВО СИНТЕЗА"},
    {"FISSION FUEL", "ТОПЛИВО ДЕЛЕНИЯ"},
    {"STRUCTURE", "КОНСТРУКЦИИ"},
    {"CONDUCTORS", "ПРОВОДНИКИ"},
    {"CATALYSIS", "КАТАЛИЗ"},
    {"LIFE SUPPORT", "ЖИЗНЕОБЕСПЕЧЕНИЕ"},
    {"REAGENTS", "РЕАГЕНТЫ"},
    {"SHIELDING", "ЗАЩИТА"},
    {"REFRACTORY", "ОГНЕУПОРЫ"},
    {"INERT / NOBLE", "ИНЕРТНЫЕ"},
    // Трёхбуквенные коды — ровно три буквы и в переводе: они стоят в колонке %-4s.
    {"FUS", "СИН"}, {"FIS", "ДЕЛ"}, {"STR", "КОН"}, {"CND", "ПРВ"}, {"CAT", "КАТ"},
    {"LIF", "ЖИЗ"}, {"RCT", "РЕА"}, {"SHD", "ЗАЩ"}, {"RFR", "ОГН"}, {"INR", "ИНЕ"},

    // ---------------- модули и корпуса ----------------
    {"CARGO POD I", "ГРУЗОВОЙ МОДУЛЬ I"},
    {"CARGO POD II", "ГРУЗОВОЙ МОДУЛЬ II"},
    {"CARGO HOLD III", "ГРУЗОВОЙ ТРЮМ III"},
    {"PROPELLANT TANK I", "БАК РАБ. ТЕЛА I"},
    {"PROPELLANT TANK II", "БАК РАБ. ТЕЛА II"},
    {"BUNKER POD I", "ТОПЛИВНЫЙ БУНКЕР I"},
    {"BUNKER POD II", "ТОПЛИВНЫЙ БУНКЕР II"},
    {"ABLATIVE PLATING I", "АБЛЯЦИОННАЯ БРОНЯ I"},
    {"COMPOSITE HULL", "КОМПОЗИТНЫЙ КОРПУС"},
    {"AEGIS BULWARK", "ЩИТ ЭГИДА"},
    {"POINT DEFENSE", "ПРОТИВОРАКЕТНАЯ ОБОРОНА"},
    {"RAILGUN BATTERY", "БАТАРЕЯ РЕЛЬСОТРОНОВ"},
    {"DEEP-FIELD SCANNER", "СКАНЕР ДАЛЬНЕГО ПОЛЯ"},
    {"MINING LASER I", "БУРОВОЙ ЛАЗЕР I"},
    {"MINING LASER II", "БУРОВОЙ ЛАЗЕР II"},
    {"ORE REFINERY RIG", "ОБОГАТИТЕЛЬНАЯ УСТАНОВКА"},
    {"DRILLING RATE", "ТЕМП ДОБЫЧИ"},
    {"ION GRID I", "ИОННАЯ СЕТКА I"},
    {"ION GRID II", "ИОННАЯ СЕТКА II"},
    {"FUSION TORCH", "ТЕРМОЯДЕРНЫЙ ФАКЕЛ"},
    {"MODULE", "МОДУЛЬ"},

    {"ESCAPE POD", "СПАСКАПСУЛА"},
    {"LIGHT COURIER", "ЛЁГКИЙ КУРЬЕР"},
    {"HEAVY COURIER", "ТЯЖЁЛЫЙ КУРЬЕР"},
    {"SMUGGLER RUNABOUT", "КОНТРАБАНДИСТ"},
    {"BLOCKADE RUNNER", "ПРОРЫВАТЕЛЬ БЛОКАД"},
    {"LIGHT FREIGHTER", "ЛЁГКИЙ ГРУЗОВИК"},
    {"MEDIUM FREIGHTER", "СРЕДНИЙ ГРУЗОВИК"},
    {"HEAVY FREIGHTER", "ТЯЖЁЛЫЙ ГРУЗОВИК"},
    {"SUPER-FREIGHTER", "СУПЕРГРУЗОВИК"},
    {"MEGAFREIGHTER", "МЕГАГРУЗОВИК"},
    {"GIGA-FREIGHTER", "ГИГАГРУЗОВИК"},
    {"TERA-FREIGHTER", "ТЕРАГРУЗОВИК"},
    {"MINING BARGE", "ДОБЫВАЮЩАЯ БАРЖА"},
    {"HAULER", "ТЯГАЧ"},
    {"LIGHT FIGHTER", "ЛЁГКИЙ ИСТРЕБИТЕЛЬ"},
    {"HEAVY FIGHTER", "ТЯЖЁЛЫЙ ИСТРЕБИТЕЛЬ"},
    {"INTERCEPTOR", "ПЕРЕХВАТЧИК"},
    {"GUNBOAT", "КАНОНЕРКА"},
    {"CORVETTE", "КОРВЕТ"},
    {"FRIGATE", "ФРЕГАТ"},
    {"DESTROYER", "ЭСМИНЕЦ"},
    {"CRUISER", "КРЕЙСЕР"},
    {"STRIKE CRUISER", "УДАРНЫЙ КРЕЙСЕР"},
    {"BATTLECRUISER", "ЛИНЕЙНЫЙ КРЕЙСЕР"},
    {"HEAVY BOMBER", "ТЯЖЁЛЫЙ БОМБАРДИРОВЩИК"},
    {"ASSAULT CRAFT", "ШТУРМОВИК"},
    {"CARRIER", "АВИАНОСЕЦ"},
    {"DREADNOUGHT", "ДРЕДНОУТ"},
    {"STAR DESTROYER", "ЗВЁЗДНЫЙ РАЗРУШИТЕЛЬ"},
    {"FORTRESS", "КРЕПОСТЬ"},
    {"TITAN", "ТИТАН"},
    {"LEVIATHAN", "ЛЕВИАФАН"},

    // ---------------- элементы таблицы Менделеева ----------------
    {"HYDROGEN", "ВОДОРОД"}, {"HELIUM", "ГЕЛИЙ"}, {"LITHIUM", "ЛИТИЙ"},
    {"BERYLLIUM", "БЕРИЛЛИЙ"}, {"BORON", "БОР"}, {"CARBON", "УГЛЕРОД"},
    {"NITROGEN", "АЗОТ"}, {"OXYGEN", "КИСЛОРОД"}, {"FLUORINE", "ФТОР"},
    {"NEON", "НЕОН"}, {"SODIUM", "НАТРИЙ"}, {"MAGNESIUM", "МАГНИЙ"},
    {"ALUMINIUM", "АЛЮМИНИЙ"}, {"SILICON", "КРЕМНИЙ"}, {"PHOSPHORUS", "ФОСФОР"},
    {"SULFUR", "СЕРА"}, {"CHLORINE", "ХЛОР"}, {"ARGON", "АРГОН"},
    {"POTASSIUM", "КАЛИЙ"}, {"CALCIUM", "КАЛЬЦИЙ"}, {"SCANDIUM", "СКАНДИЙ"},
    {"TITANIUM", "ТИТАН"}, {"VANADIUM", "ВАНАДИЙ"}, {"CHROMIUM", "ХРОМ"},
    {"MANGANESE", "МАРГАНЕЦ"}, {"IRON", "ЖЕЛЕЗО"}, {"COBALT", "КОБАЛЬТ"},
    {"NICKEL", "НИКЕЛЬ"}, {"COPPER", "МЕДЬ"}, {"ZINC", "ЦИНК"},
    {"GALLIUM", "ГАЛЛИЙ"}, {"GERMANIUM", "ГЕРМАНИЙ"}, {"ARSENIC", "МЫШЬЯК"},
    {"SELENIUM", "СЕЛЕН"}, {"BROMINE", "БРОМ"}, {"KRYPTON", "КРИПТОН"},
    {"RUBIDIUM", "РУБИДИЙ"}, {"STRONTIUM", "СТРОНЦИЙ"}, {"YTTRIUM", "ИТТРИЙ"},
    {"ZIRCONIUM", "ЦИРКОНИЙ"}, {"NIOBIUM", "НИОБИЙ"}, {"MOLYBDENUM", "МОЛИБДЕН"},
    {"TECHNETIUM", "ТЕХНЕЦИЙ"}, {"RUTHENIUM", "РУТЕНИЙ"}, {"RHODIUM", "РОДИЙ"},
    {"PALLADIUM", "ПАЛЛАДИЙ"}, {"SILVER", "СЕРЕБРО"}, {"CADMIUM", "КАДМИЙ"},
    {"INDIUM", "ИНДИЙ"}, {"TIN", "ОЛОВО"}, {"ANTIMONY", "СУРЬМА"},
    {"TELLURIUM", "ТЕЛЛУР"}, {"IODINE", "ЙОД"}, {"XENON", "КСЕНОН"},
    {"CAESIUM", "ЦЕЗИЙ"}, {"BARIUM", "БАРИЙ"}, {"LANTHANUM", "ЛАНТАН"},
    {"CERIUM", "ЦЕРИЙ"}, {"PRASEODYMIUM", "ПРАЗЕОДИМ"}, {"NEODYMIUM", "НЕОДИМ"},
    {"PROMETHIUM", "ПРОМЕТИЙ"}, {"SAMARIUM", "САМАРИЙ"}, {"EUROPIUM", "ЕВРОПИЙ"},
    {"GADOLINIUM", "ГАДОЛИНИЙ"}, {"TERBIUM", "ТЕРБИЙ"}, {"DYSPROSIUM", "ДИСПРОЗИЙ"},
    {"HOLMIUM", "ГОЛЬМИЙ"}, {"ERBIUM", "ЭРБИЙ"}, {"THULIUM", "ТУЛИЙ"},
    {"YTTERBIUM", "ИТТЕРБИЙ"}, {"LUTETIUM", "ЛЮТЕЦИЙ"}, {"HAFNIUM", "ГАФНИЙ"},
    {"TANTALUM", "ТАНТАЛ"}, {"TUNGSTEN", "ВОЛЬФРАМ"}, {"RHENIUM", "РЕНИЙ"},
    {"OSMIUM", "ОСМИЙ"}, {"IRIDIUM", "ИРИДИЙ"}, {"PLATINUM", "ПЛАТИНА"},
    {"GOLD", "ЗОЛОТО"}, {"MERCURY", "РТУТЬ"}, {"THALLIUM", "ТАЛЛИЙ"},
    {"LEAD", "СВИНЕЦ"}, {"BISMUTH", "ВИСМУТ"}, {"POLONIUM", "ПОЛОНИЙ"},
    {"ASTATINE", "АСТАТ"}, {"RADON", "РАДОН"}, {"FRANCIUM", "ФРАНЦИЙ"},
    {"RADIUM", "РАДИЙ"}, {"ACTINIUM", "АКТИНИЙ"}, {"THORIUM", "ТОРИЙ"},
    {"PROTACTINIUM", "ПРОТАКТИНИЙ"}, {"URANIUM", "УРАН"}, {"NEPTUNIUM", "НЕПТУНИЙ"},
    {"PLUTONIUM", "ПЛУТОНИЙ"}, {"AMERICIUM", "АМЕРИЦИЙ"}, {"CURIUM", "КЮРИЙ"},
    {"BERKELIUM", "БЕРКЛИЙ"}, {"CALIFORNIUM", "КАЛИФОРНИЙ"}, {"EINSTEINIUM", "ЭЙНШТЕЙНИЙ"},
    {"FERMIUM", "ФЕРМИЙ"}, {"MENDELEVIUM", "МЕНДЕЛЕВИЙ"}, {"NOBELIUM", "НОБЕЛИЙ"},
    {"LAWRENCIUM", "ЛОУРЕНСИЙ"}, {"RUTHERFORDIUM", "РЕЗЕРФОРДИЙ"}, {"DUBNIUM", "ДУБНИЙ"},
    {"SEABORGIUM", "СИБОРГИЙ"}, {"BOHRIUM", "БОРИЙ"}, {"HASSIUM", "ХАССИЙ"},
    {"MEITNERIUM", "МЕЙТНЕРИЙ"}, {"DARMSTADTIUM", "ДАРМШТАДТИЙ"}, {"ROENTGENIUM", "РЕНТГЕНИЙ"},
    {"COPERNICIUM", "КОПЕРНИЦИЙ"}, {"NIHONIUM", "НИХОНИЙ"}, {"FLEROVIUM", "ФЛЕРОВИЙ"},
    {"MOSCOVIUM", "МОСКОВИЙ"}, {"LIVERMORIUM", "ЛИВЕРМОРИЙ"}, {"TENNESSINE", "ТЕННЕССИН"},
    {"OGANESSON", "ОГАНЕСОН"},

    // ---------------- события и состояния (game.cpp, ship.cpp, modules.cpp) ----
    {"cluster seeded", "СКОПЛЕНИЕ ЗАСЕЯНО"},
    {"Player Freehold", "ВЛАДЕНИЕ ИГРОКА"},
    {"Trading licence", "ТОРГОВАЯ ЛИЦЕНЗИЯ"},
    {"route set", "КУРС ЗАДАН"},
    {"route blocked", "МАРШРУТ ЗАКРЫТ"},
    {"buy blocked", "ПОКУПКА ЗАКРЫТА"},
    {"destination", "ЦЕЛЬ"},
    {"ship already en route", "КОРАБЛЬ УЖЕ В ПУТИ"},
    {"propellant short", "НЕ ХВАТАЕТ РАБОЧЕГО ТЕЛА"},
    {"overloaded by", "ПЕРЕГРУЗ НА"},
    {"overloaded", "ПЕРЕГРУЗ"},
    {"overweight", "ПЕРЕВЕС"},
    {"need credits", "НУЖНЫ КРЕДИТЫ"},
    {"need fuel", "НУЖНО ТОПЛИВО"},
    {"need propellant", "НУЖНО РАБОЧЕЕ ТЕЛО"},
    {"needs", "ТРЕБУЕТ"},
    {"cannot retarget in transit", "В ПУТИ ЦЕЛЬ НЕ СМЕНИТЬ"},
    {"cannot transfer in transit", "В ПУТИ ПЕРЕГРУЗКА НЕВОЗМОЖНА"},
    {"cannot refit in transit", "В ПУТИ МОДУЛИ НЕ СМЕНИТЬ"},
    {"cannot mine in transit", "В ПУТИ ДОБЫВАТЬ НЕЛЬЗЯ"},
    {"cannot repair in transit", "В ПУТИ РЕМОНТИРОВАТЬ НЕЛЬЗЯ"},
    {"cannot scan in transit", "В ПУТИ СКАНИРОВАТЬ НЕЛЬЗЯ"},
    {"drive cannot reach", "ДВИГАТЕЛЬ НЕ ДОТЯНЕТ"},
    {"drive tuned for", "ДВИГАТЕЛЬ НАСТРОЕН НА"},
    {"emergency braking", "ЭКСТРЕННОЕ ТОРМОЖЕНИЕ"},
    {"stopped in deep space", "ОСТАНОВКА В ГЛУБОКОМ КОСМОСЕ"},
    {"bunkered fuel", "ТОПЛИВО В БУНКЕР"},
    {"loaded propellant", "РАБОЧЕЕ ТЕЛО ЗАЛИТО"},
    {"jettisoned", "СБРОШЕНО"},
    {"bought system", "СИСТЕМА КУПЛЕНА"},
    {"already yours", "УЖЕ ВАША"},
    {"funded colony", "КОЛОНИЯ ПРОФИНАНСИРОВАНА"},
    {"drew colony profit", "СНЯТА ПРИБЫЛЬ КОЛОНИИ"},
    {"licence acquired - one more hull permitted", "ЛИЦЕНЗИЯ ПОЛУЧЕНА - РАЗРЕШЁН ЕЩЁ ОДИН КОРПУС"},
    {"licence bought back", "ЛИЦЕНЗИЯ ВЫКУПЛЕНА"},
    {"licence is valid", "ЛИЦЕНЗИЯ ДЕЙСТВУЕТ"},
    {"licence renewed", "ЛИЦЕНЗИЯ ПРОДЛЕНА"},
    {"licence revoked - trading frozen", "ЛИЦЕНЗИЯ ОТОЗВАНА - ТОРГОВЛЯ ЗАМОРОЖЕНА"},
    {"trading frozen: licence revoked (buy back for", "ТОРГОВЛЯ ЗАМОРОЖЕНА: ЛИЦЕНЗИЯ ОТОЗВАНА (ВЫКУП ЗА"},
    {"quota already met", "КВОТА УЖЕ ЗАКРЫТА"},
    {"quota settled", "КВОТА ЗАКРЫТА"},
    {"settlement needs", "ДЛЯ РАСЧЁТА НУЖНО"},
    {"buyback needs", "ДЛЯ ВЫКУПА НУЖНО"},
    {"contract accepted", "ЗАКАЗ ПРИНЯТ"},
    {"contract completed", "ЗАКАЗ ВЫПОЛНЕН"},
    {"contract too heavy", "ЗАКАЗ СЛИШКОМ ТЯЖЁЛЫЙ"},
    {"contract wait fuel", "ЗАКАЗ ЖДЁТ ТОПЛИВА"},
    {"late contract", "ЗАКАЗ ПРОСРОЧЕН"},
    {"contract expired", "ЗАКАЗ СГОРЕЛ"},
    {"raid completed", "НАЛЁТ УДАЛСЯ"},
    {"raid failed", "НАЛЁТ ПРОВАЛЕН"},
    {"raid target stale", "ЦЕЛЬ НАЛЁТА УСТАРЕЛА"},
    {"raid contract stale", "ЗАКАЗ НА НАЛЁТ УСТАРЕЛ"},
    {"robbed by", "ОГРАБЛЕН"},
    {"repelled pirate", "ПИРАТ ОТБИТ"},
    {"repelled by", "ОТБИТ"},
    {"destroyed pirate", "ПИРАТ УНИЧТОЖЕН"},
    {"destroyed by", "УНИЧТОЖЕН"},
    {"destroyed", "УНИЧТОЖЕН"},
    {"failed to catch", "НЕ ДОГНАЛ"},
    {"fled from", "СБЕЖАЛ ОТ"},
    {"bounty suppressed", "НАГРАДА СНЯТА"},
    {"scout report sent", "ОТЧЁТ РАЗВЕДКИ ОТПРАВЛЕН"},
    {"signal arrived", "ПРИШЁЛ СИГНАЛ"},
    {"combat report", "БОЕВАЯ СВОДКА"},
    {"contract report", "СВОДКА ПО ЗАКАЗАМ"},
    {"market report", "СВОДКА ПО РЫНКУ"},
    {"owner report", "СВОДКА ПО ВЛАДЕЛЬЦАМ"},
    {"diplomacy", "ДИПЛОМАТИЯ"},
    {"settlement", "РАСЧЁТ"},
    {"awaiting signal", "ЖДЁТ СИГНАЛА"},
    {"watching market", "СЛЕДИТ ЗА РЫНКОМ"},
    {"listening", "СЛУШАЕТ ЭФИР"},
    {"border patrol", "ПАТРУЛЬ ГРАНИЦЫ"},
    {"besieging", "ОСАЖДАЕТ"},
    {"under escort", "ПОД ЭСКОРТОМ"},
    {"escort waiting", "ЭСКОРТ ЖДЁТ"},
    {"save failed", "СОХРАНЕНИЕ НЕ УДАЛОСЬ"},
    {"load failed", "ЗАГРУЗКА НЕ УДАЛАСЬ"},
    {"saved starcluster.save", "СОХРАНЕНО"},
    {"loaded starcluster.save", "ЗАГРУЖЕНО"},
    {"accepted", "ПРИНЯТО"},
    {"completed", "ВЫПОЛНЕНО"},
    {"bought", "КУПЛЕНО"},
    {"equipped", "УСТАНОВЛЕНО"},
    {"already installed", "УЖЕ УСТАНОВЛЕН"},
    {"not in cargo", "НЕТ В ТРЮМЕ"},
    {"to remove", "ЧТОБЫ СНЯТЬ"},
    {"need shipyard lvl", "НУЖНА ВЕРФЬ УР."},
    {"no upgrade slots available", "СВОБОДНЫХ СЛОТОВ НЕТ"},
    {"not enough credits for", "НЕ ХВАТАЕТ КРЕДИТОВ НА"},
    {"not enough credits to repair", "НЕ ХВАТАЕТ КРЕДИТОВ НА РЕМОНТ"},
    {"not enough cargo space for", "В ТРЮМЕ НЕ ХВАТАЕТ МЕСТА ДЛЯ"},
    {"not enough cargo space to unequip", "В ТРЮМЕ НЕ ХВАТАЕТ МЕСТА, ЧТОБЫ СНЯТЬ"},
    {"hull already intact", "КОРПУС И ТАК ЦЕЛ"},
    {"Hull repaired", "КОРПУС ОТРЕМОНТИРОВАН"},
    {"repaired hull", "КОРПУС ОТРЕМОНТИРОВАН"},
    {"no repair facility here", "ЗДЕСЬ НЕТ РЕМОНТНОЙ БАЗЫ"},
    {"hold sold", "ТРЮМ СДАН"},
    {"hold empty", "ТРЮМ ПУСТ"},
    {"lots", "ПАРТИЙ"},
    {"Mining started at", "ДОБЫЧА НАЧАТА В"},
    {"Mining halted", "ДОБЫЧА ОСТАНОВЛЕНА"},
    {"Mining interrupted", "ДОБЫЧА ПРЕРВАНА"},
    {"Anomaly detected", "ОБНАРУЖЕНА АНОМАЛИЯ"},
    {"no anomaly in scan range", "АНОМАЛИЙ В РАДИУСЕ СКАНА НЕТ"},
    {"scanned anomaly", "АНОМАЛИЯ ОТСКАНИРОВАНА"},
    {"Cache recovered", "НАЙДЕН СХРОН"},
    {"Chromocore attuned", "ХРОМОЯДРО НАСТРОЕНО"},
    {"Chromocore vault attuned", "ХРАНИЛИЩЕ ХРОМОЯДЕР НАСТРОЕНО"},
    {"Salvaged derelict", "СНЯТО С ОБЛОМКОВ"},
    {"Rode the ion storm: hull", "ПРОШЛИ ИОННЫЙ ШТОРМ: КОРПУС"},
    {"Exotic matter siphoned from pulsar", "ЭКЗОТИЧЕСКАЯ МАТЕРИЯ СЛИТА С ПУЛЬСАРА"},
    {"prices crash", "ЦЕНЫ ОБВАЛИЛИСЬ"},
    {"prices surge", "ЦЕНЫ ВЗЛЕТЕЛИ"},
    {"Famine", "ГОЛОД"},
    {"Plague", "ЭПИДЕМИЯ"},
    {"TechBoom", "ТЕХБУМ"},
    {"ProductionGlut", "ПЕРЕПРОИЗВОДСТВО"},
    {"ResourceStrike", "СРЫВ ПОСТАВОК"},
    {"deep space", "ГЛУБОКИЙ КОСМОС"},
    {"docked - market open", "В ДОКЕ - РЫНОК ОТКРЫТ"},
    {"market unknown until arrival", "РЫНОК НЕИЗВЕСТЕН ДО ПРИБЫТИЯ"},
    {"entered local flight", "ВХОД В ЛОКАЛЬНЫЙ ПОЛЁТ"},
    {"exited local flight", "ВЫХОД ИЗ ЛОКАЛЬНОГО ПОЛЁТА"},
    {"SWITCH", "СМЕНА"},
    {"anomalies", "АНОМАЛИИ"},
    {"knowledge", "ЗНАНИЕ"},
    {"contracts", "ЗАКАЗЫ"},
    {"CONTRACTS", "ЗАКАЗЫ"},
    {"CONTROLLED", "ПОД КОНТРОЛЕМ"},
    {"FROM", "ОТКУДА"},
    {"TO", "КУДА"},
    {"SUP", "ПРД"},
    {"DEM", "СПР"},
    {"INF", "ИНФ"},
    {"SY", "ВРФ"},
    {"ETA", "ПРИБ"},
};

// ---------------------------------------------------------------------------
// Таблицы поиска
// ---------------------------------------------------------------------------
typedef std::unordered_map<std::string, std::string> Dict;

Dict& exactDict() {
    static Dict d;
    return d;
}

Dict& wordDict() {
    static Dict d;
    return d;
}

// Имена собственные (основы имён систем). Живут ОТДЕЛЬНО от словаря интерфейса
// и наполняются на лету — при генерации скопления и при загрузке сейва.
Dict& properDict() {
    static Dict d;
    return d;
}

// ⚠️ ПОТОКИ. Мир генерируется В ФОНОВОМ ПОТОКЕ (shell.cpp), пока главный рисует
// комикс и переводит его подписи. Именно фоновый поток регистрирует имена
// систем, поэтому таблицы — единственное в i18n, что пишется и читается
// одновременно. Замок берётся один раз на весь разбор строки: translate()
// зовётся только при промахе кэша, так что на кадр приходится считанные
// захваты, а не по одному на каждый токен.
std::mutex& dictMutex() {
    static std::mutex m;
    return m;
}

// Поколение реестра. Кэш переводов у каждого потока свой (иначе ссылка, которую
// вернул tr, могла бы протухнуть под чужой рукой), и общей чистки у него нет —
// вместо неё поток сравнивает поколение и чистит СВОЙ кэш. Без этого запись
// «ЦЕЛЬ: VAREN-417», сделанная до регистрации имени, осталась бы латиницей.
std::atomic<unsigned> gRegistryGeneration(0);

size_t gMaxPhraseWords = 1;

// Слово — пробег из букв, цифр и `_`. Подчёркивание внутри слова важно: имена
// звёзд из СТАРЫХ сейвов (`Star_37`) должны остаться именами собственными, а не
// превратиться в «ЗВЕЗДА_37». Нынешние имена (`Varen-417`) переводятся иначе —
// через реестр имён собственных ниже.
bool wordByte(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

std::string upperAscii(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] >= 'a' && out[i] <= 'z') out[i] = char(out[i] - 'a' + 'A');
    }
    return out;
}

// Сколько «слов» в ключе: разбор оборотов пробует ровно столько токенов подряд.
size_t wordCount(const std::string& key) {
    size_t words = 0;
    bool inWord = false;
    for (size_t k = 0; k < key.size(); ++k) {
        const bool w = wordByte(key[k]);
        if (w && !inWord) ++words;
        inWord = w;
    }
    return words;
}

void buildDicts() {
    Dict& ex = exactDict();
    if (!ex.empty()) return;
    for (size_t i = 0; i < sizeof(EXACT) / sizeof(EXACT[0]); ++i) {
        ex[upperAscii(EXACT[i].en)] = EXACT[i].ru;
    }
    Dict& wd = wordDict();
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); ++i) {
        const std::string key = upperAscii(WORDS[i].en);
        wd[key] = WORDS[i].ru;
        const size_t words = wordCount(key);
        if (words > gMaxPhraseWords) gMaxPhraseWords = words;
    }
}

struct Tok {
    size_t start;
    size_t len;
    bool hasLetter;
};

std::string translate(const std::string& s) {
    // Замок на весь разбор: таблицы читаются здесь и пишутся фоновым потоком,
    // который в этот же момент может регистрировать имена систем.
    std::lock_guard<std::mutex> guard(dictMutex());
    buildDicts();

    // 1. Строка целиком (предложения со знаками препинания, форматы с %s).
    {
        const Dict& ex = exactDict();
        Dict::const_iterator it = ex.find(upperAscii(s));
        if (it != ex.end()) return it->second;
    }

    // 2/3. Обороты и слова.
    std::vector<Tok> toks;
    for (size_t i = 0; i < s.size();) {
        if (!wordByte(s[i])) { ++i; continue; }
        Tok t;
        t.start = i;
        t.hasLetter = false;
        while (i < s.size() && wordByte(s[i])) {
            const char c = s[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) t.hasLetter = true;
            ++i;
        }
        t.len = i - t.start;
        toks.push_back(t);
    }
    if (toks.empty()) return s;

    const Dict& wd = wordDict();
    const Dict& pd = properDict();
    std::string out;
    out.reserve(s.size() + s.size() / 2);
    size_t copied = 0;
    size_t i = 0;
    while (i < toks.size()) {
        out.append(s, copied, toks[i].start - copied);

        size_t bestK = 0;
        const std::string* bestVal = NULL;
        size_t maxK = toks.size() - i;
        if (maxK > gMaxPhraseWords) maxK = gMaxPhraseWords;
        for (size_t k = maxK; k >= 1; --k) {
            // Ключ берётся куском ИСХОДНОЙ строки — от первого слова оборота до
            // последнего, вместе с тем, что стоит между ними. Так совпадают и
            // обороты со знаками внутри («HOLD / TANKS», «ADRIFT - NO PORT»), и
            // при этом «FUEL: 25» не притворяется оборотом: такого ключа просто
            // нет в словаре, и разбор честно откатывается к одному слову.
            const size_t from = toks[i].start;
            const size_t to = toks[i + k - 1].start + toks[i + k - 1].len;
            const std::string key = upperAscii(s.substr(from, to - from));
            Dict::const_iterator it = wd.find(key);
            if (it != wd.end()) { bestK = k; bestVal = &it->second; break; }
            // Имена систем ищутся тем же куском строки: основа «KA'REN» или
            // «VEL-TARRA» — это два токена, и ключом становится всё между ними.
            Dict::const_iterator pit = pd.find(key);
            if (pit != pd.end()) { bestK = k; bestVal = &pit->second; break; }
        }

        if (bestVal) {
            out += *bestVal;
            copied = toks[i + bestK - 1].start + toks[i + bestK - 1].len;
            i += bestK;
        } else {
            out.append(s, toks[i].start, toks[i].len);
            copied = toks[i].start + toks[i].len;
            i += 1;
        }
    }
    out.append(s, copied, std::string::npos);
    return out;
}

}  // namespace

Lang lang() { return gLang; }

void setLang(Lang l) {
    gLang = l;
}

const std::string& tr(const std::string& s) {
    if (gLang == LANG_EN) return s;
    // Кэш: строки с числами меняются каждый кадр, поэтому он растёт. Потолок
    // держим руками — иначе за долгую партию сюда утекут все котировки подряд.
    // Кэш у каждого потока свой: ссылка, которую мы возвращаем, обязана жить,
    // пока её читают, а чужой поток не должен иметь возможности её выбросить.
    static thread_local std::unordered_map<std::string, std::string> memo;
    static thread_local unsigned memoGeneration = 0;
    const unsigned generation = gRegistryGeneration.load();
    if (memoGeneration != generation) { memo.clear(); memoGeneration = generation; }
    if (memo.size() > 8192) memo.clear();
    std::unordered_map<std::string, std::string>::iterator it = memo.find(s);
    if (it != memo.end()) return it->second;
    return memo.insert(std::make_pair(s, translate(s))).first->second;
}

// Совпадают ли спецификаторы формата в паре «английский — русский». snprintf
// берёт аргументы ПО СЧЁТУ, а не по смыслу: стоит переставить в переводе «%s ...
// %.2f» на «%.2f ... %s» — и double читается как указатель, то есть игра падает
// ровно там, где сработала реплика. Замечено на живой правке (совет Тимертии),
// поймано глазами; здесь оно закрыто проверкой, потому что это класс ошибки,
// который тихо переживает и сборку, и запуск на английском.
bool formatSpecsConsistent(std::string* firstBad) {
    static const std::string kFlags = "-+ #0123456789.";
    bool ok = true;
    for (size_t i = 0; i < sizeof(EXACT) / sizeof(EXACT[0]); ++i) {
        const std::string en(EXACT[i].en), ru(EXACT[i].ru);
        std::vector<std::string> a, b;
        for (int pass = 0; pass < 2; ++pass) {
            const std::string& s = pass == 0 ? en : ru;
            std::vector<std::string>& out = pass == 0 ? a : b;
            for (size_t p = 0; p + 1 < s.size(); ++p) {
                if (s[p] != '%') continue;
                if (s[p + 1] == '%') { ++p; continue; }
                size_t q = p + 1;
                while (q < s.size() && kFlags.find(s[q]) != std::string::npos) ++q;
                if (q < s.size()) out.push_back(std::string(1, s[q]));
            }
        }
        if (a != b) {
            ok = false;
            if (firstBad && firstBad->empty()) *firstBad = en;
        }
    }
    return ok;
}

bool isInterfaceWord(const std::string& s) {
    std::lock_guard<std::mutex> guard(dictMutex());
    buildDicts();
    const std::string key = upperAscii(s);
    return wordDict().count(key) != 0 || exactDict().count(key) != 0;
}

void registerProperNoun(const std::string& en, const std::string& ru) {
    std::lock_guard<std::mutex> guard(dictMutex());
    buildDicts();
    const std::string key = upperAscii(en);
    if (key.empty() || ru.empty()) return;
    // Интерфейс всегда важнее имени: если такое слово уже что-то значит, имя
    // сюда не пускаем (генератор имён спрашивает isInterfaceWord заранее, так
    // что до этой ветки доходят только совпадения из старых сейвов).
    if (wordDict().count(key) != 0) return;
    Dict& pd = properDict();
    Dict::iterator it = pd.find(key);
    if (it != pd.end()) {
        if (it->second == ru) return;
        it->second = ru;
    } else {
        pd.insert(std::make_pair(key, ru));
        const size_t words = wordCount(key);
        if (words > gMaxPhraseWords) gMaxPhraseWords = words;
    }
    // Кэши чужих потоков выбросить нельзя — им говорят, что реестр сменился.
    gRegistryGeneration.fetch_add(1);
}

void clearProperNouns() {
    std::lock_guard<std::mutex> guard(dictMutex());
    properDict().clear();
    gRegistryGeneration.fetch_add(1);
}

void loadPreference(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    char buf[16] = {0};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) return;
    if (buf[0] == 'r' || buf[0] == 'R') gLang = LANG_RU;
    else gLang = LANG_EN;
}

void savePreference(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fputs(gLang == LANG_RU ? "ru\n" : "en\n", f);
    std::fclose(f);
}

}  // namespace I18N
