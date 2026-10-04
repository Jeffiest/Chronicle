#pragma once

// Mod scripting (Lua 5.4) and native plugin host (Windows fork). See script.cpp.
void ScriptTick();                          // once per game tick, from GameRenderTick
void ScriptItemPickup(int *item, int *qty); // from CDngStatusData::GetItem; mods may change the item or quantity
