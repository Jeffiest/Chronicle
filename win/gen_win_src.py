#!/usr/bin/env python3
"""Generate the Windows source tree Chronicle-win-src/ from the pristine upstream copy Chronicle-src/.
 - copies ps2/src, ps2/include, port/include, port/src (tests excluded)
 - rewrites C/C++ `long` (and `unsigned long`, `long int`) to 64-bit `long long` outside strings/comments, in game code
   and replacement units (not platform/gfx/audio): the PS2 and Linux x64 have 64-bit long, Windows has 32-bit
 - swaps in the Windows arena memory unit, writes the Windows CMakeLists.txt and the compiler launcher
Upstream files in Chronicle-src/ are never modified."""
import re, shutil, sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
DST = SRC.parent / 'Chronicle-win-src'
WIN = SRC / 'win'

CODE_EXT = {'.cpp', '.c', '.h', '.hpp', '.hh', '.inc'}
HOST_DIRS = ('port/src/platform/', 'port/src/gfx/', 'port/src/audio/')
NONCODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
LONG = re.compile(r'\b(?:(unsigned|signed)\s+)?long\b(?:\s+(long|int|double)\b)?')

def long_sub(m):
    if m.group(2) in ('long', 'double'):
        return m.group(0)
    return (m.group(1) + ' ' if m.group(1) else '') + 'long long'

def rewrite_long(text):
    out, last, n = [], 0, 0
    def code(chunk):
        nonlocal n
        new, k = LONG.subn(long_sub, chunk)
        n += sum(1 for m in LONG.finditer(chunk) if m.group(2) not in ('long', 'double'))
        return new
    for m in NONCODE.finditer(text):
        out.append(code(text[last:m.start()])); out.append(m.group(0)); last = m.end()
    out.append(code(text[last:]))
    return ''.join(out), n


