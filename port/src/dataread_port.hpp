#pragma once

// Which disc the extracted data came from. The port runs one build of the game's code on either:
// true for the NTSC 1.02 release, false for PAL. Read from the data when InitCDFile indexes it,
// which RunGame does first; false (PAL) until then.
bool PortNtscData();
