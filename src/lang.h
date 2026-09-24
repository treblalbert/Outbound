// Interface language. Every user facing string is looked up by its English text,
// so an untranslated string still renders correctly instead of showing a key.
#pragma once
#include <string>

enum LangId : int { LANG_EN, LANG_ES, LANG_COUNT };

namespace L {

void init();                        // builds the lookup and reads the saved choice
int get();
void set(int lang);
bool chosen();                      // false until the player picks one on first launch
void savePref();

const char* nativeName(int lang);   // "English" / "Espanol"
const char* englishName(int lang);

// Translation of `key` into the active language; returns `key` when untranslated.
// By value, so results can be concatenated freely.
std::string t(const char* key);

// Same, with {0} / {1} substituted. Used for the messages that embed numbers.
std::string t1(const char* key, const std::string& a);
std::string t2(const char* key, const std::string& a, const std::string& b);

}  // namespace L

inline std::string T(const char* key) { return L::t(key); }
inline std::string T1(const char* key, const std::string& a) { return L::t1(key, a); }
inline std::string T2(const char* key, const std::string& a, const std::string& b) { return L::t2(key, a, b); }
