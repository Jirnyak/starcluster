#include "i18n.h"

#include <cstdio>
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
    {"The local model suggests you buy %s.", "Местная модель советует купить: %s."},
    {"We also have insight that the best place to sell it right now is %s. Remember that name, Master: %s!",
     "Есть данные: лучше всего сбыть это сейчас в системе %s. Запомни это имя, хозяин: %s!"},
    {"Finally, the new technology of applied color superconductivity has produced novel AI cores.",
     "И последнее: новая технология цветовой сверхпроводимости дала ИИ-кристаллы нового типа."},
    {"They are still prototypes and very rare. Be sure to privatise every one you find.",
     "Это пока прототипы, и они очень редки. Приватизируй каждый, который найдёшь."},
    {"By the way, you can also upgrade your vessel and purchase more trading licenses.",
     "Кстати, корабль можно улучшать, а лицензии - докупать."},
    {"I am at your service with more insights at any time, Master. [V]",
     "Я всегда на связи и готова подсказать ещё, хозяин. [V]"},
    {"Care for a market report, Master? Local scans show peak supply of %s at %s, and highest demand for %s at %s.",
     "Сводку по рынку, хозяин? Сканы дают пик предложения %s в %s, а высший спрос на %s - в %s."},
    {"no destination set: open a system and press DESTINATION",
     "цель не задана: откройте систему и нажмите НАЗНАЧЕНИЕ"},

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
    {"NO TRANSACTIONS RECORDED.", "СДЕЛОК ПОКА НЕ БЫЛО."},
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
    {"NO LOCAL CONTRACTS RIGHT NOW", "МЕСТНЫХ КОНТРАКТОВ СЕЙЧАС НЕТ"},
    {"NO VISIBLE CONTRACT SIGNALS", "СИГНАЛОВ О КОНТРАКТАХ НЕ ВИДНО"},
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
    {"TRANSACTION HISTORY", "ИСТОРИЯ СДЕЛОК"},
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
    {"LICENCE REVOKED - BUY BACK", "ЛИЦЕНЗИЯ ОТОЗВАНА - ВЫКУП"},

    // ---------------- владение системой ----------------
    {"BUY THE SYSTEM WHOLE - IT KEEPS LIVING", "СИСТЕМА ПОКУПАЕТСЯ ЦЕЛИКОМ - И ЖИВЁТ ДАЛЬШЕ"},
    {"SYSTEM FOR SALE", "СИСТЕМА ПРОДАЁТСЯ"},
    {"ASKING PRICE", "ЦЕНА"},
    {"PAID IN FULL TO THE SELLING FACTION", "ПОЛНОСТЬЮ ВЫПЛАЧИВАЕТСЯ ФРАКЦИИ-ПРОДАВЦУ"},
    {"YOUR COLONY, ALL PRICES 0", "ВАША КОЛОНИЯ, ВСЕ ЦЕНЫ 0"},
    {"YOURS", "ВАША"},
    {"VS YOURS", "ПРОТИВ ВАШЕГО"},
    {"VS CLUSTER", "ПРОТИВ СКОПЛЕНИЯ"},
    {"FREE MARKET - TAKE AND GIVE AT ZERO", "СВОБОДНЫЙ РЫНОК - БЕРИТЕ И ОТДАВАЙТЕ ПО НУЛЮ"},
    {"COLONY VAULT", "КАЗНА КОЛОНИИ"},
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
    {"contract accepted", "КОНТРАКТ ПРИНЯТ"},
    {"contract completed", "КОНТРАКТ ВЫПОЛНЕН"},
    {"contract too heavy", "КОНТРАКТ СЛИШКОМ ТЯЖЁЛЫЙ"},
    {"contract wait fuel", "КОНТРАКТ ЖДЁТ ТОПЛИВА"},
    {"late contract", "КОНТРАКТ ПРОСРОЧЕН"},
    {"raid completed", "НАЛЁТ УДАЛСЯ"},
    {"raid failed", "НАЛЁТ ПРОВАЛЕН"},
    {"raid target stale", "ЦЕЛЬ НАЛЁТА УСТАРЕЛА"},
    {"raid contract stale", "КОНТРАКТ НА НАЛЁТ УСТАРЕЛ"},
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
    {"contract report", "СВОДКА ПО КОНТРАКТАМ"},
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
    {"contracts", "КОНТРАКТЫ"},
    {"CONTRACTS", "КОНТРАКТЫ"},
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

size_t gMaxPhraseWords = 1;

// Слово — пробег из букв, цифр и `_`. Подчёркивание внутри слова важно: имена
// звёзд вида `Star_37` должны остаться именами собственными, а не превратиться
// в «ЗВЕЗДА_37».
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
        size_t words = 0;
        bool inWord = false;
        for (size_t k = 0; k < key.size(); ++k) {
            const bool w = wordByte(key[k]);
            if (w && !inWord) ++words;
            inWord = w;
        }
        if (words > gMaxPhraseWords) gMaxPhraseWords = words;
    }
}

struct Tok {
    size_t start;
    size_t len;
    bool hasLetter;
};

std::string translate(const std::string& s) {
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
    static std::unordered_map<std::string, std::string> memo;
    if (memo.size() > 8192) memo.clear();
    std::unordered_map<std::string, std::string>::iterator it = memo.find(s);
    if (it != memo.end()) return it->second;
    return memo.insert(std::make_pair(s, translate(s))).first->second;
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
