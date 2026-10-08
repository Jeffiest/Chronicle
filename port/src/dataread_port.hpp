#pragma once

#include <vector>

#include "common.h"

// The languages the extracted disc carries, as LanguageCode numbers, from the languages.json dcdata
// writes for its release (dcdata::ReleaseOf); none when the data has no such file.
std::vector<s32> DataLanguages();

// The languages the game can offer: the disc's own. A localization pack, once the port has them,
// adds the languages it carries here.
std::vector<s32> SupportedLanguages();
