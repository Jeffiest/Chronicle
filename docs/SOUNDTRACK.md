# Soundtrack option

Audio > Soundtrack chooses what plays for the game's music.

- **PS2** (default): the game's own sequences, played by the port's synth.
- **Original**: your own recordings of the soundtrack, played in place of the sequences you name.

The port ships no recordings and fetches none. You supply files from a soundtrack you own.

## Setting it up

1. Create `soundtrack/` in the save folder (the folder holding `config.json`).
2. Put `.wav`, `.flac` or `.ogg` (Vorbis) files in it. Any sample rate; mono is doubled to stereo.
3. Write `soundtrack/soundtrack.json`:

```json
{
  "tracks": {
    "e01a_a.sq": {
      "file": "norune.ogg",
      "gain": 1.0,
      "loop": true,
      "loop_start": 12.5,
      "loop_end": 150.0,
      "reference": "https://music.youtube.com/playlist?list=OLAK5uy_mRH6a_TtG_WNRPon8FvMqOJ3MHr_SLUfY"
    }
  }
}
```

4. Set Audio > Soundtrack to Original (or `"soundtrack": "ost"` under `audio` in `config.json`).

A song picks its recording when it starts, so a change of option applies from the next song. A song
with no entry, or whose file will not decode (reported on stderr), plays as the PS2 sequence.

| Key | Meaning |
|---|---|
| `file` | the recording, relative to `soundtrack/` (required) |
| `gain` | volume multiplier, default 1. The game's per-song volume still applies |
| `loop` | repeat at the end, default true. False plays once |
| `start` | seconds into the file where the song begins (default 0), so one track can serve several sequences at different sections |
| `loop_start`, `loop_end` | seconds. The song jumps from `loop_end` back to `loop_start` (default: `start`); 0 for `loop_end` is the end of the file |
| `reference` | for people: where to find the version this entry expects. Never read |

Files are decoded whole when their song first starts (about 10 MB of memory per minute of stereo audio).

## Quick setup from the shipped matches

`docs/soundtrack/matches.json` says which track of the 46-track Original Soundtrack goes with each sequence (58 of the game's
sequences so far; each is marked as matched by ear or by audio comparison only). With your own copy of the soundtrack in one folder:

```
python tools/soundtrack/make_soundtrack_json.py --music-dir "D:/Music/Dark Cloud OST" --out "<save folder>/soundtrack/soundtrack.json"
```

The script finds each file by the track's title in its name (Japanese or English), writes the entries, and lists any track it
could not find (those sequences keep the PS2 sound). `--only-ear` leaves out the matches nobody has listened to. Then set
Audio > Soundtrack to Original.

## Finding the key for a song

Keys are the sequences' names. Run the game with `DC_AUDIO_TRACE=1` and watch stderr:

```
audio: SQ_Play port 0 seq 0 (e01a_a.sq) volume 102 (table)
```

Names seen so far: `t01a_e.sq` (title), `m01a_e.sq` (dungeon floor select), `e01a_a.sq` (Norune, port 0, music) with
`e01b_e.sq` (Norune, port 1, ambience), `e04a_a.sq` (Muska Racka) with `e04b_e.sq`. Port 0 carries the
town music and port 1 ambience: map only the ones you have a recording for. `SOUNDTRACK_TABLE.md` lists every
sequence and where it plays, and `soundtrack/matches.json` pairs 58 of them with soundtrack tracks.

## Where the recordings come from

The PS2 and the soundtrack release differ: the OST tracks are higher quality and often longer, so set
`loop_start` and `loop_end` for the part the game should repeat.

- Dark Cloud Original Soundtrack (Sony Computer Entertainment, 46 tracks, composed by Tomohito Nishiura): the
  2001 CD, and now digital on [Apple Music](https://music.apple.com/us/album/%E3%83%80%E3%83%BC%E3%82%AF%E3%82%AF%E3%83%A9%E3%82%A6%E3%83%89-%E3%82%AA%E3%83%AA%E3%82%B8%E3%83%8A%E3%83%AB-%E3%82%B5%E3%82%A6%E3%83%B3%E3%83%89%E3%83%88%E3%83%A9%E3%83%83%E3%82%AF/1082699658),
  [YouTube Music](https://music.youtube.com/playlist?list=OLAK5uy_mRH6a_TtG_WNRPon8FvMqOJ3MHr_SLUfY), Spotify and
  Amazon Music.
- A digital Dark Cloud series compilation of 123 tracks has been released: see
  [Deku Deals](https://www.dekudeals.com/items/dark-cloud-series-soundtrack).

Use a copy you have bought or ripped from your own disc. Nothing here downloads audio.


## Auditioning a sequence

`DC_BGM_TEST=<set>:<track>` (a test aid in `port/src/main.cpp`) stops the music at frame 150 of a run and plays that sound set's
track on the music port, so `DC_AUDIO_WAV` can record any sequence without playing to it:
`DC_BGM_TEST=104:1 DC_AUDIO_TRACE=1 DC_AUDIO_WAV=out.wav darkcloud --headless --jump menu --fast-load --frames 1800` (the developer menu is silent, so the recording holds only that sequence, starting about 3.1 s in).
Sets are the numbers in `SOUNDTRACK_TABLE.md`.

## How it works

`CSound::SQ_Play` (`port/src/sound.cpp`) asks `audio::SoundtrackFor` for the sequence's name
(`port/src/audio/soundtrack.cpp`); a recording becomes the port's stream
(`Mixer::SetStream`, `port/src/audio/mixer.cpp`). A stream stands in for the port's sequence:
`Play`, `Stop`, `Rewind`, `IsPlaying` and the volume the game sets for the port (fades included) act on it, and
`Mixer::RenderStreams` adds it to the mix before the mono fold. Decoding is `port/src/audio/pcm.cpp`: dr_wav and dr_flac
(public domain or MIT-0) and stb_vorbis (public domain), fetched at pinned commits in `port/CMakeLists.txt`.
