// Скриншот-харнес интерфейса: рендерит HUD и каждое окно в BMP через
// ПРОГРАММНЫЙ рендерер SDL (без окна). Нужен, чтобы глазами проверять раскладку
// на обоих языках — русский текст длиннее английского, и переполнение панели
// видно только на картинке.
//
// Запуск: `make uishots` (кладёт uishot_*.bmp; артефакты не коммитятся).

#include <SDL.h>

#include "game.h"
#include "i18n.h"
#include "ui.h"
#include "render2d.h"
#include "exotic.h"

#include <cstdio>
#include <string>

namespace {

const int SCREEN_W = 1440;
const int SCREEN_H = 900;

void shotImpl(const char* name, Game& game, UI::WindowState& ui, const UI::HudSelection& sel, bool novel) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_RGB888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawColor(r, 5, 8, 16, 255);
    SDL_RenderClear(r);
    UI::updateExchangeBoard(ui, game);
    UI::drawHud(r, game, SCREEN_W, SCREEN_H, sel);
    UI::drawWindows(r, game, SCREEN_W, SCREEN_H, sel, ui);
    // Портрет Тимертии не грузим: он рисуется поверх правого края и к раскладке
    // текста отношения не имеет, а без окна SDL картинку неоткуда взять.
    if (novel) UI::drawVisualNovel(r, ui, SCREEN_W, SCREEN_H, NULL);
    SDL_RenderPresent(r);
    const std::string path = std::string("uishot_") + name + ".bmp";
    SDL_SaveBMP(surf, path.c_str());
    std::printf("shot: %s\n", path.c_str());
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
}

void shot(const char* name, Game& game, UI::WindowState& ui, const UI::HudSelection& sel) {
    shotImpl(name, game, ui, sel, false);
}

