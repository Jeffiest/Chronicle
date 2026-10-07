#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "pcm.hpp"

namespace audio {

// <save>/soundtrack/soundtrack.json names recordings to play instead of the game's sequences:
//   {"tracks": {"e01a_a.sq": {"file": "norune.ogg", "gain": 1.0, "loop": true,
//                             "loop_start": 12.5, "loop_end": 150.0, "reference": "https://..."}}}
// The keys are the sequences' names (DC_AUDIO_TRACE=1 prints each as it starts). Times are seconds.
// reference is for people and is never read. Files are decoded when their song first starts.
//
// The recording for a sequence while the soundtrack option is on, or null: the option is off, the
// sequence has no entry, or its file will not decode (reported once on stderr).
std::shared_ptr<const PcmTrack> SoundtrackFor(std::string_view sequence);

// Reads the mapping again at the next lookup.
void SoundtrackReload();

} // namespace audio
