#include "render2d.h"
#include <algorithm>
#include <cmath>

// Определения примитивов. Перенесено дословно из ui.cpp (анонимный namespace),
// теперь во внешнем namespace UI, чтобы линковаться из localdraw.cpp.
namespace UI {

const Palette P;

void color(SDL_Renderer* renderer, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
}

void fillRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect r = {x, y, w, h};
    color(renderer, c);
    SDL_RenderFillRect(renderer, &r);
}

void strokeRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect r = {x, y, w, h};
    color(renderer, c);
    SDL_RenderDrawRect(renderer, &r);
}

void panel(SDL_Renderer* renderer, int x, int y, int w, int h) {
    fillRect(renderer, x, y, w, h, P.panel);
    strokeRect(renderer, x, y, w, h, P.border);
    fillRect(renderer, x + 1, y + 1, w - 2, 1, {128, 174, 210, 65});
}

const char* glyph(char ch) {
    switch (ch) {
    case 'A': return "01110100011000111111100011000110001";
    case 'B': return "11110100011000111110100011000111110";
    case 'C': return "01111100001000010000100001000001111";
    case 'D': return "11110100011000110001100011000111110";
    case 'E': return "11111100001111010000100001000011111";
    case 'F': return "11111100001111010000100001000010000";
    case 'G': return "01111100001000010111100011000101111";
    case 'H': return "10001100011000111111100011000110001";
    case 'I': return "11111001000010000100001000010011111";
    case 'J': return "00111000100001000010100101001001100";
    case 'K': return "10001100101010011000101001001010001";
    case 'L': return "10000100001000010000100001000011111";
    case 'M': return "10001110111010110101100011000110001";
    case 'N': return "10001110011010110011100011000110001";
    case 'O': return "01110100011000110001100011000101110";
    case 'P': return "11110100011000111110100001000010000";
    case 'Q': return "01110100011000110001101011001001101";
    case 'R': return "11110100011000111110101001001010001";
    case 'S': return "01111100001000001110000010000111110";
    case 'T': return "11111001000010000100001000010000100";
    case 'U': return "10001100011000110001100011000101110";
    case 'V': return "10001100011000110001100010101000100";
    case 'W': return "10001100011000110101101011101110001";
    case 'X': return "10001100010101000100010101000110001";
    case 'Y': return "10001100010101000100001000010000100";
    case 'Z': return "11111000010001000100010001000011111";
    case '0': return "01110100011001110101110011000101110";
    case '1': return "00100011000010000100001000010001110";
    case '2': return "01110100010000100010001000100011111";
    case '3': return "11110000010000101110000010000111110";
    case '4': return "00010001100101010010111110001000010";
    case '5': return "11111100001111000001000010000111110";
    case '6': return "00110010001000011110100011000101110";
    case '7': return "11111000010001000100010000100001000";
    case '8': return "01110100011000101110100011000101110";
    case '9': return "01110100011000101111000010001001100";
    case '-': return "00000000000000011111000000000000000";
    case '+': return "00000001000010011111001000010000000";
        case ',': return "00000000000000000000001100011001000";
    case '!': return "00100001000010000100000000010000000";
    case '\'': return "01000010000100000000000000000000000";
case '.': return "00000000000000000000000000110001100";
    case ':': return "00000011000110000000011000110000000";
    case '/': return "00001000010001000100010001000010000";
    case '%': return "11001000010001000100010001000010011";
    case '[': return "01110010000100001000010000100001110";
    case ']': return "01110000100001000010000100001001110";
    case '>': return "10000010000010000010001001000010000";
    case '<': return "00001000100010001000001000010000001";
    case '(': return "00110010001000010000100000100000110";
    case ')': return "01100000100000100001000010001001100";
    case '|': return "00100001000010000100001000010000100";
    case '#': return "01010010101111101010111110101001010";
    case '@': return "01110100011011110101101111000001110";
    case '=': return "00000000001111100000111110000000000";
    case '~': return "00000000000100110110000000000000000";
    case ' ': return "00000000000000000000000000000000000";
    case '_': return "00000000000000000000000000000011111";
    case '?': return "11110000010001000100000000000000100";
    default: return "11111000010001000100010000000000100";
    }
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color c, int scale) {
    color(renderer, c);
    int penX = x;
    int penY = y;
    for (char raw : text) {
        if (raw == '\n') {
            penX = x;
            penY += 8 * scale;
            continue;
        }
        char ch = raw;
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        const char* bits = glyph(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (bits[row * 5 + col] == '1') {
                    SDL_Rect r = {penX + col * scale, penY + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &r);
                }
            }
        }
        penX += 6 * scale;
    }
}

void bar(SDL_Renderer* renderer, int x, int y, int w, int h, double value, SDL_Color c) {
    fillRect(renderer, x, y, w, h, {8, 12, 22, 230});
    const double v = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    const int filled = std::max(0, std::min(w, int(std::round(w * v))));
    if (filled > 0) fillRect(renderer, x, y, filled, h, c);
    strokeRect(renderer, x, y, w, h, {76, 96, 124, 220});
}

void fillCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color c) {
    color(renderer, c);
    if (radius <= 0) { SDL_RenderDrawPoint(renderer, cx, cy); return; }
    for (int dy = -radius; dy <= radius; ++dy) {
        const int span = int(std::sqrt(double(radius * radius - dy * dy)) + 0.5);
        SDL_RenderDrawLine(renderer, cx - span, cy + dy, cx + span, cy + dy);
    }
}

void strokeCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color c) {
    color(renderer, c);
    if (radius <= 0) { SDL_RenderDrawPoint(renderer, cx, cy); return; }
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        SDL_RenderDrawPoint(renderer, cx + x, cy + y);
        SDL_RenderDrawPoint(renderer, cx + y, cy + x);
        SDL_RenderDrawPoint(renderer, cx - y, cy + x);
        SDL_RenderDrawPoint(renderer, cx - x, cy + y);
        SDL_RenderDrawPoint(renderer, cx - x, cy - y);
        SDL_RenderDrawPoint(renderer, cx - y, cy - x);
        SDL_RenderDrawPoint(renderer, cx + y, cy - x);
        SDL_RenderDrawPoint(renderer, cx + x, cy - y);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

}
