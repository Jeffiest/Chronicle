# Sequence name to soundtrack track

Keys for `soundtrack.json` (see `SOUNDTRACK.md`). The names are the 100 sequences in `sound/tbl/sqtbl.txt`. The track
titles are those of the 46-track *Dark Cloud Original Soundtrack* (Sony Computer Entertainment, 2001), as the English
fan listing at rpgfan.com spells them (they differ from the game's own romanisation, for example "Norn Village" is Norune).

How sure each row is:
- **confirmed**: the game was run and traced (`DC_AUDIO_TRACE=1`), and the place matches the track's title.
- **by name**: the sequence name follows the game's own numbering (d01-d07 are the seven dungeons) and the title names
  the place, but no one has listened to it.
- **guess**: a candidate from the title list. Check by ear before shipping a mapping.

The suffix says what the sequence is: `_a` is music, `_e` is an ambience or effect layer (it plays on port 1 beside the music).
Map a `_a` sequence to a recording; leave the `_e` layers alone unless you have an ambience recording.

## Towns (port 0 is the music, port 1 the ambience)

| Sequence | Where | OST track | Sure |
|---|---|---|---|
| `e01a_a.sq` | Norune | 07 Norn Village | confirmed (edit:0) |
| `e02a_a.sq` | Matataki | 11 Matatagi Village | confirmed (edit:1) |
| `e03a_a.sq` | Queens | 18 Queens | confirmed (edit:2) |
| `e04a_a.sq` | Muska Racka | 27 Masca Rakka | confirmed (edit:3) |
| `e05a_a.sq` | the fifth town (Brownboo) | 14 Blaunpou | confirmed town (edit:4), title by name |
| `e01a_e`..`e01e_e`, `e02a_e`..`e02d_e`, `e03a_e`..`e03d_e`, `e04a_e`..`e04d_e` | town ambience by time of day or place | none | not music |

## Menus and title

| Sequence | Where | OST track | Sure |
|---|---|---|---|
| `t01a_e.sq` | title screen | 01 Dark Cloud Main Theme | confirmed screen, guess track |
| `m01a_e.sq` | dungeon floor select | none known | confirmed screen |

## Dungeons: floors (`a`, `b`, `c` are floor groups of one dungeon)

| Sequences | Dungeon | OST track | Sure |
|---|---|---|---|
| `d01a_a` `d01b_a` `d01c_a` | 1 Divine Beast Cave | 08 The Cave of Djinni | by name |
| `d02a_a` `d02b_a` `d02c_a` | 2 Wise Owl Forest | 16 Owl Forest | by name |
| `d03a_a` `d03b_a` `d03c_a` | 3 Shipwreck | 20 A Sinking Ship | by name |
| `d04a_a` `d04b_a` `d04c_a` | 4 Sun and Moon Temple | 30 The Palace of the Sun and Moon | by name |
| `d05a_a` `d05b_a` `d05c_a` | 5 Moon Sea | 35 Ocean and Moon | by name |
| `d06a_a` `d06b_a` `d06c_a` | 6 Gallery of Time | 40 The Corridor of Time | by name |
| `d07a_a` `d07b_a` `d07c_a` | 7 Demon Shaft | 41 Castle of Dark Heaven | guess |

Each dungeon has three sequences and the soundtrack has one track for it, so the in-game groups may be the same song at three
volumes or three arrangements. Listen to each before mapping all three to one file.

## Bosses (`b01a_a` to `b07a_a`, plus `b06b_a`)

Dungeon order. Candidates from the title list, all guesses: 09 The Spirit King and 10 The Djinni Doran (dungeon 1),
17 The Guardians of the Forest (dungeon 2), 21 DUEL, 23 The Ice Queen, 37 The Rowdy, 39 Black Shadows, 43 Last Battle.
12 Battle! is probably the ordinary-fight theme and 44 Emergency a danger cue.

## Events and scenes (guesses only)

`o01a_a`, `o01b_a`, `o02a_a`, `o03a_a`, `o03b_a`, `o03b_e`, `o04a_a`, `o04a_e`, `o04b_a`, `o04c_a` (`o` plays in the opening
and at the start: `o04b_a` played when `--jump dungeon:0` started), `s04a`, `s09a`-`s09d`, `s13a`, `s30a`, `s50a` (`s` are
scenes) and `v01a_a` to `v19a_a` (19 event pieces). Candidates from the title list: 02 The Ceremony, 03 The Village
Festival, 04 The Resurrection of Norn Village, 05 The Destruction of Norn Village, 06 Open Your Eyes, 13 Reminiscence,
15 Legend of Hunter, 19 and 24 Broken Promise, 25 The Daily Life, 26 Standup!!, 28 Departure, 29 If You Strain Your Ears,
31 Takeoff, 32 The King's Curse, 33 Aero Drop, 34 Mission!!, 36 The Land of Hope, 38 Memories, 42 Time of Destiny,
45 Two Moons, 46 Main Theme (Bossa Nova Version). There is no safe order from the names alone.

## What the scan settled and what it did not

Settled by running the game (`--jump edit:N` for every map id 0 to 70 and 99, the title, the attract movie, the floor select):
the towns, the title (`t01a_e` is not in any set: it is started by the title code), the attract movie (set 25, `v02a_a`), the
fifth town (`e05a_a`), the East King scene (set 22, `v09a_a`), and which interiors play `s04a_a`, `s13a_a`, `s30a_a`, `s50a_a`,
`v15a_a`. The dungeon sets 100 to 106 hold the floor music (three sequences each, chosen by `SQ_Play(port, k)`), and sets 150 to 157
the bosses, by the order of the files.

Not settled: which scene starts each `v`, `o` and boss set (scripts and events choose them with numbers held in the map
data), and which dungeon floor plays which of its three sequences. A headless `--jump dungeon:N` enters the floor without the
music (`map_jump_bgm_play` is 0 there), so a real play-through is needed for those. Nothing here has been listened to: the OST
title next to each sequence is a match from the place it plays and the title's wording, not from the audio.

## Filling in the rest

Run the game with `DC_AUDIO_TRACE=1`, play the scene, and read the `SQ_Play ... (name.sq)` line that appears with the music you
hear. Add the row here with "confirmed". The 100 names are in `sound/tbl/sqtbl.txt` in the extracted data.

## Sound sets: every sequence by the set the game loads it in

The game loads music as a numbered set (`SndBgmLoad(N)` loads `sound/bgm/bgmN.snd`; `SQ_Play(port, k)` then plays the set's k-th sequence).
The sequence names inside each set were read from the data. "Heard" names a traced run that played it.

| Set | Sequences | Heard in the game |
|---|---|---|
| 0 | `o04c_a.sq` | title screen start-up (op_d.cpp loads set 0 first) |
| 1 | `e01a_a.sq` | Norune (edit:0) |
| 2 | `e02a_a.sq` | Matataki (edit:1) and its sub maps edit:11, edit:13 |
| 3 | `e03a_a.sq` | Queens (edit:2), edit:19, edit:43 |
| 4 | `e04a_a.sq` | Muska Racka (edit:3), edit:42 |
| 5 | `o04b_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 6 | `s04a_a.sq` | edit:14, edit:52, edit:53, edit:54 |
| 7 | `v03a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 8 | `o01a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 9 | `o01b_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 10 | `o03a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 11 | `o02a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 12 | `o03b_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 13 | `o04a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 14 | `e05a_a.sq` | fifth town (edit:4), edit:41 |
| 15 | `s13a_a.sq` | edit:23 |
| 16 | `v04a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 17 | `v05a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 18 | `v01a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 19 | `v06a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 20 | `v07a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 21 | `v08a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 22 | `v09a_a.sq` | edit:25 (also loaded by eastking.cpp as set 0x16) |
| 23 | `v10a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 24 | `v11a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 25 | `v02a_a.sq` | attract movie (rushmovi.cpp loads set 25) |
| 26 | `v12a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 27 | `v13a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 28 | `v14a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 29 | `v15a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 30 | `s30a_a.sq` | edit:38 (with b04a_a), edit:40 |
| 31 | `v11b_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 32 | `v16a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 33 | `v17a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 34 | `v18a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 35 | `v19a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 36 | `s50a_a.sq` | edit:60, edit:61 (with s50a_e) |
| 100 | `d01a_a.sq`, `d01b_a.sq`, `d01c_a.sq` | dungeon 1 floors (dun/gameloop.cpp loads 100+dungeon) |
| 101 | `d02a_a.sq`, `d02b_a.sq`, `d02c_a.sq` | dungeon 2 floors |
| 102 | `d03a_a.sq`, `d03b_a.sq`, `d03c_a.sq` | dungeon 3 floors; also edit:35 |
| 103 | `d04a_a.sq`, `d04b_a.sq`, `d04c_a.sq` | dungeon 4 floors; edit:36 plays d04c_a |
| 104 | `d05a_a.sq`, `d05b_a.sq`, `d05c_a.sq` | dungeon 5 floors; edit:37 plays d05c_a |
| 105 | `d06a_a.sq`, `d06b_a.sq`, `d06c_a.sq` | dungeon 6 floors; edit:34 plays d06b_a |
| 106 | `d07a_a.sq`, `d07b_a.sq`, `d07c_a.sq` | dungeon 7 floors |
| 110 | `d02a_a.sq`, `d02b_a.sq`, `d02c_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 120 | `d03a_a.sq`, `d03b_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 130 | `d04a_a.sq`, `d04c_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 150 | `b01a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 151 | `b02a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 152 | `b03a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 153 | `b04a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 154 | `b05a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 155 | `b06a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 156 | `b06b_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 157 | `b07a_a.sq` | not reached: loaded by a script or event the scan did not trigger |
| 200 | `m01a_e.sq` | dungeon floor select (m01a_e) |
| 201 | `b01a_a.sq` | not reached: loaded by a script or event the scan did not trigger |

## Matched by audio

A copy of the 46-track soundtrack (FLAC files) was compared with the port's own rendering
of every sequence. Method: each sequence was played from a title-screen run with `DC_BGM_TEST=<set>:<track>` and recorded with `DC_AUDIO_WAV`; both sides
became 22 kHz mono, then pitch-class (chroma) frames, and the first 30 s of the render was aligned against the first 110 s of each OST track by dynamic time warping.
A row is listed when the best track's distance is under 0.45 and under 70% of the second best's. The recording of Muska Racka in the game with the matched FLAC
plays and is closer to the FLAC (0.20) than to the synth render (0.46). **Nobody has listened to these matches.** The towns of Norune, Matataki and
Queens, the dungeon 1, 2, 5 and 7 floors and most bosses did not match (the OST arrangements differ from the sequences, or the intro differs).

| Sequence | Set | OST track | Distance | Next best |
|---|---|---|---|---|
| `b03a_a.sq` | 152 | 39 氷の女王 (The Ice Queen) | 0.24 | 0.59 |
| `b04a_a.sq` | 153 | 15 The King's Curse | 0.31 | 0.49 |
| `b06a_a.sq` | 155 | 07 Last Battle | 0.23 | 0.60 |
| `d03a_a.sq` | 102 | 40 沈没船 (A Sinking Ship) | 0.38 | 0.63 |
| `d04a_a.sq` | 103 | 32 太陽と月の神殿 (The Palace of the Sun and Moon) | 0.22 | 0.55 |
| `d04b_a.sq` | 103 | 32 太陽と月の神殿 (The Palace of the Sun and Moon) | 0.20 | 0.53 |
| `d04c_a.sq` | 103 | 32 太陽と月の神殿 (The Palace of the Sun and Moon) | 0.21 | 0.53 |
| `d06a_a.sq` | 105 | 35 時の回廊 (The Corridor of Time) | 0.37 | 0.57 |
| `d06b_a.sq` | 105 | 35 時の回廊 (The Corridor of Time) | 0.32 | 0.58 |
| `d06c_a.sq` | 105 | 35 時の回廊 (The Corridor of Time) | 0.33 | 0.59 |
| `e04a_a.sq` | 4 | 27 ムスカ・ラッカ (Masca Rakka) | 0.39 | 0.64 |
| `e05a_a.sq` | 14 | 06 Factory | 0.28 | 0.51 |
| `o01a_a.sq` | 8 | 30 儀式 (The Ceremony) | 0.22 | 0.51 |
| `o01b_a.sq` | 9 | 45 魔人復活 (Demon Resurrection) | 0.17 | 0.48 |
| `o03a_a.sq` | 10 | 37 村祭り (The Village Festival) | 0.43 | 0.67 |
| `o03b_a.sq` | 12 | 23 ノルン村崩壊 (The Destruction of Norn Village) | 0.26 | 0.66 |
| `o04c_a.sq` | 0 | 34 旅立ち (Departure) | 0.23 | 0.56 |
| `s13a_a.sq` | 15 | 19 イエロードロップ (Yellow Drop) | 0.29 | 0.58 |
| `s30a_a.sq` | 30 | 03 Castle of Dark Heaven | 0.21 | 0.46 |
| `v01a_a.sq` | 18 | 44 耳を澄まして (If You Strain Your Ears) | 0.21 | 0.44 |
| `v03a_a.sq` | 7 | 33 希望の地 (The Land of Hope) | 0.13 | 0.53 |
| `v08a_a.sq` | 21 | 12 Stand Up!! | 0.14 | 0.43 |
| `v09a_a.sq` | 22 | 09 Memories | 0.22 | 0.59 |
| `v10a_a.sq` | 23 | 17 Time of Destiny | 0.22 | 0.49 |
| `v11a_a.sq` | 24 | 05 Emergency | 0.30 | 0.48 |
| `v12a_a.sq` | 26 | 08 Legend of Hunter | 0.14 | 0.59 |
| `v13a_a.sq` | 27 | 13 Take Off | 0.20 | 0.44 |
| `v16a_a.sq` | 32 | 01 Broken Promise (Orgel Version) | 0.18 | 0.55 |
| `v17a_a.sq` | 33 | 10 Mission!! | 0.44 | 0.63 |

Notable: `e05a_a` (the fifth town) matched "Factory", not Brownboo as the title guess above says, so that guess is wrong; `e04a_a` matched Masca Rakka as expected;
`d04a_a`, `d04b_a`, `d04c_a` all match the Palace of the Sun and Moon and `d06a_a` to `d06c_a` the Corridor of Time, as the dungeon numbering predicted.
