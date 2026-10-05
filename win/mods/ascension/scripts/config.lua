-- Ascension: every number you might want to change lives here. Restart the game after editing.
local D = {
  -- character levels --------------------------------------------------------------------------------------------------
  max_level       = 40,
  xp_to_next      = function(level) return 40 + 22 * level + 3 * level * level end, -- XP needed to go from `level` to level+1
  hp_per_level    = 3,      -- max HP gained each level (the game's own max-HP upgrades still apply on top)
  damage_per_level = 0.025, -- +2.5% damage dealt per level above 1
  mitigation_per_level = 0.008, -- -0.8% damage taken per level above 1 (capped below)
  mitigation_cap  = 0.40,

  -- skill tree ----------------------------------------------------------------------------------------------------------------
  points_per_level = 1,        -- skill points earned per level above 1
  bonus_points_every_5 = 1,    -- plus this many extra at levels 6, 11, 16...
  focus_regen_per_second = 6,  -- Focus is the resource active skills spend (100 base)
  shockwave_radius = 20,       -- world units around the player
  respec_gilda_per_level = 100,

  -- combat ------------------------------------------------------------------------------------------------------------------
  crit_chance     = 0.08,   -- base chance a hit crits
  crit_per_speed  = 0.0005, -- extra crit chance per point of weapon speed (Toan's dagger has ~76 => +3.8%)
  crit_multiplier = 1.75,
  variance        = 0.10,   -- each hit varies by +/- this much

  -- enemy scaling (by dungeon 0-6 and floor) ----------------------------------------------------------------------------------
  hp_per_dungeon  = 0.10,   -- +10% enemy HP for each dungeon
  hp_per_floor    = 0.035,  -- +3.5% for each floor
  defense_per_floor = 0.15, -- +0.15 defense per floor (rounded)
  damage_taken_per_floor = 0.02, -- enemies hit harder by 2% per floor, 8% per dungeon
  damage_taken_per_dungeon = 0.08,

  -- champions (mini-bosses, after DarkCloud-Expanded) --------------------------------------------------------------------------
  champion_chance = 0.08,   -- chance each regular enemy is a champion (about one per floor)
  champion_melee  = 1.5,      -- damage taken is multiplied by this while a champion is within champion_threat_range of you
  champion_threat_range = 22, -- world units
  champion_scale  = 1.5,      -- how much bigger champions are drawn
  champion_hp     = 3.0,
  champion_defense = 1.5,
  champion_xp     = 3.0,
  champion_gilda  = 3.0,
  champion_max_base_hp = 600, -- enemies tougher than this are bosses and never become champions
  flavor_rare_chance   = 5,   -- % for the rare themed drop of that enemy
  flavor_common_chance = 30,  -- % for the common themed drop

  -- loot ---------------------------------------------------------------------------------------------------------------------
  loot_rolls = true,        -- weapons you pick up may roll bonus stats: Fine 25%, Superior 10%, Exquisite 4%, Legendary 1%

  -- XP and rewards -----------------------------------------------------------------------------------------------------------
  xp_per_max_hp   = 0.45,   -- kill XP = enemy max HP (before scaling) * this + its exp stat
  gilda_bonus     = 0.5,    -- extra gilda on a kill = enemy money * this (champions use champion_gilda instead)
}

-- Anything set in the in-game settings screen (stored under the "ascension" namespace) overrides the value above; numbers and
-- booleans only. Functions such as xp_to_next are not overridable.
local shared = dc.shared_get
return setmetatable({}, {
  __index = function(_, key)
    local v = shared and shared("ascension", key)
    if v ~= nil and type(v) == type(D[key]) then return v end
    return D[key]
  end,
})
