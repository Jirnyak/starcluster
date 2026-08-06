#include "render2d.h"
#include "i18n.h"
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

// ---------------------------------------------------------------------------
// Кириллица в том же растре 5x7
//
// Шрифт был чисто заглавный ASCII, и русская локализация упиралась именно в
// него: перевести строки мало, их нечем нарисовать. Регистр складываем как и
// раньше — строчные приводим к заглавным (в 5x7 строчные всё равно не живут).
// ---------------------------------------------------------------------------
const char* cyrillicGlyph(unsigned int cp) {
    switch (cp) {
    case 0x0410: return "01110100011000111111100011000110001";  // А
    case 0x0411: return "11111100001000011110100011000111110";  // Б
    case 0x0412: return "11110100011000111110100011000111110";  // В
    case 0x0413: return "11111100001000010000100001000010000";  // Г
    case 0x0414: return "01110010100101001010010101111110001";  // Д
    case 0x0415: return "11111100001111010000100001000011111";  // Е
    case 0x0401: return "01010111111000011110100001000011111";  // Ё
    case 0x0416: return "10101101011010111111101011010110101";  // Ж
    case 0x0417: return "01110100010000100110000011000101110";  // З
    case 0x0418: return "10001100011001110101110011000110001";  // И
    case 0x0419: return "01010100011000110011101011100110001";  // Й
    case 0x041A: return "10001100101010011000101001001010001";  // К
    case 0x041B: return "00111001010010100101001010100110001";  // Л
    case 0x041C: return "10001110111010110101100011000110001";  // М
    case 0x041D: return "10001100011000111111100011000110001";  // Н
    case 0x041E: return "01110100011000110001100011000101110";  // О
    case 0x041F: return "11111100011000110001100011000110001";  // П
    case 0x0420: return "11110100011000111110100001000010000";  // Р
    case 0x0421: return "01111100001000010000100001000001111";  // С
    case 0x0422: return "11111001000010000100001000010000100";  // Т
    case 0x0423: return "10001100011000101111000010001001100";  // У
    case 0x0424: return "00100011101010110101101010111000100";  // Ф
    case 0x0425: return "10001100010101000100010101000110001";  // Х
    case 0x0426: return "10010100101001010010100101111100001";  // Ц
    case 0x0427: return "10001100011000101111000010000100001";  // Ч
    case 0x0428: return "10101101011010110101101011010111111";  // Ш
    case 0x0429: return "10101101011010110101101011111100001";  // Щ
    case 0x042A: return "11000010000100001110010010100101110";  // Ъ
    case 0x042B: return "10001100011000111001101011010111001";  // Ы
    case 0x042C: return "10000100001000011110100011000111110";  // Ь
    case 0x042D: return "11110000010000101111000010000111110";  // Э
    case 0x042E: return "10010101011010111101101011010110010";  // Ю
    case 0x042F: return "01111100011000101111001010100110001";  // Я
    default: return NULL;
    }
}

const char* glyphCp(unsigned int cp) {
    // Строчная кириллица складывается в заглавную: а-я → А-Я, ё → Ё.
    if (cp >= 0x0430 && cp <= 0x044F) cp -= 0x20;
    if (cp == 0x0451) cp = 0x0401;
    if (const char* bits = cyrillicGlyph(cp)) return bits;
    if (cp >= 'a' && cp <= 'z') cp = cp - 'a' + 'A';
    if (cp < 128) return glyph(char(cp));
    return glyph('\1');  // «тофу» из ветки default
}

namespace {

// Разбор одной кодовой точки UTF-8. Возвращает число съеденных байт (>=1),
// чтобы битая последовательность не зациклила обход строки.
size_t decodeUtf8(const std::string& s, size_t i, unsigned int& cp) {
    const unsigned char b0 = (unsigned char)s[i];
    if (b0 < 0x80) { cp = b0; return 1; }
    const size_t left = s.size() - i;
    if ((b0 & 0xE0) == 0xC0 && left >= 2) {
        cp = ((b0 & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && left >= 3) {
        cp = ((b0 & 0x0Fu) << 12) | (((unsigned char)s[i + 1] & 0x3Fu) << 6)
           | ((unsigned char)s[i + 2] & 0x3Fu);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && left >= 4) {
        cp = ((b0 & 0x07u) << 18) | (((unsigned char)s[i + 1] & 0x3Fu) << 12)
           | (((unsigned char)s[i + 2] & 0x3Fu) << 6) | ((unsigned char)s[i + 3] & 0x3Fu);
        return 4;
    }
    cp = b0;
    return 1;
}

}  // namespace

size_t textLength(const std::string& text) {
    size_t n = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned int cp = 0;
        i += decodeUtf8(text, i, cp);
        ++n;
    }
    return n;
}

std::string textPrefix(const std::string& text, size_t chars) {
    size_t n = 0;
    for (size_t i = 0; i < text.size();) {
        if (n >= chars) return text.substr(0, i);
        unsigned int cp = 0;
        i += decodeUtf8(text, i, cp);
        ++n;
    }
    return text;
}

int textWidth(const std::string& text, int scale) {
    const std::string& s = I18N::tr(text);
    size_t best = 0, cur = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned int cp = 0;
        i += decodeUtf8(s, i, cp);
        if (cp == '\n') { best = std::max(best, cur); cur = 0; continue; }
        ++cur;
    }
    best = std::max(best, cur);
    return int(best) * 6 * scale;
}

void drawText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color c, int scale) {
    // Единая точка перевода: любая надпись игры проходит здесь, поэтому
    // локализация не требует править сотни мест вызова (см. i18n.h).
    const std::string& s = I18N::tr(text);
    color(renderer, c);
    int penX = x;
    int penY = y;
    for (size_t i = 0; i < s.size();) {
        unsigned int cp = 0;
        i += decodeUtf8(s, i, cp);
        if (cp == '\n') {
            penX = x;
            penY += 8 * scale;
            continue;
        }
        const char* bits = glyphCp(cp);
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
