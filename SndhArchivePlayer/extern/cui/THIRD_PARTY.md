# Third party software

CUI includes the following works. Their licenses apply to those parts.

| Component | Where | License |
|---|---|---|
| Inter (Rasmus Andersson) | `assets/fonts/Inter-*.ttf`, embedded in `src/cui_font_data.c` | SIL Open Font License 1.1, see `assets/fonts/LICENSE-Inter.txt` |
| Geist and Geist Mono (Vercel) | `assets/fonts/Geist*.ttf`, embedded | SIL Open Font License 1.1, see `assets/fonts/LICENSE-Geist.txt` |
| Lucide icons | `assets/fonts/Lucide-CUI.ttf`, embedded; `include/cui/cui_icons.h` | ISC, see `assets/fonts/LICENSE-Lucide.txt` |
| Unicode 17.0.0 data (Unicode, Inc.) | Generated `src/cui_bidi_data.h`, `src/cui_joining.h` and `src/cui_emoji_data.h` (emoji names and order from emoji-test.txt); generators in `tools/` | Unicode License V3, see `third_party/LICENSE-Unicode.txt` |
| stb_truetype, stb_image, stb_image_write, stb_rect_pack (Sean Barrett) | `third_party/` | Public domain (or MIT) |

The fonts are subsets of the originals (Latin, Latin-1, Latin Extended-A and common symbols). Under the OFL the subsets keep their original names only as embedded data; they are not distributed as separate font products.
