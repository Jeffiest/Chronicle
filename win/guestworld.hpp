#pragma once

// Guest world (Windows fork multiplayer): a guest plays in the host's world while keeping their own inventory and gilda. See guestworld_win.cpp.
#include <string>

class CSaveData;

std::string GwCapture();                        // the host: the world to send ("" before a game is loaded)
void        GwSetHostWorld(const std::string &w); // the guest: the host's world arrived (enter it, or refresh it)
void        GwLeave();                          // the guest: back to their own world (also resets the stored host world)
bool        GwActive();                         // true while the guest is in the host's world
bool        GwTakeJump();                       // true once after a world change asked the game to jump maps
void        GwAfterLoad();                      // a save was loaded
void        GwFixSave(CSaveData *copy);         // the copy of the save about to be written