# Windows source patches: (file, old, new). Each `old` must occur; a mismatch means upstream changed and the patch needs review.
PATCHES = [
    ('port/src/platform/input.cpp', '{"rx",           ActionKind::Axis,     0,              kAxisRightX, 0,  "MouseX"},',
     '{"rx",           ActionKind::Axis,     0,              kAxisRightX, 0,  ""},'),
    ('port/src/platform/input.cpp', '{"ry",           ActionKind::Axis,     0,              kAxisRightY, 0,  "-MouseY"},',
     '{"ry",           ActionKind::Axis,     0,              kAxisRightY, 0,  ""},'),
    ('port/src/dun/movechara.cpp', '                                                            float turn = GamePad.GetRXf();\n\n                                                            NowCamera__3->AddHeight(-GamePad.GetRYf());\n',
     '                                                            float turn = GamePad.GetRXf();\n                                                            float mouse_turn = 0.0f, mouse_height = 0.0f;\n                                                            { extern void ModsMouseLook(float *, float *); ModsMouseLook(&mouse_turn, &mouse_height); }\n\n                                                            NowCamera__3->AddHeight(-GamePad.GetRYf() + mouse_height);\n'),
    ('port/src/dun/movechara.cpp', '                                                            NowCamera__3->AddAngle(0.04f * -turn);\n',
     '                                                            NowCamera__3->AddAngle(0.04f * -turn + mouse_turn);\n'),
    ('ps2/src/editloop.cpp', '    float horizontal = GamePad.GetRXf();\n    camera->AddHeight(-GamePad.GetRYf());\n',
     '    float horizontal = GamePad.GetRXf();\n    float mouse_turn = 0.0f, mouse_height = 0.0f;\n    { extern void ModsMouseLook(float *, float *); ModsMouseLook(&mouse_turn, &mouse_height); }\n    camera->AddHeight(-GamePad.GetRYf() + mouse_height);\n'),
    ('ps2/src/editloop.cpp', '    camera->AddAngle(0.03f * -horizontal);\n',
     '    camera->AddAngle(0.03f * -horizontal + mouse_turn);\n'),
    ('port/src/clsmes.cpp', 'void Myset2DSprite(sceVif1Packet *packet, CTexture *texture, const CRect_i_ &src, const CRect_i_ &dst, u8 r,\n                   u8 g, u8 b, u8 a) {\n    if (texture == nullptr) {\n        return;\n    }\n',
     'void Myset2DSprite(sceVif1Packet *packet, CTexture *texture, const CRect_i_ &src, const CRect_i_ &dst, u8 r,\n                   u8 g, u8 b, u8 a) {\n    if (texture == nullptr) {\n        return;\n    }\n    { extern bool ModsSprite(const char *, int, int, int, int, int, int, int, int); if (ModsSprite(texture->name, src.x, src.y, src.width, src.height, dst.x, dst.y, dst.width, dst.height)) { return; } }\n'),
    ('ps2/src/title/titleloop.cpp', '#include "common.h"\n',
     '#include "common.h"\nbool ModsAnyKeyEdge(); // script.cpp: a key or mouse button went down\n'),
    ('ps2/src/title/titleloop.cpp', '            if (GamePad.Down(PAD_START) && EffCnt > 16) {',
     '            if ((GamePad.Down(PAD_START | PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE) | ModsAnyKeyEdge()) && EffCnt > 16) {'),
    ('ps2/src/title/titleloop.cpp', '            if (GamePad.Down(PAD_START)) {\n                CProcess.no = TITLE_STEP_LOGO;\n            }\n\n            break;\n\n        case TITLE_STEP_LOGO:',
     '            if ((GamePad.Down(PAD_START | PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE) | ModsAnyKeyEdge())) {\n                CProcess.no = TITLE_STEP_LOGO;\n            }\n\n            break;\n\n        case TITLE_STEP_LOGO:'),
    ('ps2/src/title/titleloop.cpp', '            if (GamePad.Down(PAD_START)) {\n                Logo.motion_type.state.time = 116.0f;',
     '            if ((GamePad.Down(PAD_START | PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE) | ModsAnyKeyEdge())) {\n                Logo.motion_type.state.time = 116.0f;'),
    ('ps2/src/title/titleloop.cpp', '                if (Fade1 > 127 && GamePad.Down(PAD_START)) {',
     '                if (Fade1 > 127 && (GamePad.Down(PAD_START | PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE) | ModsAnyKeyEdge())) {'),
    ('port/src/snd.cpp', '#include "texture.hpp"\n',
     '#include "texture.hpp"\nbool ModsSprite(const char *, int, int, int, int, int, int, int, int); // script.cpp: every 2D sprite passes here\n'),
    ('port/src/snd.cpp', 'void RectSprite(CTexture *texture, const CRect_i_ &screen, const CRect_i_ &texel, Colour colour) {\n    if (texture == nullptr) {\n        return;\n    }\n',
     'void RectSprite(CTexture *texture, const CRect_i_ &screen, const CRect_i_ &texel, Colour colour) {\n    if (texture == nullptr) {\n        return;\n    }\n    { if (ModsSprite(texture->name, screen.x, screen.y, screen.width, screen.height, texel.x, texel.y, texel.width, texel.height)) { return; } }\n'),
    ('ps2/src/menu_manual.cpp', 'int GetGameFlagForManualMenu() {\n    CDngStatusData *status = SaveData->GetDngStatus();\n',
     'int GetGameFlagForManualMenu() {\n    if (true) {\n        return 1; // the MODS entry lives in the Manuals slot and is always there\n    }\n\n    CDngStatusData *status = SaveData->GetDngStatus();\n'),
    ('port/src/texture.cpp', '    if (PortDecodeTexture(1, width, height, levels, 1, clut, 256, false, decoded)) {\n        tbp = PortCreateTexture(decoded, PortTextureOwner::Manager, &cbp);',
     '    if (PortDecodeTexture(1, width, height, levels, 1, clut, 256, false, decoded)) {\n        { bool idx = decoded.format == gfx::TextureFormat::Index8; ModsTextureHook(name, 1, 73, decoded.width, decoded.height, idx, decoded.has_alpha, decoded.four_bit, decoded.palette.data(), decoded.levels); if (!idx) decoded.format = gfx::TextureFormat::Rgba8; }\n        tbp = PortCreateTexture(decoded, PortTextureOwner::Manager, &cbp);'),
    ('port/src/platform/input.cpp', '{"cross",        ActionKind::Button,   kInputCross,    kAxisLeftX,  0,  "Mouse1, Space"},',
     '{"cross",        ActionKind::Button,   kInputCross,    kAxisLeftX,  0,  "F, Mouse1"},'),
    ('port/src/platform/input.cpp', '{"circle",       ActionKind::Button,   kInputCircle,   kAxisLeftX,  0,  "F"},',
     '{"circle",       ActionKind::Button,   kInputCircle,   kAxisLeftX,  0,  "E, Space, Left Ctrl"},'),
    ('port/src/platform/input.cpp', '{"square",       ActionKind::Button,   kInputSquare,   kAxisLeftX,  0,  "E"},',
     '{"square",       ActionKind::Button,   kInputSquare,   kAxisLeftX,  0,  "G"},'),
    ('port/src/platform/input.cpp', '{"start",        ActionKind::Button,   kInputStart,    kAxisLeftX,  0,  "Return"},',
     '{"start",        ActionKind::Button,   kInputStart,    kAxisLeftX,  0,  "Return, Escape"},'),
    ('port/src/platform/input.cpp', '{"select",       ActionKind::Button,   kInputSelect,   kAxisLeftX,  0,  "Backspace, C"},',
     '{"select",       ActionKind::Button,   kInputSelect,   kAxisLeftX,  0,  "Backspace"},'),
    ('port/src/platform/input.cpp', 'InputKeyboardMouse                                       g_scripted;\n',
     'InputKeyboardMouse                                       g_scripted;\nstd::uint16_t g_inject_mask = 0; // pad buttons a mod presses for the game (dc.press), for g_inject_ticks more ticks\nint           g_inject_ticks = 0;\nfloat         g_wheel = 0.0f;   // mouse wheel notches not turned into a press yet\nint           g_wheel_mode = 0; // 0: up/down notches are Left/Right (the dungeon\'s item choice), 1: Up/Down (menus)\nint           g_wheel_press = 0; // the wheel press held this tick: 0 none, else a pad button mask\n'),
    ('port/src/platform/input.cpp', '    g_state[pad] = pad == 0 ? InputApplyKeyboardMouse(g_device[pad], LiveKeyboardMouse()) : g_device[pad];\n',
     '    g_state[pad] = pad == 0 ? InputApplyKeyboardMouse(g_device[pad], LiveKeyboardMouse()) : g_device[pad];\n    if (pad == 0) {\n        if (g_inject_ticks > 0) {\n            g_state[pad].buttons |= g_inject_mask;\n        }\n        g_state[pad].buttons |= g_wheel_press;\n    }\n'),
    ('port/src/platform/input.cpp', '        case SDL_EVENT_WINDOW_FOCUS_LOST:\n            g_keys.fill(false);\n            break;\n',
     '        case SDL_EVENT_MOUSE_WHEEL:\n            g_wheel += event.wheel.y;\n            break;\n        case SDL_EVENT_WINDOW_FOCUS_LOST:\n            g_keys.fill(false);\n            break;\n'),
    ('port/src/platform/input.cpp', '    g_mouse_dx = dx / static_cast<float>(ticks);\n    g_mouse_dy = dy / static_cast<float>(ticks);\n    Compose(0);\n}',
     '    g_mouse_dx = dx / static_cast<float>(ticks);\n    g_mouse_dy = dy / static_cast<float>(ticks);\n    if (g_inject_ticks > 0) {\n        --g_inject_ticks;\n    }\n    g_wheel_press = 0; // one wheel notch is one press, held for this tick only\n    if (g_wheel >= 1.0f) {\n        g_wheel -= 1.0f;\n        g_wheel_press = g_wheel_mode == 0 ? kInputLeft : kInputUp;\n    } else if (g_wheel <= -1.0f) {\n        g_wheel += 1.0f;\n        g_wheel_press = g_wheel_mode == 0 ? kInputRight : kInputDown;\n    }\n    Compose(0);\n}'),
    ('port/src/platform/input.cpp', 'void InputResetBindings() {',
     'void InputInjectButtons(std::uint16_t mask, int ticks) {\n    g_inject_mask = mask;\n    g_inject_ticks = ticks;\n}\n\nvoid InputSetWheelMode(int mode) { g_wheel_mode = mode; }\n\nstd::string InputBindingText(std::string_view action) {\n    EnsureBindings();\n    std::string out;\n    for (std::size_t i = 0; i < kActionCount; ++i) {\n        if (kActions[i].name != action) {\n            continue;\n        }\n        for (const Source &s : g_bindings[i]) {\n            std::string one;\n            if (s.kind == Source::Key) {\n                one = SDL_GetScancodeName(static_cast<SDL_Scancode>(s.code));\n            } else if (s.kind == Source::MouseButton) {\n                one = "Mouse" + std::to_string(s.code);\n            } else {\n                continue;\n            }\n            out += (out.empty() ? "" : ", ") + one;\n        }\n    }\n    return out;\n}\n\nvoid InputResetBindings() {'),
    ('port/src/platform/input.hpp', '// Restores the default bindings and mouse settings.\n',
     '// Presses pad buttons (a mask of kInput*) for the game for the next `ticks` ticks; a mod\'s "press" (dc.press).\nvoid InputInjectButtons(std::uint16_t mask, int ticks);\n\n// What one mouse wheel notch presses: mode 0 Left/Right (choosing a dungeon item), mode 1 Up/Down (menus).\nvoid InputSetWheelMode(int mode);\n\n// The keys and mouse buttons now bound to an action, as text ("F, Mouse1"); "" when none or unknown.\nstd::string InputBindingText(std::string_view action);\n\n// Restores the default bindings and mouse settings.\n'),
    ('ps2/src/editloop3.cpp', '    if (candidate_count > 0) {\n        for (period = 0; period < 4; period++) {\n',
     '    if (candidate_count > 0) {\n        { extern unsigned ModsVillagerSeed(int); unsigned seed = ModsVillagerSeed(MapNo); if (seed != 0) { srand(seed); } }\n        for (period = 0; period < 4; period++) {\n'),
    ('ps2/src/editloop3.cpp', '                info->time_tables[0][period][i] = candidates[i];\n            }\n        }\n    }\n}',
     '                info->time_tables[0][period][i] = candidates[i];\n            }\n        }\n        { extern unsigned ModsVillagerSeed(int); extern unsigned ModsVillagerReseed(); if (ModsVillagerSeed(MapNo) != 0) { srand(ModsVillagerReseed()); } }\n    }\n}'),
    ('ps2/include/savedata.hpp', '    friend class CMemoryCardAccess;\n',
     '    friend class CMemoryCardAccess;\n    friend struct GuestWorldAccess;\n'),
    ('port/src/memorycardaccess.cpp', '    memcpy(this->save_buffer, SaveData, 0x131C0);\n    this->save_buffer->ConvertConfig(&sys_config);',
     '    memcpy(this->save_buffer, SaveData, 0x131C0);\n    { extern void GwFixSave(CSaveData *); GwFixSave(this->save_buffer); }\n    this->save_buffer->ConvertConfig(&sys_config);'),
    ('ps2/src/memorycardaccess.cpp', '                memcpy(SaveData, this->load_buffer, sizeof(CSaveData));\n                ((s32 *) SaveData->GetConfigData())[17] = file_no;\n',
     '                memcpy(SaveData, this->load_buffer, sizeof(CSaveData));\n                ((s32 *) SaveData->GetConfigData())[17] = file_no;\n                { extern void GwAfterLoad(); GwAfterLoad(); }\n'),
    ('port/src/gameloop.cpp', '            result = ModeLoop(skip_title);\n            GameApplyLoopResult(old_main_mode, result);\n',
     '            result = ModeLoop(skip_title);\n            GameApplyLoopResult(old_main_mode, result);\n            { extern bool GwTakeJump(); if (GwTakeJump()) { result = 1; } }\n'),
    ('ps2/src/editloop.cpp', '        int plot = pEditGround->SetMapParts(NowSelectParts, parts_pos[0], parts_pos[1], parts_pos[2], NowSelectAngle);\n\n        if (plot >= 0) {\n',
     '        int plot = pEditGround->SetMapParts(NowSelectParts, parts_pos[0], parts_pos[1], parts_pos[2], NowSelectAngle);\n\n        if (plot >= 0) {\n            { extern void ModsGeoPlaced(int, const float *, int); ModsGeoPlaced(NowSelectParts, parts_pos, NowSelectAngle); }\n'),
    ('ps2/src/editloop.cpp', 'pEditGround->DeleteMapParts(&deleted_parts, &deleted_angle, parts_pos[0], parts_pos[1], parts_pos[2]) >= 0) {\n',
     'pEditGround->DeleteMapParts(&deleted_parts, &deleted_angle, parts_pos[0], parts_pos[1], parts_pos[2]) >= 0) {\n            { extern void ModsGeoRemoved(const float *); ModsGeoRemoved(parts_pos); }\n'),
    ('ps2/src/editloop.cpp', '            if (pEditGround->DeleteMapParts(&taken_parts, &taken_angle, parts_pos[0], parts_pos[1], parts_pos[2]) >= 0) {\n',
     '            if (pEditGround->DeleteMapParts(&taken_parts, &taken_angle, parts_pos[0], parts_pos[1], parts_pos[2]) >= 0) {\n                { extern void ModsGeoRemoved(const float *); ModsGeoRemoved(parts_pos); }\n'),
    ('port/src/editloop.cpp', 'EdPadDown(0x100, 2) != 0 && EdCheckViewMode() == 0 && change_time_event == 0) {',
     '(EdPadDown(0x100, 2) != 0 || ({ extern bool ModsGeoForce(); ModsGeoForce(); })) && EdCheckViewMode() == 0 && change_time_event == 0) {'),
    ('ps2/src/editloop3.cpp', '    GamePad.MenuModeOn(0x78);\n    ReadBG();\n\n    if (camera != NULL) {\n        camera->FollowOff();\n    }\n\n    EditMes1.Step();\n',
     '    GamePad.MenuModeOn(0x78);\n    ReadBG();\n\n    if (camera != NULL) {\n        camera->FollowOff();\n    }\n\n    EditMes1.Step();\n    { extern void ModsNpcTalkTick(int); ModsNpcTalkTick(talk_villager__2 != NULL ? talk_villager__2->villager_id : -1); }\n'),
    ('port/src/editloop3.cpp', '                EdVillager[i].SetPosition(position);\n            }\n        }\n    }\n}\n',
     '                EdVillager[i].SetPosition(position);\n            }\n        }\n    }\n    { extern void ModsNpcStep(); ModsNpcStep(); }\n}\n'),
    ('ps2/src/runscript_opcodes.cpp', '        if (half_speed) {\n            speed *= 0.5f;\n        }\n',
     '        if (half_speed) {\n            speed *= 0.5f;\n        }\n        { extern float ModsSpeedMul(int); speed *= ModsSpeedMul(monster_no); }\n'),
    ('ps2/src/runscript_opcodes.cpp', '        speed *= 0.5f;\n    }\n\n    NowMonstorUnit->monster[monster_no].movement_speed = speed;',
     '        speed *= 0.5f;\n    }\n    { extern float ModsSpeedMul(int); speed *= ModsSpeedMul(monster_no); }\n\n    NowMonstorUnit->monster[monster_no].movement_speed = speed;'),
    # multiplayer (ghost.cpp): a guest builds the host's floor seed; ghosts step and draw beside the player
    ('ps2/src/dungeonmap.cpp', 'this->map_seed = (int) ((float) rand() / (float) 100000);',
     'extern int MpFloorSeed(int);\n        this->map_seed = MpFloorSeed((int) ((float) rand() / (float) 100000));'),
    ('ps2/src/dun/gameloop.cpp', '    CharaMain.TextureAnime(0x11);\n    CharaMain.Draw();\n',
     '    CharaMain.TextureAnime(0x11);\n    CharaMain.Draw();\n    { extern void GhostDrawDungeon(); GhostDrawDungeon(); }\n'),
    ('ps2/src/edit.cpp', '        player->TextureAnime(8);\n        player->Draw();\n',
     '        player->TextureAnime(8);\n        player->Draw();\n    }\n    { extern void GhostDrawTown(); GhostDrawTown(); }\n    if (false) {\n'),
    ('ps2/src/monstorunit.cpp', '        float distance = DistVector(player_position, monster_position);\n        monster[i].player_distance = distance;',
     '        extern float MpNearestDistance(const float *, const float *);\n        float distance = MpNearestDistance(player_position, monster_position);\n        monster[i].player_distance = distance;'),
    ('ps2/src/monstorunit.cpp', '            current_monster = i;\n\n            if (monster[current_monster].motion_reset_pending != 0) {',
     '            current_monster = i;\n            { extern void MpMonsterTarget(int); MpMonsterTarget(i); }\n\n            if (monster[current_monster].motion_reset_pending != 0) {'),
    ('ps2/src/dun/gameloop.cpp', '        NowMonstorUnit->Step(driveStepHold | CMonUnitHold);',
     '        { extern void MpMonsterStepBegin(); extern void MpMonsterStepEnd(); MpMonsterStepBegin(); NowMonstorUnit->Step(driveStepHold | CMonUnitHold); MpMonsterStepEnd(); }'),
    ('ps2/src/monstorunit.cpp', '        if (monster[i].state == -1 || monster[i].revealed == 0) {\n            continue;\n        }\n\n        CCharacter *character = &chara[i][0];',
     '        extern bool MpIsPuppet(int);\n        if (monster[i].state == -1 || monster[i].revealed == 0 || MpIsPuppet(i)) {\n            continue;\n        }\n\n        CCharacter *character = &chara[i][0];'),
    ('ps2/src/monstorunit.cpp', '    if (monster[current_monster].stop_timer <= 0) {\n        for (int i = 0; i < 16; i++) {\n            if (effect2[current_monster].active[i] != 0) {',
     '    extern bool MpIsPuppet(int);\n    if (monster[current_monster].stop_timer <= 0 && !MpIsPuppet(current_monster)) {\n        for (int i = 0; i < 16; i++) {\n            if (effect2[current_monster].active[i] != 0) {'),
    ('ps2/src/monstorunit.cpp', '                if (monster[current_monster].stop_timer > 0) {\n                    PalletStep();',
     '                extern bool MpIsPuppet(int);\n                if (monster[current_monster].stop_timer > 0 || MpIsPuppet(current_monster)) {\n                    PalletStep();'),
    ('ps2/src/dun/gameloop.cpp', 'void LoadChara2(int chara, int keep_place, unsigned int *chara_data, unsigned int *crash_data, unsigned int *default_data, unsigned int *main_data) {\n    CFrameAttr    frame_attr;',
     'void LoadChara2(int chara, int keep_place, unsigned int *chara_data, unsigned int *crash_data, unsigned int *default_data, unsigned int *main_data) {\n    { extern void GhostInvalidate(); GhostInvalidate(); }\n    CFrameAttr    frame_attr;'),
    ('ps2/src/editloop.cpp', 'void EdLoadMainChara(char *pack_path, char *info_name, CDataAlloc2<1> *arena) {\n    int i;',
     'void EdLoadMainChara(char *pack_path, char *info_name, CDataAlloc2<1> *arena) {\n    { extern void GhostInvalidate(); GhostInvalidate(); }\n    int i;'),
    ('ps2/src/btsysscript.cpp', 'int _SET_OBJHDL_POS(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_OBJHDL_POS(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(1); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_OBJHDL_ROT(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_OBJHDL_ROT(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(1); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_EVENT_SW(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_EVENT_SW(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(1); }'),
    ('ps2/src/btsysscript.cpp', 'int _GO_DUNGEON(RS_STACKDATA *stack, int argument_count) {',
     'int _GO_DUNGEON(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_FLOOR_LEVEL(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_FLOOR_LEVEL(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_URA_DUNGEON(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_URA_DUNGEON(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_DUNGEON_MAP(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_DUNGEON_MAP(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_RANDOM_MAP(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_RANDOM_MAP(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _LOAD_DUNGEON_MAP2(RS_STACKDATA *stack, int argument_count) {',
     'int _LOAD_DUNGEON_MAP2(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _SET_MAIN_CHR2(RS_STACKDATA *stack, int argument_count) {',
     'int _SET_MAIN_CHR2(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _RESET_MAIN_CHR(RS_STACKDATA *stack, int argument_count) {',
     'int _RESET_MAIN_CHR(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _OPEN_ENTRANCE_WINDOW(RS_STACKDATA *stack, int argument_count) {',
     'int _OPEN_ENTRANCE_WINDOW(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _OPEN_ESCAPE_WINDOW(RS_STACKDATA *stack, int argument_count) {',
     'int _OPEN_ESCAPE_WINDOW(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/btsysscript.cpp', 'int _EASTKING_COMPLETE(RS_STACKDATA *stack, int argument_count) {',
     'int _EASTKING_COMPLETE(RS_STACKDATA *stack, int argument_count) {\n    { extern void MpScriptOp(int); MpScriptOp(2); }'),
    ('ps2/src/dun/gameloop.cpp', '                case BT_REQUEST_ITEM_WINDOW:\n                    BtEventInfo.request = BT_REQUEST_NONE;\n                    BtMiniItemSelect();',
     '                case BT_REQUEST_ITEM_WINDOW:\n                    BtEventInfo.request = BT_REQUEST_NONE;\n                    { extern int MpAutoItemSelect(); if (MpAutoItemSelect()) { break; } }\n                    BtMiniItemSelect();'),
    ('port/src/runtime.cpp', '#ifdef __APPLE__\n#define LIBC_NOEXCEPT', '#if defined(__APPLE__) || defined(_WIN32)\n#define LIBC_NOEXCEPT'),
    ('port/src/sce/sifdev.cpp', '#include <fcntl.h>',
     '#include <fcntl.h>\n#include <io.h>\n#ifndef O_CLOEXEC\n#define O_CLOEXEC 0\n#endif\n#ifndef O_BINARY\n#define O_BINARY 0\n#endif'),
    ('port/src/sce/sifdev.cpp', 'return host | O_CLOEXEC;', 'return host | O_CLOEXEC | O_BINARY;'),
    ('port/src/sce/sifdev.cpp', '::open(path.c_str(), HostFlags(flags), 0644)', '::_wopen(path.c_str(), HostFlags(flags), 0644)'),
    ('port/src/sce/libmc.cpp', 'gmtime_r(&seconds, &utc);', 'gmtime_s(&utc, &seconds);'),
    ('port/src/sce/libmc.cpp', 'std::fopen(path.c_str(), (flag & kOpenWrite) != 0 ? "r+b" : "rb")', '_wfopen(path.c_str(), (flag & kOpenWrite) != 0 ? L"r+b" : L"rb")'),
    ('port/src/sce/libmc.cpp', 'std::fopen(path.c_str(), "w+b")', '_wfopen(path.c_str(), L"w+b")'),
    ('port/src/sce/libmc.cpp', 'McDateTime DateTime(const fs::path &path) {',
     'static std::string PathUtf8(const fs::path &p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }\nMcDateTime DateTime(const fs::path &path) {'),
    ('port/src/sce/libmc.cpp', 'Matches(pattern.c_str(), entry.path().filename().c_str())', 'Matches(pattern.c_str(), PathUtf8(entry.path().filename()).c_str())'),
    ('port/src/sce/libmc.cpp', '[](const fs::directory_entry &entry) { return entry.path().filename().string(); }', '[](const fs::directory_entry &entry) { return PathUtf8(entry.path().filename()); }'),
    ('port/src/sce/libmc.cpp', 'Entry(entry.path(), entry.path().filename().string(), entry.is_directory(error))', 'Entry(entry.path(), PathUtf8(entry.path().filename()), entry.is_directory(error))'),
    ('port/src/dataread.cpp', 'std::string relative = it->path().lexically_relative(root).generic_string();',
     'std::string relative = RelativeAsGameName(it->path().lexically_relative(root));'),
    ('ps2/src/menu_save.cpp', '    McAccess.SetFuncNo(MC_OPERATION_SAVE);\n    McAccess.file_no = SaveMenu.file_no;\n    SaveMenu.key_no = SAVE_KEY_END_SAVE;',
     '    McAccess.SetFuncNo(MC_OPERATION_SAVE);\n    McAccess.file_no = SaveMenu.file_no;\n    SaveMenu.key_no = SAVE_KEY_END_SAVE;\n    { void ScriptSaveSlot(int, int); ScriptSaveSlot(SaveMenu.file_no, 1); }'),
    ('ps2/src/menu_save.cpp', '    McAccess.SetFuncNo(MC_OPERATION_LOAD);\n    McAccess.file_no = SaveMenu.file_no;\n    SaveMenu.key_no = SAVE_KEY_FILE_SELECT;',
     '    McAccess.SetFuncNo(MC_OPERATION_LOAD);\n    McAccess.file_no = SaveMenu.file_no;\n    SaveMenu.key_no = SAVE_KEY_FILE_SELECT;\n    { void ScriptSaveSlot(int, int); ScriptSaveSlot(SaveMenu.file_no, 0); }'),
    ('ps2/src/monstorunit.cpp', '    if (monster[current_monster].poison_timer > 0) {\n        monster[current_monster].palette_override[0] = 47.0f;',
     '    {\n        int ModsTakeDamage(int, int *);\n        int ext_owner = -1;\n        int ext = ModsTakeDamage(current_monster, &ext_owner);\n        if (ext > 0 && monster[current_monster].hp > 0) {\n            monster[current_monster].hp -= ext;\n            result = MONSTER_DMG_HIT;\n            if (monster[current_monster].hp <= 0) {\n                monster[current_monster].hp = 0;\n                alive_count--;\n                result = MONSTER_DMG_KILLED;\n                { void ScriptMonsterKilled(int, int); ScriptMonsterKilled(current_monster, ext_owner); }\n                ((CDngStatusData *) UserStatus)->AddKills();\n            }\n            chara[current_monster][0].GetPosition(poison_position);\n            poison_position[1] += chara[current_monster][0].body_height;\n            HitValueEntry(NowHitValue, poison_position, ext, HIT_VALUE_MONSTER, NULL);\n        }\n    }\n\n    if (monster[current_monster].poison_timer > 0) {\n        monster[current_monster].palette_override[0] = 47.0f;'),
    ('port/src/platform/input.hpp', 'const InputPadState &InputGetPad(int pad);\n',
     'const InputPadState &InputGetPad(int pad);\n\n// Mod framework: hides these buttons (and the sticks when `sticks`) of pad 0 from the game. The mod reads the raw pad.\nvoid                 InputModBlock(std::uint16_t buttons, bool sticks);\nconst InputPadState &InputGetPadRaw(int pad);\n'),
    ('port/src/platform/input.cpp', 'const InputPadState &InputGetPad(int pad) {\n    static const InputPadState kAbsent;',
     'static std::uint16_t g_mod_block_buttons = 0;\nstatic bool          g_mod_block_sticks = false;\nstatic bool          g_raw_read = false;\nvoid InputModBlock(std::uint16_t buttons, bool sticks) {\n    g_mod_block_buttons = buttons;\n    g_mod_block_sticks = sticks;\n}\n\nconst InputPadState &InputGetPad(int pad) {\n    static const InputPadState kAbsent;'),
    ('port/src/platform/input.cpp', '    if (!g_stick_live) {\n        g_view[pad].buttons |= g_view[pad].stick_dpad;\n    }\n    return g_view[pad];\n}',
     '    if (!g_stick_live) {\n        g_view[pad].buttons |= g_view[pad].stick_dpad;\n    }\n    if (pad == 0 && !g_raw_read && (g_mod_block_buttons != 0 || g_mod_block_sticks)) {\n        g_view[pad].buttons &= static_cast<std::uint16_t>(~g_mod_block_buttons);\n        if (g_mod_block_sticks) {\n            g_view[pad].left_x = g_view[pad].left_y = g_view[pad].right_x = g_view[pad].right_y = kInputAxisCentre;\n            g_view[pad].stick_dpad = 0;\n        }\n    }\n    return g_view[pad];\n}\n\nconst InputPadState &InputGetPadRaw(int pad) {\n    g_raw_read = true;\n    const InputPadState &state = InputGetPad(pad);\n    g_raw_read = false;\n    return state;\n}'),
    ('port/src/gameutil_motion.cpp', '#include "framevu1.hpp"\n', '#include "framevu1.hpp"\n#include "modmodel.hpp"\n'),
    ('port/src/gameutil_motion.cpp', '        std::memcpy(g_deformed, frame_info[list->frame].base_vertices,\n                    frame_info[list->frame].vertex_count * sizeof(sceVu0FVECTOR));',
     '        std::memcpy(g_deformed, frame_info[list->frame].base_vertices,\n                    frame_info[list->frame].vertex_count * sizeof(sceVu0FVECTOR));\n        ModsSkinBegin(vert);'),
    ('port/src/gameutil_motion.cpp', '        SkinVertex(vert[vertex], g_deformed[vertex], frame_info[list->frame].base_vertices[vertex], Bone_Matrix_Base,\n                   Bone_Matrix, Bone_Matrix_inv, 0.01f * list->keys[i].value[0]);',
     '        SkinVertex(vert[vertex], g_deformed[vertex], frame_info[list->frame].base_vertices[vertex], Bone_Matrix_Base,\n                   Bone_Matrix, Bone_Matrix_inv, 0.01f * list->keys[i].value[0]);\n        ModsSkinVertex(vert, vertex, Bone_Matrix_Base, Bone_Matrix, Bone_Matrix_inv, 0.01f * list->keys[i].value[0]);'),
    ('ps2/src/camera.cpp', 'void CCamera::GetCameraMatrix(float (*matrix)[4]) {\n    float dir[4];',
     'void CCamera::GetCameraMatrix(float (*matrix)[4]) {\n    { void ModsCameraApply(CCamera *); ModsCameraApply(this); }\n    float dir[4];'),
    ('ps2/src/dun/gameloop.cpp', 'NowDngMap->buildRandomMap(6, 1);', '{ int ModsRoomMax(int, int); NowDngMap->buildRandomMap(ModsRoomMax(6, (int) selectMapNo), 1); int area = 0; for (int r = 0; r < NowDngMap->room_num && r < 16; r++) area += NowDngMap->rooms[r].width * NowDngMap->rooms[r].height; printf("floor built: %d rooms, %d room cells\\n", (int) NowDngMap->room_num, area); if (getenv("DC_DUMP_FLOOR")) { for (int rw = 0; rw < 20; rw++) { char line[24]; for (int cl = 0; cl < 20; cl++) { int pn = (int) NowDngMap->cells[cl + rw * 20].parts_no; line[cl] = pn == -1 ? 46 : (pn <= 4 ? 43 : 35); } line[20] = 0; printf("floor row %s\\n", line); } } }'),
    ('ps2/src/dun/gameloop.cpp', 'UraDungeonMap.buildRandomMap(6, 0);', '{ int ModsRoomMax(int, int); UraDungeonMap.buildRandomMap(ModsRoomMax(6, (int) selectMapNo), 0); }'),
    ('ps2/src/monstorunit.cpp', 'void CMonstorUnit::ArrangementPos(CDungeonMap *map, int count, int model_no, int unused) {\n    sceVu0FVECTOR position;',
     'void CMonstorUnit::ArrangementPos(CDungeonMap *map, int count, int model_no, int unused) {\n    { int ModsMonsterCount(int, int); count = ModsMonsterCount(count, (int) selectMapNo); }\n    sceVu0FVECTOR position;'),
    ('ps2/src/dungeonmap.cpp', 'while (roomStackCnt <= room_max - 1 && retry < 0x200) {', 'while (roomStackCnt <= room_max - 1 && retry < 0x4000) {'),
    ('ps2/src/dungeonmap.cpp', 'retry++;\n\n                if (retry >= 0x200) {', 'retry++;\n\n                if (retry >= 0x4000) {'),
    ('ps2/src/dungeonmap.cpp', '#include "dungeonmap.hpp"\n', '#include "dungeonmap.hpp"\nfloat ModsRoomBonus(int);\n'),
    ('ps2/src/dungeonmap.cpp', 'w = (int) ((2.0f * (float) rand()) / 2147483648.0f) + 3;', 'w = (int) (((2.0f + ModsRoomBonus((int) selectMapNo)) * (float) rand()) / 2147483648.0f) + 3;'),
    ('ps2/src/dungeonmap.cpp', 'h = (int) ((2.0f * (float) rand()) / 2147483648.0f) + 3;', 'h = (int) (((2.0f + ModsRoomBonus((int) selectMapNo)) * (float) rand()) / 2147483648.0f) + 3;'),
    ('ps2/src/menu_dungeon.cpp', '        case DUN_ENTER_SELECT:\n            selected = DEnterMenu.selected_floor;',
     '        case DUN_ENTER_SELECT:\n            { void ModsFloorSelect(int, int, int, float, int); ModsFloorSelect((int) DEnterMenu.dungeon, (int) DEnterMenu.selected_floor, (int) DEnterMenu.scroll_top, DEnterMenu.list_y, (int) DEnterMenu.floor_count); }\n            selected = DEnterMenu.selected_floor;'),
    ('port/src/dataread.cpp', '    auto found = data_index.find(dcdata::FoldPath(StripDevice(path)));\n    return found == data_index.end() ? nullptr : &found->second;',
     '    std::string key = dcdata::FoldPath(StripDevice(path));\n    auto        found = data_index.find(key);\n    if (const fs::path *over = ModsFileOverride(key.c_str(), found == data_index.end() ? nullptr : &found->second)) {\n        return over;\n    }\n    return found == data_index.end() ? nullptr : &found->second;'),
    ('port/src/dataread.cpp', '    int size = ReadWhole(*file, buffer);\n    if (out_size) {\n        *out_size = size;\n    }\n    return 1;\n}\n\nvoid InitReadBG() {',
     '    int size = ReadWhole(*file, buffer);\n    { int ModTextPatch(const char *, void *, int); size = ModTextPatch(path, buffer, size); }\n    if (out_size) {\n        *out_size = size;\n    }\n    return 1;\n}\n\nvoid InitReadBG() {'),
    ('port/src/dataread.cpp', 'std::unordered_map<std::string, fs::path> data_index;',
     'extern "C" __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned, unsigned long, const wchar_t *, int, char *, int, const char *, int *);\n'
     '// Windows stores the Shift-JIS file names as Unicode; the game asks for the original CP932 bytes.\n'
     'static std::string RelativeAsGameName(const fs::path &p) {\n'
     '    std::wstring w = p.generic_wstring();\n'
     '    int n = WideCharToMultiByte(932, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);\n'
     '    std::string out(n > 0 ? n : 0, 0);\n'
     '    if (n > 0) WideCharToMultiByte(932, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);\n'
     '    return out;\n}\n'
     'std::unordered_map<std::string, fs::path> data_index;'),
    ('tools/dcdata/dcdata.hpp', 'inline Summary Extract(',
     'extern "C" __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned, unsigned long, const char *, int, wchar_t *, int);\n'
     'inline fs::path Cp932Path(const std::string &s) { // game names are Shift-JIS bytes\n'
     '    int n = MultiByteToWideChar(932, 0, s.data(), (int)s.size(), nullptr, 0);\n'
     '    std::wstring w(n > 0 ? n : 0, 0);\n'
     '    if (n > 0) MultiByteToWideChar(932, 0, s.data(), (int)s.size(), w.data(), n);\n'
     '    return fs::path(w);\n}\n'
     'inline Summary Extract('),
    ('tools/dcdata/dcdata.hpp', 'fs::path target = out / fs::path(record->path);', 'fs::path target = out / Cp932Path(record->path);'),
    ('tools/dcdata/dcdata.hpp', 'IsCurrent(root / fs::path(record->path), record->size)', 'IsCurrent(root / Cp932Path(record->path), record->size)'),
    ('port/src/main.cpp', 'int main(int argc, const char **argv, const char **envp) {\n    argc = PathsConsumeArgs(argc, argv);',
     'extern "C" char *setlocale(int, const char *);\nvoid InstallCrashHandler();\nint main(int argc, const char **argv, const char **envp) {\n    InstallCrashHandler();\n    setlocale(0 /* LC_ALL */, ".UTF-8"); // wide<->narrow path conversion needs a UTF-8 locale on Windows\n    argc = PathsConsumeArgs(argc, argv);\n    argc = SetupConsumeArgs(argc, argv);'),
    ('port/src/main.cpp', '    FirstRunIfNoData(options.headless);', '    FirstRunIfNoData(options.headless);\n    SetupIfNeeded(options.headless);'),
    ('port/src/main.cpp', 'void InstallCrashHandler();', 'void InstallCrashHandler();\nint SetupConsumeArgs(int, const char **);\nvoid SetupIfNeeded(bool);'),
    # Windows defaults: mailbox presenting keeps the game ticking at 50 Hz; a frame cap of 240 stops the GPU running flat out.
    ('port/src/platform/config.hpp', 'present_mode = ConfigPresentMode::Fifo;', 'present_mode = ConfigPresentMode::Mailbox;'),
    ('port/src/platform/config.hpp', 'max_fps = 0.0;', 'max_fps = 240.0;'),
    ('port/src/platform/config.hpp', 'show_fps = true;', 'show_fps = false;'),
    ('port/src/platform/config.hpp', 'debug_mode = true;', 'debug_mode = false;'),
    ('port/src/gfx/draw.cpp', 'Error("a draw samples the target it renders to; snapshot it first");',
     '{ static int dbg_n = 0; if (dbg_n++ < 8) Error("a draw samples the target it renders to (texture %#x, target %#x, main %#x, #%d); snapshot it first", (unsigned) binding.texture, (unsigned) g.target, (unsigned) kMainTarget, dbg_n); }'),
    # Mod framework phase 1a: texture overrides.
    ('port/src/texture.cpp', '#include "texture_port.hpp"', '#include "texture_port.hpp"\n#include "platform/mods.hpp"'),
    ('port/src/texture.cpp', '            return;\n        }\n        tbp = PortCreateTexture(decoded, PortTextureOwner::Manager, &cbp);',
     '            return;\n        }\n        { bool idx = decoded.format == gfx::TextureFormat::Index8; ModsTextureHook(name, bpp, block, decoded.width, decoded.height, idx, decoded.has_alpha, decoded.four_bit, decoded.palette.data(), decoded.levels); if (!idx) decoded.format = gfx::TextureFormat::Rgba8; }\n        tbp = PortCreateTexture(decoded, PortTextureOwner::Manager, &cbp);'),
    ('port/src/dataread.cpp', '#include "platform/paths.hpp"', '#include "platform/paths.hpp"\n#include "platform/mods.hpp"'),
    # Mod framework phases 3/4: Lua scripts + native plugins.
    ('port/src/gameloop.cpp', '#include "gameloop.hpp"\n\n#include <algorithm>', '#include "gameloop.hpp"\n#include "script.hpp"\n\n#include <algorithm>'),
    ('port/src/gameloop.cpp', 'void GameRenderTick(gfx::DisplayListRef list) {\n', 'void GameRenderTick(gfx::DisplayListRef list) {\n    ScriptTick();\n'),
    ('ps2/src/dngstatusdata.cpp', '    printf("GetITEM No === %d\\n", item_id);\n', '    void ScriptItemPickup(int *, int *);\n    ScriptItemPickup(&item_id, &qty);\n    printf("GetITEM No === %d\\n", item_id);\n'),
    # Mod framework phase 1b: model dump + replacement.
    ('port/src/draw3d.hpp', '    bool                       immediate = false;\n', '    bool                       immediate = false;\n    bool                       replaced = false; // a mod swapped the geometry; the remake only carries the original\'s skinning over\n    std::vector<float>         skin_bind;        // replaced: the original\'s vertex positions at build time (3 each)\n    std::vector<float>         skin_base;        // replaced: the replacement\'s own bind positions (3 each)\n    std::vector<int>           skin_src;         // replaced: 4 nearest original vertices per replacement vertex\n    std::vector<float>         skin_weight;      // replaced: and their weights\n    std::vector<std::vector<int>> skin_by_orig;  // replaced: original vertex -> the replacement vertices that copy its bone weights\n    std::vector<float>         skin_def;         // exact skinning: running blend (4 per vertex)\n    std::vector<float>         skin_out;         // exact skinning: posed positions (4 per vertex)\n    bool                       skin_fresh = false; // MotionProc2 posed the replacement since the last remake\n'),
    ('port/src/visualvu1.cpp', '#include "visual.hpp"\n', '#include "visual.hpp"\n#include "modmodel.hpp"\n#include <string>\n'),
    ('port/src/visualvu1.cpp', '    Draw3DStrip             current;\n', '    std::string             first_name;\n    Draw3DStrip             current;\n'),
    ('port/src/visualvu1.cpp', '            MDT_MATERIAL &entry = view.materials[material];\n            int           handle', '            MDT_MATERIAL &entry = view.materials[material];\n            if (first_name.empty()) first_name = entry.texture;\n            int           handle'),
    ('port/src/visualvu1.cpp', '    SpanWholeTexture(visual, runs);\n    Draw3DFinishVisual(visual);', '    SpanWholeTexture(visual, runs);\n    ModsModelHook(visual, first_name.c_str(), view.vertex, view.header->vertex_num, block);\n    Draw3DFinishVisual(visual);'),
    ('port/src/visualvu1.cpp', '    Draw3DVisual *visual = Draw3DFindVisual(block);\n    if (visual == nullptr) {\n        return vu_size;', '    Draw3DVisual *visual = Draw3DFindVisual(block);\n    if (visual != nullptr && visual->replaced) {\n        ModsModelSkin(*visual, View(data).vertex, block);\n        return vu_size;\n    }\n    if (visual == nullptr) {\n        return vu_size;'),
    ('port/src/dataread.cpp', 'int LoadFile2(char *path, void *buffer, int *out_size, int mode) {\n    if (out_size) {',
     'int LoadFile2(char *path, void *buffer, int *out_size, int mode) {\n    ModsNoteFile(path);\n    if (out_size) {'),
    # Mod framework v2: combat hooks, HUD text.
    ('ps2/src/monstorunit.cpp', '                    BtActStatus.monstor_target = owner;\n                    monster[current_monster].hp -= amount;\n',
     '                    BtActStatus.monstor_target = owner;\n                    { void ScriptMonsterHit(int, int, int, int *); ScriptMonsterHit(current_monster, (int) owner, (int) element, &amount); }\n                    monster[current_monster].hp -= amount;\n'),
    ('ps2/src/monstorunit.cpp', '                        result = MONSTER_DMG_KILLED;\n                        ((CDngStatusData *) UserStatus)->AddKills();\n',
     '                        result = MONSTER_DMG_KILLED;\n                        { void ScriptMonsterKilled(int, int); ScriptMonsterKilled(current_monster, (int) owner); }\n                        ((CDngStatusData *) UserStatus)->AddKills();\n'),
    ('ps2/src/monstorunit.cpp', '                result = MONSTER_DMG_KILLED;\n                ((CDngStatusData *) UserStatus)->AddKills();\n            }\n\n            chara[current_monster][0].GetPosition(poison_position);',
     '                result = MONSTER_DMG_KILLED;\n                { void ScriptMonsterKilled(int, int); ScriptMonsterKilled(current_monster, -1); }\n                ((CDngStatusData *) UserStatus)->AddKills();\n            }\n\n            chara[current_monster][0].GetPosition(poison_position);'),
    ('ps2/src/dngstatusdata.cpp', 'void CUserStatus::AddNowLife(int chara_no, s16 amount, float ratio) {\n    int valid;\n',
     'void CUserStatus::AddNowLife(int chara_no, s16 amount, float ratio) {\n    { void ScriptPlayerLife(int, short *); ScriptPlayerLife(chara_no, (short *) &amount); }\n    int valid;\n'),
    ('port/src/gameloop.cpp', '#include "script.hpp"\n', '#include "script.hpp"\n#include "platform/modtext.hpp"\n'),
    ('port/src/gameloop.cpp', '    std::string         text;\n    gfx::LogicalMapping mapping = {};\n', '    std::string         text;\n    unsigned            mod_version = 0;\n    gfx::LogicalMapping mapping = {};\n'),
    ('port/src/gameloop.cpp', '    if (!g_fps.on) {\n        return nullptr;\n    }\n    std::string         text = GameFpsText();\n',
     '    if (!g_fps.on && !ModTextAny()) {\n        return nullptr;\n    }\n    std::string         text = g_fps.on ? GameFpsText() : std::string();\n'),
    ('port/src/gameloop.cpp', '    if (!g_fps.list || text != g_fps.text || !SameMapping(mapping, g_fps.mapping)) {\n        g_fps.list = OverlayRecord(text);\n',
     '    if (!g_fps.list || text != g_fps.text || g_fps.mod_version != ModTextVersion() || !SameMapping(mapping, g_fps.mapping)) {\n        g_fps.mod_version = ModTextVersion();\n        g_fps.list = ModsOverlayRecord(text);\n'),
    ('port/src/platform/input.cpp', 'void InputSetWheelMode(int mode) { g_wheel_mode = mode; }\n',
     'void InputSetWheelMode(int mode) { g_wheel_mode = mode; }\n\n// The mouse motion of this tick in pixels (y down), with the invert setting applied; zero while the cursor is free.\nvoid InputMouseLook(float *dx, float *dy) {\n    EnsureBindings();\n    *dx = g_mouse_dx;\n    *dy = g_mouse.invert_y ? -g_mouse_dy : g_mouse_dy;\n}\n'),
    ('port/src/platform/input.hpp', '// What one mouse wheel notch presses:',
     '// The mouse motion of this tick in pixels (y down, invert applied) for the cameras to turn by directly.\nvoid InputMouseLook(float *dx, float *dy);\n\n// What one mouse wheel notch presses:'),
    ('port/src/platform/mouse.cpp', 'bool Live() { return g_captured || !g_capture_enabled; }',
     'bool g_free_mode = false;     // a menu wants the pointer: not captured, buttons always count\nbool g_resume_capture = false;\n\nbool Live() { return g_free_mode || g_captured || !g_capture_enabled; }'),
    ('port/src/platform/mouse.cpp', 'bool MouseCaptured() { return g_captured; }',
     'bool MouseCaptured() { return g_captured; }\n\n// Free mode for menus: the pointer is released (and kept visible for the caller to hide) and clicks go to the game; leaving it captures again.\nvoid MouseSetFree(bool free) {\n    if (free == g_free_mode) {\n        return;\n    }\n    g_free_mode = free;\n    if (free) {\n        g_resume_capture = g_captured;\n        if (g_captured) {\n            SetCaptured(false);\n        }\n        g_dx = 0.0f;\n        g_dy = 0.0f;\n    } else {\n        SDL_Window *window = WindowHandle();\n        if (g_resume_capture && g_capture_enabled && window != nullptr && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0) {\n            SetCaptured(true);\n        }\n        g_resume_capture = false;\n    }\n}\n\nbool MouseIsFree() { return g_free_mode; }'),
    ('port/src/platform/mouse.cpp', '        case SDL_EVENT_KEY_DOWN:\n            if (g_captured && !event.key.repeat',
     '        case SDL_EVENT_KEY_DOWN:\n            if (!g_free_mode && g_captured && !event.key.repeat'),
    ('port/src/platform/mouse.hpp', 'bool MouseCaptured();',
     'bool MouseCaptured();\n\n// A menu is showing its own pointer: release the mouse (no look, no recapture by clicks) until freed again.\nvoid MouseSetFree(bool free);\nbool MouseIsFree();'),
    ('port/src/snd.cpp', '    { if (ModsSprite(texture->name, screen.x, screen.y, screen.width, screen.height, texel.x, texel.y, texel.width, texel.height)) { return; } }\n',
     '    CRect_i_ moved = screen;\n    { if (ModsSprite(texture->name, moved.x, moved.y, screen.width, screen.height, texel.x, texel.y, texel.width, texel.height)) { return; } }\n'),
    ('port/src/snd.cpp', '    std::array<gfx::Vertex2D, 4> quad = RectQuad(screen, texel, colour);\n    draw2d::DrawTextured(gfx::Primitive::Quads, quad, texture->tex0, SpriteTex1(), draw2d::SpriteState());',
     '    std::array<gfx::Vertex2D, 4> quad = RectQuad(moved, texel, colour);\n    draw2d::DrawTextured(gfx::Primitive::Quads, quad, texture->tex0, SpriteTex1(), draw2d::SpriteState());'),
    ('port/src/snd.cpp', 'bool ModsSprite(const char *, int, int, int, int, int, int, int, int);',
     'bool ModsSprite(const char *, int &, int &, int, int, int, int, int, int);'),
    ('port/src/clsmes.cpp', '{ extern bool ModsSprite(const char *, int, int, int, int, int, int, int, int); if (ModsSprite(texture->name, src.x, src.y, src.width, src.height, dst.x, dst.y, dst.width, dst.height)) { return; } }',
     'CRect_i_ moved = src;\n    { extern bool ModsSprite(const char *, int &, int &, int, int, int, int, int, int); if (ModsSprite(texture->name, moved.x, moved.y, src.width, src.height, dst.x, dst.y, dst.width, dst.height)) { return; } }'),
    ('port/src/clsmes.cpp', '    const float x0 = static_cast<float>(src.x);\n    const float y0 = static_cast<float>(src.y);\n    const float x1 = static_cast<float>(src.x + src.width);\n    const float y1 = static_cast<float>(src.y + src.height);\n    const float u0 = static_cast<float>(dst.x);',
     '    const float x0 = static_cast<float>(moved.x);\n    const float y0 = static_cast<float>(moved.y);\n    const float x1 = static_cast<float>(moved.x + src.width);\n    const float y1 = static_cast<float>(moved.y + src.height);\n    const float u0 = static_cast<float>(dst.x);'),
    ('ps2/src/battlemenu.cpp', 'void BattleManualInit(int *result, u_long128 *load_buffer) {\n    InitMenuManual(result, load_buffer);',
     'void BattleManualInit(int *result, u_long128 *load_buffer) {\n    { extern void ModsPageNote(); ModsPageNote(); }\n    InitMenuManual(result, load_buffer);'),
    ('ps2/src/menu_manual.cpp', '#include "menu_manual.hpp"\n',
     '#include "menu_manual.hpp"\nbool ModsPageOwns();\nvoid ModsPageStep();\nvoid ModsPageDraw();\n'),
    ('ps2/src/menu_manual.cpp', '        case MANUAL_MODE_SELECT: {\n            int  minimum;',
     '        case MANUAL_MODE_SELECT: {\n            if (ModsPageOwns()) {\n                ModsPageStep();\n                break;\n            }\n\n            int  minimum;'),
    ('ps2/src/menu_manual.cpp', 'void DrawManualMsg() {\n    if (ManualMenu.messages_ready == 0) {\n        return;\n    }\n',
     'void DrawManualMsg() {\n    if (ManualMenu.messages_ready == 0) {\n        return;\n    }\n\n    if (ModsPageOwns() && ManualMenu.mode == MANUAL_MODE_SELECT) {\n        ModsPageDraw();\n        return;\n    }\n'),
    ('ps2/src/menu_manual.cpp', 'int GetNowManualMenuMode() {',
     'void ModsManualClose() {\n    ManualMenu.mode = MANUAL_MODE_CLOSING;\n    ManualMenu.transition_frame = 0;\n    ManualMsg->Preset(MES_PRESET_SYSTEM);\n    CommonMenuMes3.SetBuff(ManualMenu.common_message_buffer);\n\n    for (int slot = 0; slot < 10; slot++) {\n        CommonMenuMes3.mes_no[slot] = -1;\n    }\n\n    CommonMenuMes3.Preset(MES_PRESET_NAME);\n    CommonMenuMes3.char_width = ManualMenu.char_width;\n    ManualMsg->SetBuff(ManualMenu.menu_message_buffer);\n    ManualMsg->Preset(MES_PRESET_SYSTEM);\n}\n\nint GetNowManualMenuMode() {'),
    # Save menu without the memory card question: file 0 is the autosave, manual saves go below it, one empty slot ends the list
    ('ps2/src/memcard.cpp', 'static int SaveMenuKeyFileSelect();\n',
     'static int SaveMenuKeyFileSelect();\nstatic int ModsSaveLast();\nstatic int ModsMcSelVisits = 0;\n'),
    ('ps2/src/memcard.cpp', '    SaveMenu.texture_ready = false;\n    SaveMenu.file_no = 0;\n    SaveMenu.loaded = false;\n    StartReadBG();',
     '    SaveMenu.texture_ready = false;\n    SaveMenu.file_no = 0;\n    SaveMenu.loaded = false;\n    ModsMcSelVisits = 0;\n    StartReadBG();'),
    ('ps2/src/memcard.cpp', 'static int SaveMenuKeyMcSelect() {\n    int prev_slot;\n\n    prev_slot = SaveMenu.file_no;\n',
     'static int ModsSaveLast() {\n    int last = 0;\n    for (int i = 1; i < 12; i++) {\n        if (McAccess.file_info[i].state != 0) {\n            last = i;\n        }\n    }\n    return last + 1 > 11 ? 11 : last + 1; // the one empty slot after the last save\n}\n\n'
     'static int SaveMenuKeyMcSelect() {\n    int prev_slot;\n    bool auto_go = false, auto_back = false;\n\n    if (SaveMenu.texture_ready) { // the memory card is not asked about: the first visit goes straight on, a return from the list leaves\n        if (ModsMcSelVisits == 0) {\n            auto_go = true;\n            SaveMenu.file_no = 0;\n        } else {\n            auto_back = true;\n        }\n        ModsMcSelVisits++;\n    }\n\n    prev_slot = SaveMenu.file_no;\n'),
    ('ps2/src/memcard.cpp', '    if (GamePad.Down(PAD_CIRCLE)) {\n        switch (SaveMenu.mode) {\n            case SAVE_MENU_MODE_LOAD:',
     '    if (GamePad.Down(PAD_CIRCLE) || auto_back) {\n        switch (SaveMenu.mode) {\n            case SAVE_MENU_MODE_LOAD:'),
    ('ps2/src/memcard.cpp', '    if (GamePad.Down(PAD_CROSS) && SaveMenu.texture_ready) {\n        SaveMenu.key_no = SAVE_KEY_CHECK_MC_TYPE;',
     '    if ((GamePad.Down(PAD_CROSS) || auto_go) && SaveMenu.texture_ready) {\n        SaveMenu.key_no = SAVE_KEY_CHECK_MC_TYPE;'),
    ('ps2/src/memcard.cpp', '    DrawMenuClsMes(&CommonMenuMes2, (int) text_pos[0], (int) text_pos[1]);\n    int hand_x = -1;',
     '    if (SaveMenu.key_no != SAVE_KEY_MC_SELECT) {\n        DrawMenuClsMes(&CommonMenuMes2, (int) text_pos[0], (int) text_pos[1]);\n    }\n    int hand_x = -1;'),
    ('ps2/src/memcard.cpp', '            for (int i = 0; i < 12; i++) {\n                SAVEDATA_INFO *file = &McAccess.file_info[i];\n\n                if (file != NULL) {\n                    if (file->state == 0) {\n                        DrawNewFileTemplete((int) board_x, (int) y, alpha);\n                    } else {\n                        DrawSaveBoard(file, SaveMenuMojiTextbl, (int) board_x, (int) y, bright, alpha);\n                    }\n',
     '            for (int i = 0; i < 12 && i <= (ModsSaveLast() > 1 ? ModsSaveLast() : 1); i++) {\n                SAVEDATA_INFO *file = &McAccess.file_info[i];\n\n                if (file != NULL) {\n                    if (file->state == 0) {\n                        DrawNewFileTemplete((int) board_x, (int) y, alpha);\n                    } else {\n                        SAVEDATA_INFO shown = *file;\n                        shown.file_no = i;\n                        if (i == 0) { // the autosave is labelled in the board\'s own name font\n                            static const short label[] = {162, 208, 207, 202, 206, 188, 209, 192, 0};\n                            std::memset(shown.name, 0, sizeof shown.name);\n                            std::memcpy(shown.name, label, sizeof label);\n                        }\n                        DrawSaveBoard(&shown, SaveMenuMojiTextbl, (int) board_x, (int) y, bright, alpha);\n                    }\n'),
    ('ps2/src/memcard.cpp', '    if (GamePad.Down(PAD_DOWN)) {\n        SaveMenu.file_no++;\n\n        if (SaveMenu.file_no >= 12) {\n            SaveMenu.file_no--;\n        }\n    }\n\n    if (GamePad.Down(PAD_UP) && 0 < SaveMenu.file_no) {\n        SaveMenu.file_no--;\n    }\n',
     '    if (SaveMenu.access_kind == SAVE_ACCESS_SAVE && SaveMenu.file_no < 1) {\n        SaveMenu.file_no = 1; // the autosave slot is not written by hand\n        prev_file = 1;\n    }\n\n    if (GamePad.Down(PAD_DOWN)) {\n        SaveMenu.file_no++;\n\n        if (SaveMenu.file_no > ModsSaveLast()) {\n            SaveMenu.file_no--;\n        }\n    }\n\n    if (GamePad.Down(PAD_UP) && (SaveMenu.access_kind == SAVE_ACCESS_SAVE ? 1 : 0) < SaveMenu.file_no) {\n        SaveMenu.file_no--;\n    }\n'),
    ('ps2/src/memcard.cpp', '    if (GamePad.Down(PAD_CIRCLE)) {\n        SaveMenu.key_no = SAVE_KEY_MC_SELECT;\n        SaveMenu.file_no = McAccess.port;\n        SaveMenu.step_time = 0;',
     '    if (GamePad.Down(PAD_CIRCLE)) {\n        SaveMenu.key_no = SAVE_KEY_MC_SELECT; // the memory card question is skipped: this leaves the menu\n        SaveMenu.file_no = McAccess.port;\n        SaveMenu.step_time = 0;'),
    ('ps2/src/menu_save.cpp', '        for (digits = GetNumberKeta(number); 0 < digits; digits--) {\n            clip_y = draw_y;',
     '        for (digits = number == 0 ? 0 : GetNumberKeta(number); 0 < digits; digits--) { // the autosave board has no number\n            clip_y = draw_y;'),
    ('ps2/src/dun/gameloop.cpp', '    if (BtActStatus.camera_hold == 0) {\n        NowCamera__3->Step(1);\n    }\n\n    if (Water_Splash_actFlag != 0) {',
     '    if (BtActStatus.camera_hold == 0) {\n        NowCamera__3->Step(1);\n        { extern void ModsFirstPerson(); ModsFirstPerson(); }\n    }\n\n    if (Water_Splash_actFlag != 0) {'),
    ('port/src/editloop.cpp', '            if (EdCheckViewMode() == 0) {\n                NowCamera->Step(1);\n            }\n\n            if (goto_cmp_event != 0) {',
     '            if (EdCheckViewMode() == 0) {\n                NowCamera->Step(1);\n                { extern void ModsFirstPersonTown(); ModsFirstPersonTown(); }\n            }\n\n            if (goto_cmp_event != 0) {'),
    ('port/src/editloop_init.cpp', '    EdLoadMainChara("chara/c01d.chr", "info.cfg", &CharaBuffer);',
     '    { extern void ModsLoadTownChara(); ModsLoadTownChara(); }'),
    ('port/src/editloop_init.cpp', '    SetDataBuffer(&CharaBuffer, 115000);',
     '    SetDataBuffer(&CharaBuffer, 160000); // room for the dungeon bodies played in towns'),
    ('ps2/src/character.cpp', '    index = -1;\n    found = NULL;\n\n    // The motion numbers of the sets',
     '    index = -1;\n    found = NULL;\n    { extern int ModsRemapMotion(CCharacter *, int); motion_no = ModsRemapMotion(this, motion_no); }\n\n    // The motion numbers of the sets'),
    ('ps2/src/character.cpp', '    motion_no = this->motion_no;\n\n    if (motion_no < 0) {\n        return;\n    }\n\n    index = -1;\n    shadow = NULL;',
     '    motion_no = this->motion_no;\n    { extern int ModsRemapMotion(CCharacter *, int); motion_no = ModsRemapMotion(this, motion_no); }\n\n    if (motion_no < 0) {\n        return;\n    }\n\n    index = -1;\n    shadow = NULL;'),
    ('port/src/title/titleloop.cpp', '        case TITLE_STEP_MENU:\n            CLogo.Move();\n\n            if (CCursol.Move()) {\n                if (GamePad.Down(PAD_UP)) {',
     '        case TITLE_STEP_MENU:\n            CLogo.Move();\n\n            if (CCursol.Move()) {\n                { extern int ModsTitleHover(); int row = ModsTitleHover(); if (row >= 0) { if (row != CCursol.select) { TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f); } CCursol.select = row; opcnt = 0; } }\n                if (GamePad.Down(PAD_UP)) {'),
    ('ps2/src/editmenu.cpp', '                blocks[2] = EdMenuExTextureBlock2;\n                InitMenuManual(blocks, work);',
     '                blocks[2] = EdMenuExTextureBlock2;\n                { extern void ModsPageNote(); ModsPageNote(); }\n                InitMenuManual(blocks, work);'),
    ('ps2/src/menu_draw.cpp', 'void InitMenuMesSet(int mode, short *messages) {\n    CommonMenuMes1.tex_buff = MesWinTexBuff_01;',
     'void InitMenuMesSet(int mode, short *messages) {\n    { void ModTextPatchMenu(void *); ModTextPatchMenu(messages); }\n    CommonMenuMes1.tex_buff = MesWinTexBuff_01;'),
    # Packaged copies: with no data/ or save/ next to anything, fall back to the folder holding the exe, never to the current directory
    # (a shortcut or a console opened elsewhere moved the saves around).
    ('port/src/platform/paths.cpp', '        std::error_code error;\n        return fs::current_path(error) / name;\n    }\n    return base / "chronicle" / name;',
     '        std::error_code error;\n        fs::path        executable = ExecutableDirectory();\n        return (executable.empty() ? fs::current_path(error) : executable) / name;\n    }\n    return base / "chronicle" / name;'),
]

