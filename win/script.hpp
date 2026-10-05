#pragma once

// Mod scripting (Lua 5.4) and native plugin host (Windows fork). See script.cpp.
void ScriptTick();                                                  // once per game tick, from GameRenderTick
void ScriptItemPickup(int *item, int *qty);                         // from CDngStatusData::GetItem
void ScriptMonsterHit(int index, int attacker, int element, int *amount); // from CMonstorUnit::CheckDmg, before HP drops
void ScriptMonsterKilled(int index, int attacker);                  // from CMonstorUnit::CheckDmg
void ScriptPlayerLife(int chara, short *amount);                    // from CUserStatus::AddNowLife (negative = damage)
void ScriptSaveSlot(int slot, int saving);                          // from the save menu: slot chosen to save to (1) or load (0)
