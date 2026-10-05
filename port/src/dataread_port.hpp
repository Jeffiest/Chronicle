#pragma once

// Which disc the extracted data came from. The port runs one build of the game's code on either:
// true for the NTSC 1.02 release, false for PAL. Read from the data, once it is indexed.
bool PortNtscData();