def apply_patches():
    bad = 0
    for rel, old, new in PATCHES:
        f = DST / rel
        t = f.read_bytes().decode('utf-8', errors='surrogateescape')
        if old not in t:
            print(f'PATCH MISMATCH {rel}: {old[:60]!r}'); bad += 1; continue
        f.write_bytes(t.replace(old, new).encode('utf-8', errors='surrogateescape'))
    print(f'applied {len(PATCHES) - bad}/{len(PATCHES)} patches')

def main():
    if DST.exists():
        try:
            shutil.rmtree(DST)
        except PermissionError:
            print('note: could not clear the old tree (no delete permission); overwriting in place')
    for sub in ('ps2/src', 'ps2/include', 'port/include', 'port/src'):
        shutil.copytree(SRC / sub, DST / sub, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('tests') if sub == 'port/src' else None)
    total, files = 0, []
    for p in sorted(DST.rglob('*')):
        if not p.is_file() or p.suffix not in CODE_EXT:
            continue
        rel = p.relative_to(DST).as_posix()
        if rel.startswith(HOST_DIRS):
            continue
        text = p.read_bytes().decode('utf-8', errors='surrogateescape')
        new, n = rewrite_long(text)
        if n:
            p.write_bytes(new.encode('utf-8', errors='surrogateescape'))
            total += n; files.append((n, rel))
    # Windows replacements
    (DST / 'tools/dcdata').mkdir(parents=True, exist_ok=True)
    for name in ('dcdata.hpp', 'main.cpp'):
        shutil.copy(SRC / 'tools/dcdata' / name, DST / 'tools/dcdata' / name)
    shutil.copy(WIN / 'memory_win.cpp', DST / 'port/src/platform/memory.cpp')
    shutil.copy(WIN / 'crash_win.cpp', DST / 'port/src/platform/crash_win.cpp')
    shutil.copy(WIN / 'mods_win.cpp', DST / 'port/src/platform/mods.cpp')
    shutil.copy(WIN / 'mods.hpp', DST / 'port/src/platform/mods.hpp')
    shutil.copy(WIN / 'script_win.cpp', DST / 'port/src/script.cpp')
    shutil.copy(WIN / 'script.hpp', DST / 'port/src/script.hpp')
    shutil.copy(WIN / 'modmodel_win.cpp', DST / 'port/src/modmodel.cpp')
    shutil.copy(WIN / 'modmodel.hpp', DST / 'port/src/modmodel.hpp')
    shutil.copy(WIN / 'mod_api.h', DST / 'port/src/mod_api.h')
    shutil.copy(WIN / 'moddata_win.cpp', DST / 'port/src/moddata.cpp')
    shutil.copy(WIN / 'moddata.hpp', DST / 'port/src/moddata.hpp')
    shutil.copy(WIN / 'mestext_win.cpp', DST / 'port/src/platform/mestext.cpp')
    shutil.copy(WIN / 'mestext.hpp', DST / 'port/src/platform/mestext.hpp')
    shutil.copy(WIN / 'modtext_win.cpp', DST / 'port/src/platform/modtext.cpp')
    shutil.copy(WIN / 'modtext.hpp', DST / 'port/src/platform/modtext.hpp')
    shutil.copy(WIN / 'ghost_win.cpp', DST / 'port/src/ghost.cpp')
    shutil.copy(WIN / 'ghost.hpp', DST / 'port/src/ghost.hpp')
    shutil.copy(WIN / 'guestworld_win.cpp', DST / 'port/src/guestworld.cpp')
    shutil.copy(WIN / 'nativeui_win.cpp', DST / 'port/src/nativeui.cpp')
    shutil.copy(WIN / 'nativeui.hpp', DST / 'port/src/nativeui.hpp')
    shutil.copy(WIN / 'guestworld.hpp', DST / 'port/src/guestworld.hpp')
    shutil.copy(WIN / 'net_win.cpp', DST / 'port/src/platform/net.cpp')
    shutil.copy(WIN / 'net.hpp', DST / 'port/src/platform/net.hpp')
    shutil.copy(WIN / 'setup_win.cpp', DST / 'port/src/platform/setup.cpp')
    shutil.copy(WIN / 'CMakeLists.win.txt', DST / 'CMakeLists.txt')
    (DST / 'win').mkdir(parents=True, exist_ok=True)
    shutil.copy(WIN / 'weakcc.py', DST / 'win/weakcc.py')
    apply_patches()
    print(f'generated {DST}')
    print(f'long rewritten at {total} sites in {len(files)} files:')
    for n, rel in sorted(files, reverse=True)[:25]:
        print(f'  {n:4d} {rel}')

main()
