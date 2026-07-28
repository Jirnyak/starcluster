#pragma once
#include <SDL.h>
#include <string>

// Низкоуровневые 2D-примитивы и битмап-шрифт 5x7. Вынесено из ui.cpp в общий
// модуль, чтобы локальный рендер (localdraw.cpp) мог рисовать текст/панели/круги.
namespace UI {

struct Palette {
    SDL_Color panel = {12, 18, 34, 218};
    SDL_Color panel2 = {22, 30, 52, 230};
    SDL_Color border = {84, 112, 150, 210};
    SDL_Color text = {214, 228, 238, 255};
    SDL_Color dim = {116, 136, 158, 255};
    SDL_Color cyan = {82, 222, 246, 255};
    SDL_Color amber = {245, 191, 78, 255};
    SDL_Color red = {238, 88, 82, 255};
    SDL_Color green = {90, 220, 132, 255};
};

extern const Palette P;

void color(SDL_Renderer* renderer, SDL_Color c);
void fillRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c);
void strokeRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c);
void panel(SDL_Renderer* renderer, int x, int y, int w, int h);
const char* glyph(char ch);
void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color c, int scale = 2);
void bar(SDL_Renderer* renderer, int x, int y, int w, int h, double value, SDL_Color c);

// Круги (растеризация): fill — горизонтальными отрезками, stroke — серединной точкой.
void fillCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color c);
void strokeCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color c);

}