void vnShot(const char* name, Game& game, UI::WindowState& ui, const UI::HudSelection& sel) {
    shotImpl(name, game, ui, sel, true);
}

}  // namespace

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO);
    const bool ru = !(argc > 1 && std::string(argv[1]) == "en");
    I18N::setLang(ru ? I18N::LANG_RU : I18N::LANG_EN);
    const char* tag = ru ? "ru" : "en";

    Game game;
    game.init(300);
    const int here = game.agents[game.playerAgent].currentStar;

    UI::HudSelection sel;
    sel.star = here;
    sel.agent = game.playerAgent;
    // Груз на борту и выбранный элемент — иначе окно торговли снимается в самом
    // пустом своём состоянии: без рамок трюма, с погашенным SELL ALL и без
    // разбора элемента, то есть ровно без тех строк, которые и переполняются.
    {
        Ship& ps = game.agents[game.playerAgent].ship;
        ps.cargo.emplace_back("Fe", 12.0);
        ps.cargo.emplace_back("Si", 7.0);
        ps.cargo.emplace_back("Au", 1.5);
        sel.element = 25;   // Fe
        sel.star = here;
    }

    // Репутация выставляется РАЗНОЙ у разных фракций (§24): панель фракций и
    // шапка доски заказов на нулях показывают одно слово «НИКТО» и не проверяют
    // ни длину званий, ни ширину шкалы — а русские звания заметно длиннее
    // английских, и вылезают они только на настоящих значениях.
    game.resizeFactionReputation();
    for (size_t f = 0; f < game.factionReputation.size(); ++f) {
        static const double ladder[] = {780.0, 240.0, 61.0, 9.0, 0.0, 1000.0};
        game.factionReputation[f] = ladder[f % 6];
    }

    // Каждое окно снимается отдельно: в каскаде они перекрываются и текст под
    // соседом не прочитать.
    struct Case { const char* name; void (*open)(UI::WindowState&, int, int, int); };
    const Case cases[] = {
        {"system",    &UI::openSystemWindow},
        {"trade",     &UI::openTradeWindow},
        {"contracts", &UI::openContractsWindow},
        {"shipyard",  &UI::openShipyardWindow},
        {"cargo",     &UI::openCargoWindow},
        {"shipfit",   &UI::openShipFitWindow},
        {"exchange",  &UI::openExchangeWindow},
        {"colony",    &UI::openColonyWindow},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        UI::WindowState ui;
        cases[i].open(ui, here, SCREEN_W, SCREEN_H);
        shot((std::string(cases[i].name) + "_" + tag).c_str(), game, ui, sel);
    }
    {
        // Карточка управления (F1) — единственный экран, который новый игрок
        // читает целиком, и единственное место, где написано про клавишу L.
        // Групп стало шесть (добавлено управление внутри системы), и раскладка
        // на три колонки могла не влезть по высоте.
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_RGB888);
        SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
        SDL_SetRenderDrawColor(r, 5, 8, 16, 255);
        SDL_RenderClear(r);
        UI::drawControlsCard(r, SCREEN_W, SCREEN_H);
        SDL_RenderPresent(r);
        const std::string path = std::string("uishot_controls_") + tag + ".bmp";
        SDL_SaveBMP(surf, path.c_str());
        std::printf("shot: %s\n", path.c_str());
        SDL_DestroyRenderer(r);
        SDL_FreeSurface(surf);
    }
    {
        // Биржа держав (§33) снимается в режиме акций и на мире, где книги уже
        // опубликованы, а портфель не пуст: на нулях строка «нет отчёта»
        // ничего не проверяет, а вылезают в русском именно цифры и знак прибыли.
        Game sh;
        sh.seed = 11;
        sh.init(600);
        for (int y = 0; y < 12; ++y) sh.update(1.0);
        sh.agents[sh.playerAgent].money = 9.0e11;
        for (size_t f = 0; f < sh.factions.size(); ++f) {
            sh.publishFactionBook(int(f));
            sh.playerBuyShares(int(f), 900.0 * double(f + 1));
        }
        UI::HudSelection ssel;
        ssel.star = sh.agents[sh.playerAgent].currentStar;
        ssel.agent = sh.playerAgent;
        UI::WindowState ui;
        UI::openExchangeWindow(ui, ssel.star, SCREEN_W, SCREEN_H);
        ui.exchangeShares = true;
        shot((std::string("shares_") + tag).c_str(), sh, ui, ssel);
    }
    {
        // Хайтек-этаж (§31) снимается ОТДЕЛЬНО и на подготовленном мире: окно
        // открывается только там, где рынок экзотики есть вообще, а таких систем
        // около 12%. Игроку выдаётся ячейка и вещество на борт — иначе строки
        // «на борту / цена / кузница» снимаются пустыми, то есть ровно те, что
        // переполняются в русском.
        Game ex;
        ex.seed = 7;
        ex.init(1200);
        int host = -1;
        for (size_t i = 0; i < ex.cluster.stars.size(); ++i) {
            if (ex.exoticMarketAt(int(i))) { host = int(i); break; }
        }
        if (host >= 0) {
            ex.agents[ex.playerAgent].currentStar = host;
            ex.agents[ex.playerAgent].ship.enRoute = false;
            ex.agents[ex.playerAgent].money = 4.2e8;
            ex.agents[ex.playerAgent].ship.containmentLevel = 3;
            ex.agents[ex.playerAgent].ship.platingLayers = 1;
            ex.rebakePlayerBakedBonuses();
            ex.agents[ex.playerAgent].ship.exotic[EX_ANTIMATTER] = 44.0;
            ex.agents[ex.playerAgent].ship.exotic[EX_NEUTRONIUM] = 61.0;
            ex.agents[ex.playerAgent].ship.exotic[EX_CONDENSATE] = 25.0;
            UI::HudSelection esel;
            esel.star = host;
            esel.agent = ex.playerAgent;
            UI::WindowState ui;
            UI::openExoticsWindow(ui, host, SCREEN_W, SCREEN_H);
            shot((std::string("exotics_") + tag).c_str(), ex, ui, esel);
        } else {
            std::printf("EXOTICS: no market found in the probe world\n");
        }
    }
    {
        // Окно собственности снимается ДВАЖДЫ: в списке выше система чужая, и
        // видна только левая колонка с ценой. Своя система показывает кассу и
        // поле имени (§25) — то есть ровно те строки, которые могут вылезти.
        Game owned;
        owned.init(300);
        const int mine = owned.agents[owned.playerAgent].currentStar;
        owned.agents[owned.playerAgent].money = 1.0e15;
        owned.playerBuySystem();
        UI::HudSelection osel;
        osel.star = mine;
        osel.agent = owned.playerAgent;
        osel.element = 25;
        {
            UI::WindowState ui;
            UI::openColonyWindow(ui, mine, SCREEN_W, SCREEN_H);
            shot((std::string("colonyown_") + tag).c_str(), owned, ui, osel);
        }
        {
            UI::WindowState ui;
            UI::openColonyWindow(ui, mine, SCREEN_W, SCREEN_H);
            ui.renameStar = mine;
            ui.renameText = ru ? "\xD0\x94\xD0\xBE\xD0\xBC" : "HOME";  // «Дом»
            shot((std::string("colonyrename_") + tag).c_str(), owned, ui, osel);
        }
    }
    {
        // Журнал снимается ЗАПОЛНЕННЫМ. Пустой он показывал одну строку «ЖУРНАЛ
        // ПУСТ» и не проверял ничего: длинные русские строки заказов вылезают
        // за рамку только на настоящих записях (§23).
        int shown = 0;
        for (const Contract& c : game.contracts) {
            if (shown >= 3) break;
            game.pushJournal(JournalKind::JobAccepted, "TOOK " + game.contractJournalText(c) + " DUE 140Y", 0.0, here);
            ++shown;
        }
        game.pushJournal(JournalKind::Money, std::string(), -1240.0, here);
        if (!game.contracts.empty()) {
            game.pushJournal(JournalKind::JobCompleted,
                game.contractJournalText(game.contracts[0]) + " +1980 Cr", 1980.0, here);
        }
        if (game.contracts.size() > 1) {
            game.pushJournal(JournalKind::JobFailed,
                game.contractJournalText(game.contracts[1]) + " EXPIRED", 0.0, here);
        }
        UI::WindowState ui;
        UI::openTransactionsWindow(ui, SCREEN_W, SCREEN_H);
        shot((std::string("log_") + tag).c_str(), game, ui, sel);
    }
    {
        // Вступительная новелла Тимертии. Коробка диалога — фиксированные 150 px:
        // текст начинается на +60 и живёт в пяти строках по 16 px. Русская реплика
        // длиннее английской, поэтому каждый шаг прогоняется и меряется; на экран
        // сохраняется самый длинный — тот, который сломается первым.
        // Меряем по УЗКОМУ окну, а не по снимаемому: на 1440 px в строку влезает
        // 113 символов и не переполнится ничего, а игра запускается и в 960.
        const int NARROW_W = 960;
        const int maxChars = (NARROW_W - 40 - 40) / 12;
        const int maxLines = 5;
        int worstStep = 0, worstLines = 0;
        {
            UI::WindowState ui;
            ui.vnState.active = true;
            UI::updateVisualNovel(ui, game, 0.0, SCREEN_W, SCREEN_H);
            for (int guard = 0; guard < 400 && ui.vnState.active; ++guard) {
                const int step = ui.vnState.tutorialStep;
                const std::string wrapped = UI::wrapText(ui.vnState.targetText, maxChars);
                int lines = 1;
                for (size_t i = 0; i < wrapped.size(); ++i) {
                    if (wrapped[i] == '\n') ++lines;
                }
                if (lines > worstLines) { worstLines = lines; worstStep = step; }
                if (lines > maxLines) std::printf("VN OVERFLOW: step %d takes %d lines\n", step, lines);
                UI::advanceVisualNovel(ui, game, SCREEN_W, SCREEN_H);  // дописать реплику
                UI::advanceVisualNovel(ui, game, SCREEN_W, SCREEN_H);  // следующий шаг
            }
        }
        // Шаги 110..112 — хайтек-лента (§37.2). Живут вне ленты обучения и в
        // прогон выше не попадают, а реплики у них длинные: меряем отдельно и
        // по тому же узкому окну.
        {
            for (int step = 110; step <= 112; ++step) {
                const std::string wrapped = UI::wrapText(UI::tutorialLine(game, step), maxChars);
                int lines = 1;
                for (size_t i = 0; i < wrapped.size(); ++i) if (wrapped[i] == '\n') ++lines;
                if (lines > worstLines) { worstLines = lines; worstStep = step; }
                if (lines > maxLines) std::printf("VN OVERFLOW: step %d takes %d lines\n", step, lines);
            }
        }

        // Шаг 100 — сводка по прибытии — в этот прогон НЕ попадает: он живёт вне
        // ленты обучения и включается по приходу в новую систему. А реплика у
        // него самая длинная в игре (цена здесь, цена там, имя системы, дальность,
        // годы и прибыль в одной строке), поэтому меряется отдельно и в ОБОИХ
        // исходах: с разведкой (есть что советовать) и без неё (сравнивать не с чем).
        {
            const bool surveyed[2] = {false, true};
            for (int s = 0; s < 2; ++s) {
                UI::WindowState ui;
                ui.vnState.active = true;
                ui.vnState.tutorialCompleted = true;
                Game probe;
                probe.seed = game.seed;
                probe.init(300);   // тот же размер, что и у сцены выше
                for (int y = 0; y < 20; ++y) probe.update(1.0);
                if (surveyed[s]) {
                    for (int i = 0; i < int(probe.cluster.stars.size()) && i < 400; ++i)
                        probe.observeMarketForFaction(probe.playerFaction, i);
                }
                UI::updateVisualNovel(ui, probe, 0.0, SCREEN_W, SCREEN_H);
                const std::string wrapped = UI::wrapText(ui.vnState.targetText, maxChars);
                int lines = 1;
                for (size_t i = 0; i < wrapped.size(); ++i) if (wrapped[i] == '\n') ++lines;
                if (lines > worstLines) { worstLines = lines; worstStep = 100; }
                std::printf("vn: step 100 %-8s %d/%d lines\n",
                            surveyed[s] ? "surveyed" : "blind", lines, maxLines);
                if (lines > maxLines)
                    std::printf("VN OVERFLOW: step 100 (%s) takes %d lines\n",
                                surveyed[s] ? "surveyed" : "blind", lines);
            }
        }
        // Шаги 101–102 — глубокий просчёт по V (§28) и отказ на сухом бункере.
        // Меряются тем же способом: реплика 101 длиннее сводки по прибытии, а
        // рождается она только по нажатию клавиши, то есть мимо ленты обучения.
        {
            const bool dry[2] = {false, true};
            for (int d = 0; d < 2; ++d) {
                Game probe;
                probe.seed = game.seed;
                probe.init(300);
                for (int y = 0; y < 20; ++y) probe.update(1.0);
                if (dry[d]) probe.agents[probe.playerAgent].ship.fuel.clear();
                UI::WindowState ui;
                ui.vnState.tutorialCompleted = true;
                ui.vnState.active = false;
                UI::toggleVisualNovel(ui, probe);
                const std::string wrapped = UI::wrapText(ui.vnState.targetText, maxChars);
                int lines = 1;
                for (size_t i = 0; i < wrapped.size(); ++i) if (wrapped[i] == '\n') ++lines;
                if (lines > worstLines) { worstLines = lines; worstStep = ui.vnState.tutorialStep; }
                std::printf("vn: step %d %-8s %d/%d lines\n",
                            ui.vnState.tutorialStep, dry[d] ? "dry" : "fuelled", lines, maxLines);
                if (lines > maxLines)
                    std::printf("VN OVERFLOW: step %d takes %d lines\n", ui.vnState.tutorialStep, lines);
            }
        }
        std::printf("vn: worst step %d, %d/%d lines\n", worstStep, worstLines, maxLines);

        // ПАНЕЛЬ ЦЕЛЕЙ (§46). Меряем КАЖДУЮ ступень лестницы, а не то, что видно
        // на нулевом году: панель показывает окно вокруг первой незакрытой
        // ступени, поэтому снимок никогда не покажет поздние ступени — а
        // вылезали за рамку именно они. Бюджет: панель 248 px, текст с +34.
        {
            const int OBJ_PANEL_W = 248;
            const int OBJ_TEXT_X = 34;
            const int budget = OBJ_PANEL_W - OBJ_TEXT_X - 6;   // 6 px поля справа
            // Мир, где ВСЕ ступени уже видны: харнесу нужен полный список, а не
            // окно из шести строк. `objectiveLadder` отдаёт лестницу целиком.
            const std::vector<UI::ObjectiveStep> ladder = UI::objectiveLadder(game);
            int worstW = 0;
            const char* worstText = "";
            for (size_t i = 0; i < ladder.size(); ++i) {
                const int wpx = UI::textWidth(ladder[i].text, 1);
                if (wpx > worstW) { worstW = wpx; worstText = ladder[i].text; }
                if (wpx > budget)
                    std::printf("OBJECTIVE OVERFLOW: \"%s\" takes %d px of %d\n",
                                ladder[i].text, wpx, budget);
            }
            std::printf("objectives: %d steps, worst %d/%d px (\"%s\")\n",
                        int(ladder.size()), worstW, budget, worstText);
        }

        // Два снимка: самая длинная реплика (проверка коробки) и топливный шаг —
        // там новелла САМА открывает окно ТРЮМ/БАКИ, и видно, не легла ли коробка
        // диалога поверх того самого окна, о котором Тимертия рассказывает.
        const int VN_FUEL_STEP = 16;   // ручки ТЯГА и КРЕЙСЕР
        const int shots[2] = {worstStep, VN_FUEL_STEP};
        const char* names[2] = {"vn_", "vnfuel_"};
        for (int s = 0; s < 2; ++s) {
            UI::WindowState ui;
            ui.vnState.active = true;
            UI::updateVisualNovel(ui, game, 0.0, SCREEN_W, SCREEN_H);
            while (ui.vnState.active && ui.vnState.tutorialStep < shots[s]) {
                UI::advanceVisualNovel(ui, game, SCREEN_W, SCREEN_H);
                UI::advanceVisualNovel(ui, game, SCREEN_W, SCREEN_H);
            }
            UI::advanceVisualNovel(ui, game, SCREEN_W, SCREEN_H);  // машинка до конца строки
            vnShot((std::string(names[s]) + tag).c_str(), game, ui, sel);
        }
    }
    {
        UI::WindowState ui;
        shot((std::string("hud_") + tag).c_str(), game, ui, sel);
    }
    SDL_Quit();
    return 0;
}
