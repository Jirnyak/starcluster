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

#include <cstdio>
#include <string>

namespace {

const int SCREEN_W = 1440;
const int SCREEN_H = 900;

void shot(const char* name, Game& game, UI::WindowState& ui, const UI::HudSelection& sel) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_RGB888);
    SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawColor(r, 5, 8, 16, 255);
    SDL_RenderClear(r);
    UI::updateExchangeBoard(ui, game);
    UI::drawHud(r, game, SCREEN_W, SCREEN_H, sel);
    UI::drawWindows(r, game, SCREEN_W, SCREEN_H, sel, ui);
    SDL_RenderPresent(r);
    const std::string path = std::string("uishot_") + name + ".bmp";
    SDL_SaveBMP(surf, path.c_str());
    std::printf("shot: %s\n", path.c_str());
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
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
        UI::WindowState ui;
        UI::openTransactionsWindow(ui, SCREEN_W, SCREEN_H);
        shot((std::string("log_") + tag).c_str(), game, ui, sel);
    }
    {
        UI::WindowState ui;
        shot((std::string("hud_") + tag).c_str(), game, ui, sel);
    }
    SDL_Quit();
    return 0;
}
