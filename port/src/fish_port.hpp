#pragma once

// game.smooth_fish_sizes. Retail clamps every roll past the largest size to the largest size, so
// the top display centimetre holds the whole tail (8.5 fish in a million) and stands out against
// the centimetre below it: 7.9 times as many for Baron Garayan, 2.1 for Mardan Garayan. The lift
// moves a fish rolled above its kind's usual size up the range by a share t^kFishSizeLiftExponent
// * (1 - t) of it, t being how far up it rolled, so that the centimetres approaching the largest
// fill in to about the largest's own share and the sizes ramp into it. The largest size keeps
// exactly its retail rarity: only a roll that retail clamps lands on it.
inline constexpr float kFishSizeLiftExponent = 1.2f;

// The lift's strength for a kind whose sizes span range (largest less usual, in size units): the
// strength that leaves the centimetre below the largest holding as many fish as the largest. Wide
// ranges are lifted more, since there the clamp stands out more; a range of 4 cm or less, none.
float FishSizeLift(float range);

float SmoothFishSize(float size, float base_size, float max_size);
