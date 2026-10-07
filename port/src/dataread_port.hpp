#pragma once

#include "common.h"

// Whether the extracted data is the NTSC disc's (dcdata::IsNtscLayout over the data index).
bool DataIsNtsc();

// The English the data's disc shipped with: American on NTSC, British on PAL.
s32 DataEnglishLanguage();
