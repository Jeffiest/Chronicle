# Town sky pack fix

## Fix

- Removed index key `4ca6f09169f13055bbcec98914c426f0`, which pointed to `tex/60/608f54f5329a31edd68728c9cf7ff32b.png` (the foliage atlas). The saved town comparison in `prs/HIRES_PERF.md` identifies this runtime key as the 128×128 town cloud (`e02s01_0.png`). Without a replacement entry, runtime uses the original cloud.
- Updated `build_hirespack.py` so a grouped asset member key is indexed only when it equals the representative member key used to select the upscale. This prevents a representative picture from being assigned to another decoded picture in the same manifest group.
- Did not rebuild the pack. The texture PNGs are unchanged.

## Index scan

Compared 4216 source keys (from 4224 unique decoded source keys found in the manifest) against their indexed upscales. Downscaled each upscale with Pillow BOX and used the `check_upscaled.py` visible-pixel RGB MAD and full alpha MAD; flagged RGB MAD ≥ 12 or alpha MAD ≥ 12. Found 61 threshold mismatches. Entries are listed below; `MAD` is visible RGB mean absolute difference and `A-MAD` is full alpha mean absolute difference.

| Key | MAD | A-MAD | Manifest member |
|---|---:|---:|---|
| `e35f17aa0908196f1913432417ee7710` | 12.26 | 1.21 | `commenu__a1.pak/a1~3.png` (A00043) |
| `44a454bc528b4f704f7d085aa853c894` | 14.13 | 2.35 | `commenu__a2.pak/a2~2.png` (A00050) |
| `229f8e85feb3a22af0f1293c3137e086` | 12.15 | 1.27 | `commenu__a2.pak/a2~3.png` (A00051) |
| `d01f641e1334188cc3362db0dba85209` | 12.58 | 1.37 | `commenu__a3.pak/a3~1.png` (A00053) |
| `d45ad2e7929df423554bc6f7cfde476c` | 13.06 | 1.37 | `commenu__a3.pak/a3~2.png` (A00054) |
| `e64f31c485799dcc209b93ad67efe80c` | 12.11 | 0.42 | `commenu__a5.pak/a5~2.png` (A00065) |
| `8d6acdfd33787be3ce90566e86ca2668` | 12.67 | 1.27 | `commenu__a5.pak/a5~3.png` (A00066) |
| `3a7d627d3255c9c83457d6b34114038e` | 16.29 | 4.13 | `commenu__a_usa__itemlst.img/itemlst.png` (A00099) |
| `de373ce0e3d5ddcaf736631264bfa764` | 16.33 | 4.13 | `commenu__a_eng__dunenter__dunenter0.pak/dunenter0~3.png` (A00101) |
| `8a31d7215b31a9b922396388f3d4d9bc` | 0.93 | 17.76 | `commenu__a_eng__kgegoro2.img/kgegoro2.png` (A00139) |
| `8f95763b0aab95c0d0f7b58a0471cfa1` | 4.1 | 21.64 | `commenu__a_eng__kgeozu2.img/kgeozu2.png` (A00140) |
| `ce03039bfe9e77dca8a61240514e39d5` | 0.68 | 14.39 | `commenu__a_eng__kgeruby2.img/kgeruby2.png` (A00141) |
| `54328c27a44234bb53206337f8d75f68` | 16.31 | 4.13 | `commenu__a_fre__dunenter__dunenter0.pak/dunenter0~3.png` (A00255) |
| `91babd2bec27af66d66a446cac1e0ceb` | 16.3 | 4.13 | `commenu__a_ger__dunenter__dunenter0.pak/dunenter0~3.png` (A00339) |
| `1965e2478532c259ac7a00544fd35eca` | 16.31 | 4.13 | `commenu__a_ita__dunenter__dunenter0.pak/dunenter0~3.png` (A00425) |
| `de416e827507db0a00d4979889adfe4c` | 15.38 | 3.95 | `commenu__a_jpn__kgeozu2.img/kgeozu2~1.png` (A00502) |
| `52e5129e3d59b25dd130d439ec66c983` | 15.71 | 3.99 | `commenu__a_jpn__itemlst.img/itemlst.png` (A00506) |
| `4cb373dcedddc0065e6715669a06b507` | 14.1 | 2.78 | `commenu__a_jpn__dunenter__dunenter0.pak/dunenter0~3.png` (A00509) |
| `b88ce3416c5092d5da8af5cc398b6f71` | 15.79 | 3.08 | `commenu__kgeozu2.img/kgeozu2~1.png` (A00511) |
| `10071419f11059e0d510ecbff389329e` | 15.5 | 3.95 | `commenu__a_jpn__fishmenu.pac/fishmenu~2.png` (A00515) |
| `d7fe5b8af75b28566175995b62e412a2` | 15.69 | 3.99 | `commenu__a_jpn__quickchr.pac/quickchr~1.png` (A00586) |
| `3e77a05a8ad55bafb43780d1b34e5e49` | 14.7 | 3.0 | `commenu__allicon.img/allicon.png` (A00711) |
| `4683db933d215d01d5aaeb372c07c870` | 14.32 | 3.02 | `commenu__charatex2.img/charatex2~1.png` (A00825) |
| `484983eecd73457b1a45ba95468c1725` | 19.69 | 3.28 | `commenu__charatex2.img/charatex2~2.png` (A00826) |
| `2d2c7f311dc17a0ac4819a9eb64698f0` | 16.64 | 2.6 | `commenu__charatex2.img/charatex2~3.png` (A00827) |
| `ef91ee48932b4f6976b1c63cce5c625a` | 19.94 | 5.03 | `img__itemshop.pak/itemshop~2.png` (A00838) |
| `3cfa4b8cd177a147c34ddf3082a32cee` | 12.06 | 3.38 | `img__eventmnu.pak/eventmnu~2.png` (A00840) |
| `fd34b6ce8433c767701fbe5947d66305` | 12.02 | 3.37 | `commenu__dungeon___dunmenu.pak/_dunmenu~6.png` (A00847) |
| `49622427351a5c78063f106246fb22af` | 19.92 | 5.03 | `commenu__dungeon___dunmenu.pak/_dunmenu~8.png` (A00849) |
| `ed13f0d4921a946fc0a7fdbc99d929bb` | 12.05 | 3.38 | `commenu__dungeon__dunmenu.pak/dunmenu~6.png` (A00877) |
| `cea266fde43bc3a7f3a16737b87d5d8a` | 19.94 | 5.03 | `commenu__dungeon__dunmenu.pak/dunmenu~8.png` (A00879) |
| `94a4d4cfb829dd08784cd157c195e96b` | 12.61 | 1.27 | `commenu__moon.img/moon~3.png` (A00901) |
| `2ef17047a08c42ced1613db5e9760095` | 19.95 | 5.03 | `commenu__quickchr.img/quickchr~2.png` (A00912) |
| `6d8e5f74502c753e7f37670f805338f9` | 29.78 | 47.84 | `dun__pack__teximg.pac_/teximg~8.png` (A00967) |
| `6c6d7a46ce2eb50ad73d67a89a9b70b8` | 125.69 | 149.87 | `dun__pack__teximg.pac_/teximg~9.png` (A00968) |
| `9849dc81eef47eaff7bf41d7923710d0` | 12.13 | 0.31 | `dun__d01__event__pic.chr/pic.png` (A00982) |
| `9fa440cb9cb213dd34ebe55fec7e7b24` | 60.52 | 0.0 | `gedit__s42__img.pak/img~20.png` (A01572) |
| `54c947b2bd5f4edfceb65481cf89580d` | 44.69 | 0.0 | `gedit__s80__img.pak/img~12.png` (A01573) |
| `1b727a8356ede723218d78651e619054` | 48.15 | 0.0 | `gedit__s13__img.pak/img~2.png` (A01683) |
| `93b0ac75b5be75b7b72d412065d8ac5b` | 42.96 | 0.0 | `gedit__s38__img.pak/img~11.png` (A01683) |
| `53c904d07cdadf5017a709c8f743941a` | 0.0 | 254.82 | `gedit__s41__img.pak/img~44.png` (A01708) |
| `2d7c4db2d11f0ff51cffe1a699ac4455` | 45.21 | 0.0 | `gedit__s91__img.pak/img~6.png` (A01709) |
| `d800712a77aabc1960e51d73c6d3bfd5` | 45.1 | 0.0 | `dun__mpd_pack__d05inter.mpd/d05inter~6.png` (A01709) |
| `8a679bbf84c7de4f53942c84a523859e` | 12.44 | 1.26 | `dun__pack__teximg.pac_/gaiji.png` (A01910) |
| `9787533f7ff3f9ca73f79eeb5a0e64c7` | 21.18 | 1.05 | `dun__pack__teximg_e.pac/teximg_e~10.png` (A01933) |
| `8fe2e3d20b264581e574ae5a545c0adf` | 14.63 | 2.98 | `dun__pack__teximg_e.pac/teximg_e~12.png` (A01935) |
| `10b4cddc56130f4499030d0973d2fd2b` | 17.07 | 1.78 | `dun__pack__teximg_e.pac/teximg_e~14.png` (A01937) |
| `d1172498adcea794b9c08304e721f9f3` | 118.74 | 254.82 | `gedit__e02__img.pak/img~29.png` (A02123) |
| `b96e689b6e52605a6894aaf300a6830e` | 166.88 | 254.82 | `gedit__e03__img.pak/img~19.png` (A02123) |
| `ad061fb5753cd7309819b0c4d02fb50a` | 109.03 | 242.44 | `gedit__e02__img.pak/img~27.png` (A02126) |
| `19cc9aa7936960f23bc21e727a89caa2` | 88.08 | 242.43 | `gedit__s32__img.pak/img~11.png` (A02126) |
| `587aa68104a012c5662ca8aecaf3d019` | 83.33 | 207.72 | `gedit__s47__img.pak/img~11.png` (A02126) |
| `e3662d2ab6eedf8cd19064afd1d4225b` | 81.05 | 242.44 | `gedit__s17__1__img.pak/img~10.png` (A02126) |
| `672132ab5863d978fa11aeca83244f46` | 42.75 | 133.77 | `gedit__s01__img.pak/img~12.png` (A02415) |
| `6fb5426d1a772813c5b7a9f20f0f76c7` | 56.31 | 156.98 | `gedit__s47__img.pak/img~10.png` (A02434) |
| `c6b19bbc6cc9cb92b2f634098edf1aa6` | 63.42 | 156.97 | `rmdat__rmdat7.pak/rmdat7~1.png` (A02434) |
| `d6d60ec67767d3fecd83d1eed7166a66` | 48.58 | 18.43 | `gedit__e03__img.pak/img~25.png` (A02726) |
| `d98c6a9fc1381191d1c77f3c3ea9837f` | 67.0 | 254.21 | `gedit__s32__img.pak/img~14.png` (A03000) |
| `dd36ad545a6c18d162a26df39362ecf0` | 0.05 | 254.76 | `gedit__s27__img.pak/img~16.png` (A03175) |
| `af6106edbe098616f034834a8b1b6d39` | 83.62 | 0.0 | `gedit__s04__img.pak/img~5.png` (A03330) |
| `042f2ab393f8671a61a143e025cff7a0` | 12.96 | 2.86 | `gedit__system__esys.pak/whatsday.png` (A04461) |

The scan found 12 index keys with no matching decoded key in the current manifest source set. The fixed cloud key was one of them. Other orphan keys (not otherwise assessed for image difference):

- `05f3d5e962fa0a432b5d39c227129879`
- `88e800cf15204e4928e237dc4ad1bbc7`
- `977e9b0285d4f359c6e84e24b9d2e241`
- `987e3dea0eaa6ffef42c1463b6cc1fd8`
- `a8085c0b3bb47e2517ba98fdd6b9fd45`
- `ba0d00e8a0fb5c2ec714fb6d7292156e`
- `d714db0b23b1287638ccbd8169fcc356`
- `de6eb05c477ea694a98b83a551bb37fa`
- `e33cfa7a7db323147827cc18f4ca7576`
- `fa6918a1ab57609d813c653bf83bf1ae`
- `fe1be9981997903886be389ba808c421`

The mismatch scan was diagnostic only; these other entries were not changed. The result covers keys reproducible from the current manifest source files and does not claim coverage for the 12 orphan index entries.
