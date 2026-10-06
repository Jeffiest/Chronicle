#pragma once

// Text overrides for message files (Windows fork). See mestext.cpp and win-save/mods/TEXT.md. Plain types only.
// Called from LoadFile2 with the bytes just read; returns the new size in bytes (larger when messages were replaced; the
// replacement words are written after the original file, so the buffer must have room for them, which the game's arenas do).
int ModTextPatch(const char *path, void *buffer, int size);

// The menu message set (InitMenuMesSet): in-place replacements only.
void ModTextPatchMenu(void *buffer);
