#pragma once

// The mouse on the Register Name screen (ps2/src/battle_globals.cpp). While the screen is open the
// mouse is a pointer: hovering a control puts the game's own cursor on it, and a click presses the
// pad button that would act there, so the game's code does the rest.

// Reads the pointer for this tick, ahead of the game's reading of the pad: moves the cursor to what
// the pointer is over and decides which pad buttons a click presses. Does nothing, and leaves the
// mouse to the pad, unless the screen is open.
void NameMouseUpdate();

// The pad buttons the mouse presses this tick, as CGamePad::Down sees them.
int NameMouseSyntheticDown();
