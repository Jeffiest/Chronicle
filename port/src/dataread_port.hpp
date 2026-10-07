#pragma once

#include <vector>

#include "common.h"

// The languages the extracted data can run, as LanguageCode numbers, from the languages.json
// dcdata writes (dcdata::SupportedLanguages); none when the data has no such file.
std::vector<s32> DataLanguages();
