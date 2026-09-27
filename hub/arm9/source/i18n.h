// Tiny translation helper. Strings are written inline in English and
// Portuguese: TR("Camera", "Câmera").
#pragma once

namespace i18n {
void init();
bool isPt();
void setPt(bool pt); // override (saved in hub.ini)
} // namespace i18n

#define TR(en, pt) (i18n::isPt() ? (pt) : (en))
