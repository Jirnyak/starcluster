// Android-слой поверх игры. Проверка концепции: экранная клавиатура вместо
// физической, ассеты из APK, залипание синтетических нажатий.
//
// Файл собирается ТОЛЬКО в Android-сборке (android/app/jni/src/Android.mk) и
// целиком спрятан за __ANDROID__: на маке и в Windows его как будто нет.
#pragma once
#ifdef __ANDROID__

#include <SDL.h>
#include <string>

namespace AndroidPort {

// Каталог во внутреннем хранилище, куда распакованы ассеты из APK. Заканчивается
// разделителем. Пуст, пока не отработал extractAssets().
const std::string& assetRoot();

// Ассеты лежат ВНУТРИ apk (это zip, а не каталог), и обычный fopen их не видит:
// assetPath() в main.cpp ходит через fopen, музыка перечисляется через dirent.
// Поэтому один раз на установку копируем их наружу, в SDL_GetPrefPath, и дальше
// вся игра работает с обычными файлами и ничего не знает про Android.
void extractAssets();

// Полёт опрашивает УДЕРЖАНИЕ клавиш (SDL_GetKeyboardState в main.cpp), а
// экранная клавиатура шлёт нажатие и отпускание в одном кадре — тяга не успела
// бы включиться ни разу. Держим синтетическое нажатие HOLD_MS миллисекунд:
// тап по «W» = короткий импульс двигателя.
void noteKeyDown(SDL_Scancode sc);

// Замена SDL_GetKeyboardState: настоящее состояние клавиатуры, поверх которого
// подмешаны ещё не истёкшие залипшие клавиши.
const Uint8* keyboardState();

}  // namespace AndroidPort

#endif  // __ANDROID__
