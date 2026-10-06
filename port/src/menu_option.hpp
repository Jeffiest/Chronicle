#pragma once

#include "platform/config.hpp"

class CSaveData;

// Whether the Options screen is open, from InitMenuOption until MenuOptionKey reports it closed.
// The host keeps the interface at 100% meanwhile: video.ui_scale applies from when it closes.
bool MenuOptionOpen();

// Puts the game's options from config.json into the save, where the game reads them (its
// configuration words, the dungeon map's status and the menu cursors' reset flag), and sets the
// sound's stereo mode. A loaded save brings its own copy, so this runs before every mode starts
// and whenever config.json's options change.
void GameOptionsApply(const ConfigGameOptions &options);

// The change hook that calls GameOptionsApply.
void GameOptionsChanged(const Config &before, const Config &after);

// Zeros the option fields reserved in a serialized save's fixed layout. Only the copy about to
// be written is cleared; the running game keeps config.json's options.
void GameOptionsClear(CSaveData &save);
