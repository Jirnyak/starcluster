#ifdef __ANDROID__

#include "android_support.h"

#include <sys/stat.h>
#include <cstring>
#include <vector>

namespace AndroidPort {
namespace {

// Список распаковки. Каталог внутри apk перечислить нечем (AAssetManager умеет,
// но SDL его наружу не отдаёт), поэтому имена перечислены руками. Появится новый
// ассет — его надо добавить сюда, иначе на Android его просто не будет.
const char* const ASSETS[] = {
    "timertia.png",
    "comic/slide1.png",
    "comic/slide2.png",
    "comic/slide3.png",
    "comic/slide4.png",
    "comic/slide5.png",
    "music/processed/Static Between Stars.mp3",
    "music/processed/Static Between Stars (1).mp3",
    "music/processed/Static Horizon.mp3",
    "music/processed/Static Horizon (1).mp3",
    "music/processed/Static Horizon (2).mp3",
    "music/processed/Sub-Light Cruise.mp3",
};

std::string g_root;

// Залипание: скан-код -> момент, до которого клавиша считается зажатой.
const Uint32 HOLD_MS = 250;
Uint32 g_holdUntil[SDL_NUM_SCANCODES];
Uint8 g_state[SDL_NUM_SCANCODES];

void makeParentDirs(const std::string& path) {
    // mkdir -p для каталогов из имени файла. Корень уже существует (его создал
    // SDL_GetPrefPath), так что начинаем с первого разделителя после него.
    for (size_t i = g_root.size(); i < path.size(); ++i) {
        if (path[i] != '/') continue;
        const std::string dir = path.substr(0, i);
        mkdir(dir.c_str(), 0755);
    }
}

bool copyOut(const char* relative) {
    const std::string dest = g_root + relative;
    // Уже распаковано — не переписываем: это первый запуск, а не каждый.
    if (FILE* probe = std::fopen(dest.c_str(), "rb")) {
        std::fclose(probe);
        return true;
    }
    // Относительный путь в SDL_RWFromFile на Android читается из ассетов apk.
    SDL_RWops* in = SDL_RWFromFile(relative, "rb");
    if (!in) {
        SDL_Log("starcluster: нет ассета в apk: %s", relative);
        return false;
    }
    makeParentDirs(dest);
    SDL_RWops* out = SDL_RWFromFile(dest.c_str(), "wb");
    if (!out) {
        SDL_Log("starcluster: не открыть на запись: %s", dest.c_str());
        SDL_RWclose(in);
        return false;
    }
    std::vector<char> buf(64 * 1024);
    size_t n;
    while ((n = SDL_RWread(in, buf.data(), 1, buf.size())) > 0) {
        SDL_RWwrite(out, buf.data(), 1, n);
    }
    SDL_RWclose(out);
    SDL_RWclose(in);
    return true;
}

}  // namespace

const std::string& assetRoot() { return g_root; }

void extractAssets() {
    if (!g_root.empty()) return;
    if (char* p = SDL_GetPrefPath("starcluster", "Starcluster")) {
        g_root = p;
        SDL_free(p);
    }
    if (g_root.empty()) return;
    int ok = 0;
    for (size_t i = 0; i < sizeof(ASSETS) / sizeof(ASSETS[0]); ++i) {
        if (copyOut(ASSETS[i])) ++ok;
    }
    SDL_Log("starcluster: ассеты распакованы в %s (%d из %d)", g_root.c_str(), ok,
            (int)(sizeof(ASSETS) / sizeof(ASSETS[0])));
}

void noteKeyDown(SDL_Scancode sc) {
    if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_NUM_SCANCODES) return;
    g_holdUntil[sc] = SDL_GetTicks() + HOLD_MS;
}

const Uint8* keyboardState() {
    const Uint8* real = SDL_GetKeyboardState(NULL);
    std::memcpy(g_state, real, SDL_NUM_SCANCODES);
    const Uint32 now = SDL_GetTicks();
    for (int i = 0; i < SDL_NUM_SCANCODES; ++i) {
        // SDL_GetTicks переполняется через 49 суток; сравнение через вычитание
        // переживает переполнение, прямое `now < until` — нет.
        if (g_holdUntil[i] && (Sint32)(g_holdUntil[i] - now) > 0) g_state[i] = 1;
    }
    return g_state;
}

}  // namespace AndroidPort

#endif  // __ANDROID__
