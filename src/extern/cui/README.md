<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="web/site/img/cui-mark-dark.svg">
  <img src="web/site/img/cui-mark-light.svg" width="64" height="64" alt="">
</picture>

# CUI

**Native UI components for C.**

About 170 components with spring motion and full keyboard support.<br>
Windows, macOS and Linux from one C99 codebase. No dependencies, about 250 KB.

[![CI](https://img.shields.io/github/actions/workflow/status/yappologistic/CUI/ci.yml?branch=master&label=CI&style=flat-square&labelColor=24292f)](https://github.com/yappologistic/CUI/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-57606a.svg?style=flat-square&labelColor=24292f&color=57606a)](LICENSE)
![C99](https://img.shields.io/badge/C-99-57606a.svg?style=flat-square&labelColor=24292f&color=57606a)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%C2%B7%20macOS%20%C2%B7%20Linux-57606a.svg?style=flat-square&labelColor=24292f&color=57606a)

[Getting started](docs/getting-started.md) ·
[Components](docs/components.md) ·
[Live docs](https://yappologistic.github.io/CUI/) ·
[Examples](examples) ·
[Design principles](docs/design.md)

<br>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/showcase/components-dark.png">
  <img src="docs/images/showcase/components.png" alt="CUI components: a sign up form, metric cards, a calendar with time slots, a bar chart, team members and settings">
</picture>

<sub>Every image on this page is drawn by CUI itself, from plain C.</sub>

</div>

> [!TIP]
> **Try it before you install it.** The [live docs](https://yappologistic.github.io/CUI/) run every component in your browser: the same C library, compiled to WebAssembly and drawn by a WebGL2 port of its renderer.

## Your interface is plain C

```c
#include <cui/cui.h>
#include <cui/cui_app.h>

int main(void) {
    cui_app *app = cui_app_create(&(cui_app_desc){
        .title = "Hello", .width = 420, .height = 300 });
    if (!app) return 1;
    bool notifications = true;
    int clicks = 0;

    while (cui_app_frame(app)) {
        cui_ctx *ui = cui_app_ui(app);
        cui_column(ui, .pad = 32, .gap = 16) {
            cui_heading(ui, "Hello, CUI", 2);
            cui_secondary(ui, "A native window, a few components "
                              "and no dependencies.");
            cui_switch(ui, "Notifications", &notifications);
            cui_row(ui, .gap = 8) {
                if (cui_button(ui, "Click me", .variant = CUI_BUTTON_PRIMARY))
                    clicks++;
                cui_textf(ui, "Clicked %d times", clicks);
            }
        }
    }
    cui_app_destroy(app);
    return 0;
}
```

<table>
<tr>
<td width="40%" valign="middle"><img src="docs/images/showcase/hello-dark.png" alt="The hello example: a heading, a line of text, a switch and a button"></td>
<td valign="middle">

**Components are functions.** They draw themselves, handle input and return what happened.

**Options are named arguments,** so the common case stays short and everything else is one `.field = value` away.

**Your variables are the state.** The screen is described again every frame, so there is nothing to keep in sync.

</td>
</tr>
</table>

## Why CUI

<table>
<tr>
<td width="50%" valign="top">

**A complete library**

Buttons, fields, pickers, dialogs and tables, plus a command palette, a virtualized data grid, kanban boards, gantt charts, a dozen kinds of chart, AI chat components, and tool panels such as docking, a node editor, a code editor, a terminal and a flame graph. They are designed together and look finished without styling.

</td>
<td width="50%" valign="top">

**Small and fast**

A full screen builds, lays out and paints in 0.1 to 0.25 ms. Idle apps use no CPU. A complete app is about 250 KB.

</td>
</tr>
<tr>
<td valign="top">

**Native everywhere**

Win32, Cocoa and X11, or SDL2 and GLFW, drawn with OpenGL or a software renderer. The core only emits triangles, so your own renderer works too.

</td>
<td valign="top">

**Careful by default**

Neutral surfaces, one accent, hairline borders, shadows only on things that float, and motion that explains what changed. Every component works from the keyboard and every animation has a reduced motion path. The [design principles](docs/design.md) list the rules.

</td>
</tr>
</table>

## Install

CUI uses CMake and builds with GCC, Clang, MSVC and MinGW. Add it to your project:

```cmake
include(FetchContent)
FetchContent_Declare(cui
    GIT_REPOSITORY https://github.com/yappologistic/CUI.git
    GIT_TAG master)
FetchContent_MakeAvailable(cui)
target_link_libraries(my_app PRIVATE cui::cui)
```

Or build it and run an example:

```sh
git clone https://github.com/yappologistic/CUI.git
cmake -S CUI -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/examples/tracker
```

On Linux you need the X11 development headers (`libx11-dev`). Windows and macOS need nothing beyond the SDK. [Getting started](docs/getting-started.md) covers other build systems and backends.

## Components

| Group | Components |
|---|---|
| **Actions** | button, icon button, button group, split button, toggle group, copy button, hold to confirm, segmented control, menus, command palette |
| **App chrome** | menu bar, toolbar, status bar, document tabs, docking layout with saved layouts |
| **Inputs** | text field, password, search, text area, number, money and scrub fields, tag input, one time code, select, combobox, multi-select, shortcut recorder |
| **Pickers** | date and date range, time, wheel and dial pickers, calendar, color picker, emoji picker, ruler and histogram sliders, knob and fader |
| **Selection** | checkbox, switch, radio group and cards, sliders, chips, rating |
| **Navigation** | tabs, sidebar, breadcrumb, pagination, stepper, table of contents, dock, tab bar |
| **Overlays** | dialog, sheet, popover, hover card, tooltip, toast stack, confirm popover, guided tour |
| **Feedback** | alert, banner, progress bar and ring, meter, spinner, skeleton, badge, avatar, empty state, timeline |
| **Data** | card, metric, table, virtualized data grid, tree table, path bar, list, tree, accordion, filter bar, file browser and open/save dialog |
| **Code** | code block, code editor, diff viewer, markdown view, JSON viewer |
| **Tool panels** | property inspector, log console, terminal, node editor, flame graph, hex viewer, curve and gradient editors, timeline editor, level meter |
| **Charts** | line, area, bar, donut, sparkline, gauge and radial gauge, candlestick, heatmap, treemap, radar, funnel, sankey, waffle, scatter and more |
| **Media** | image viewer with zoom and pan, image cropper, image compare, carousel |
| **Planning** | kanban board, week and month calendars, gantt chart with dependencies |
| **AI** | reasoning, tool calls, agent plan, sources and citations, context meter, prompt suggestions, model picker, chat composer |
| **Custom** | a canvas with a full 2D drawing API, per component state and spring animation helpers |

Every component is documented with a screenshot, usage, options and keyboard behaviour in [Components](docs/components.md) and [More components](docs/components-more.md).

<details>
<summary><b>The extended set</b></summary>
<br>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/showcase/components-pro-dark.png">
  <img src="docs/images/showcase/components-pro.png" alt="Extended CUI components: an AI answer with tools, a contribution heatmap, a sankey diagram, an agent plan, control tiles, a player and a radar chart">
</picture>

Capture (dropzone with real file drops, avatar upload, signature pad), conversation (chat bubbles, comments, presence, rolling counter, ticker, carousel, image compare, card stack), motion (shimmer text, typewriter, word reveal, spotlight cards, loaders, waveform, now playing) and more, all in [More components](docs/components-more.md).

</details>

## Showcase

<table>
<tr>
<td colspan="2"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/images/components/docking-dark.png"><img src="docs/images/components/docking.png" alt="A docked workspace with a scene tree, a viewport, a log console and a property inspector, with a tab being dragged to a new place"></picture></td>
</tr>
<tr>
<td colspan="2" align="center"><sub><b>Docking</b>: drag a tab to stack it or split any side; the layout saves as a short string · <a href="docs/components-more.md#docking">docs</a></sub></td>
</tr>
<tr>
<td colspan="2"><img src="docs/images/showcase/hero-data.png" alt="A data grid with a filter bar, two selected rows, team tags and score bars"></td>
</tr>
<tr>
<td colspan="2" align="center"><sub><b>Data grid</b>: 2,000 virtualized rows, filters, sorting, resizable columns and spreadsheet style selection</sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/images/showcase/hero-board.png" alt="A kanban board with a card being dragged between columns"></td>
<td width="50%"><img src="docs/images/showcase/hero-palette-dark.png" alt="A command palette with grouped commands and shortcuts"></td>
</tr>
<tr>
<td align="center"><sub><b>Kanban</b>: the lifted card follows the pointer and the column opens a slot for it</sub></td>
<td align="center"><sub><b>Command palette</b>: fuzzy search, groups and shortcuts</sub></td>
</tr>
<tr>
<td colspan="2"><img src="docs/images/showcase/hero-pick-dark.png" alt="A date range picker with presets and two months, and a wheel time picker"></td>
</tr>
<tr>
<td colspan="2" align="center"><sub><b>Pickers</b>: a date range with presets over two months, and wheels that spin with momentum</sub></td>
</tr>
</table>

<details>
<summary><b>More: charts, planning, conversation, effects</b></summary>
<br>

<table>
<tr>
<td width="50%"><img src="docs/images/showcase/hero-charts.png" alt="A contribution heatmap, a sankey of test runs, a radar chart and a task plan"></td>
<td width="50%"><img src="docs/images/showcase/hero-desk-dark.png" alt="Control tiles, a now playing card, a waveform and a dock"></td>
</tr>
<tr>
<td align="center"><sub><b>Charts</b>: calendar heatmap, sankey and radar, with a live task plan</sub></td>
<td align="center"><sub><b>Dock, tiles and player</b>: icons grow under the pointer, the waveform seeks on click</sub></td>
</tr>
<tr>
<td><img src="docs/images/showcase/hero-social-dark.png" alt="Chat bubbles, threaded comments, a rolling counter and presence avatars"></td>
<td><img src="docs/images/showcase/hero-effects-dark.png" alt="Three cards for choosing a renderer, one lit by the pointer"></td>
</tr>
<tr>
<td align="center"><sub><b>Conversation</b>: grouped bubbles, threads, a rolling counter, presence</sub></td>
<td align="center"><sub><b>Spotlight cards</b>: the surface and edge nearest the pointer light up</sub></td>
</tr>
<tr>
<td colspan="2"><img src="docs/images/showcase/hero-plan-dark.png" alt="A gantt chart with dependencies, progress and a today line"></td>
</tr>
<tr>
<td colspan="2" align="center"><sub><b>Gantt</b>: drag to reschedule, pull the ends to resize, arrows follow dependencies</sub></td>
</tr>
</table>

</details>

## Example apps

Complete apps in [`examples/`](examples), each a single C file.

<table>
<tr>
<td colspan="2"><img src="docs/images/showcase/assistant.png" alt="An AI assistant built with CUI"></td>
</tr>
<tr>
<td colspan="2" align="center"><sub><b>Muse</b>: reasoning, tool calls, sources, a plan and a composer with a model picker · <a href="examples/assistant.c">assistant.c</a></sub></td>
</tr>
<tr>
<td width="50%"><img src="docs/images/showcase/tracker.png" alt="An issue tracker built with CUI"></td>
<td width="50%"><img src="docs/images/showcase/dashboard-dark.png" alt="An analytics dashboard built with CUI"></td>
</tr>
<tr>
<td align="center"><sub><b>Orbit</b>: grouped issues, custom glyphs, an activity pane · <a href="examples/tracker.c">tracker.c</a></sub></td>
<td align="center"><sub><b>Larkspur</b>: figures, charts and a sortable table · <a href="examples/dashboard.c">dashboard.c</a></sub></td>
</tr>
</table>

<details>
<summary><b>All examples</b></summary>
<br>

| Example | What it shows |
|---|---|
| [`assistant`](examples/assistant.c) | AI assistant: reasoning, tool calls, sources, plan, composer with a model picker |
| [`tracker`](examples/tracker.c) | Issue tracker: grouped list, custom status glyphs, detail pane, activity |
| [`music`](examples/music.c) | Music player: cover art drawn on a canvas, track list, transport bar |
| [`finance`](examples/finance.c) | Personal finance: top navigation, charts, painted card, budgets, goals |
| [`dashboard`](examples/dashboard.c) | Sidebar, top bar, KPIs, charts and a sortable table |
| [`mail`](examples/mail.c) | Three pane inbox, folders, reading pane, compose dialog |
| [`notes`](examples/notes.c) | Split view, search, context menus, dialogs, shortcuts, file export |
| [`settings`](examples/settings.c) | Forms, validation, preferences and a danger zone |
| [`gallery`](examples/gallery_ui.c) | Every component, light and dark, all accents |
| [`hello`](examples/hello.c) | The smallest app |

<table>
<tr>
<td width="50%"><img src="docs/images/showcase/music.png" alt="Music player"></td>
<td width="50%"><img src="docs/images/showcase/finance.png" alt="Personal finance app"></td>
</tr>
<tr>
<td><img src="docs/images/showcase/mail-dark.png" alt="Mail client"></td>
<td><img src="docs/images/showcase/notes-dark.png" alt="Notes app"></td>
</tr>
</table>

</details>

## Light, dark and eight accents

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/showcase/accents-dark.png">
  <img src="docs/images/showcase/accents.png" alt="The same controls in all eight accents">
</picture>

```c
cui_set_theme(ui, CUI_THEME_DARK);      /* animates */
cui_set_accent(ui, CUI_ACCENT_BLUE);    /* neutral, violet, blue, green, amber, orange, coral, rose */
```

All colors are semantic tokens (`CUI_COLOR_SURFACE`, `CUI_COLOR_TEXT_SECONDARY`, `CUI_COLOR_ACCENT` and so on), so your own boxes follow the theme too. Run any app in the other theme with `CUI_THEME=dark` or `CUI_THEME=light`. See [Theming](docs/theming.md).

## Size and speed

| Measure | Result |
|---|---|
| `hello` on Linux, stripped, whole executable | about 250 KB |
| `assistant` (the AI app above), whole executable | about 330 KB |
| Whole library: every component, both renderers, X11 backend, fonts and icons | about 590 KB |
| Build, layout and paint a frame (gallery pages, 1240 × 820 points at 2×) | 0.12 to 0.25 ms |
| Software rasterization of the same frame (2480 × 1640 pixels, 4 threads) | 12 to 46 ms |
| Idle CPU | 0%, event driven |

The library is compiled for dead code stripping, so apps only carry what they use, and every heavy part (fonts, image decoding, the window layer) can be turned off ([Integration](docs/integration.md#binary-size)). Timings are wall clock on a 4 core cloud VM; `cui_bench` in [`tests/`](tests) reproduces them.

## Platforms

| Platform | Backend | Renderer | Tested in CI |
|---|---|---|---|
| Windows 7+ | Win32 (per monitor DPI, dark title bar) | OpenGL 3.3, GDI software fallback | MSVC, MinGW |
| macOS 10.13+ | Cocoa (Retina, native menus, text input) | OpenGL 3.3+, layer software fallback | Apple clang |
| Linux, BSD | X11 (XIM text input, HiDPI) | OpenGL 3.3 (GLX), XPutImage fallback | GCC, Clang |
| Wayland and others | SDL2 or GLFW | OpenGL 3.3 | GCC |
| Anything | Your own | Your own (one shader) | |

Set `CUI_RENDERER=software` to force the software renderer and `CUI_SCALE=2` to override the display scale. To use CUI inside an engine with your own window and renderer, see [Integration](docs/integration.md) and [Rendering](docs/rendering.md).

## Status and limitations

CUI 1.1 is complete enough to build real desktop apps. Every push runs unit, interaction and render tests, a randomized input fuzzer under AddressSanitizer and UndefinedBehaviorSanitizer, and real windows on Windows, macOS and Linux. What it does not do yet:

- **Screen readers.** There is no accessibility tree yet. Keyboard navigation and focus are complete.
- **Complex scripts.** Text is UTF-8 with kerning and font fallback. The optional `CUI_TEXT_SHAPING` CMake option adds Arabic-script shaping, Unicode bidi and shaped paragraph wrapping for static text (see [integration](docs/integration.md#optional-arabic-script-text-shaping)). Explicit embeddings, overrides and isolates are unsupported. It is off by default. Editing and other complex scripts remain outside this experimental path. CJK works once you add a fallback font.
- **Inline IME composition.** Committed text from input methods works; the text being composed shows in the system's IME window.
- **Native Wayland.** Use the SDL2 or GLFW backend, or XWayland.

## Documentation

| Guide | What it covers |
|---|---|
| [Getting started](docs/getting-started.md) | Install, build, the frame loop, your first screens |
| [Layout](docs/layout.md) | Rows, columns, sizes, alignment, grids, scrolling, split views |
| [Components](docs/components.md) and [more](docs/components-more.md) | Every component with screenshots, options and keyboard support |
| [Theming](docs/theming.md) | Tokens, light and dark, accents, fonts, motion |
| [Icons](docs/icons.md) | The built-in Lucide icons, and how to use any other icon font |
| [Custom components](docs/custom-components.md) | Canvas, drawing API, state and animation |
| [Integration](docs/integration.md) | Your own window, input and renderer |
| [Rendering](docs/rendering.md) | The vertex format and the reference shader |
| [Design principles](docs/design.md) | The rules every component follows |

**Live docs.** The same guide, with every component running in your browser, is at **https://yappologistic.github.io/CUI/**: the library compiled to WebAssembly and drawn with a WebGL2 port of its renderer. It is rebuilt from `master` on every push. To run it locally, use `web/tools/serve.sh` (needs clang with the `wasm32` target) and open http://localhost:8765. See [`web/`](web).

## Development

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure    # unit, interaction and render tests, the fuzzer
```

The documentation images in [`docs/images`](docs/images) are rendered by the tests (`tests/component_shots.c` for components and showcase scenes, `tests/screenshots.c` for the overviews), so a visual change and its documentation move together.

## Contributing

Bug reports and pull requests are welcome, from people and from AI agents. Every report needs an honest account of who wrote it (and which model, if an AI did) and evidence you produced yourself: the commands you ran and their output. [Contributing](docs/CONTRIBUTING.md) explains what that means, and the issue forms and pull request template ask for it.

## Credits

Type: [Inter](https://rsms.me/inter/) and [Geist](https://vercel.com/font) (SIL Open Font License). Icons: [Lucide](https://lucide.dev) (ISC). Font rasterization and PNG output: [stb](https://github.com/nothings/stb) (public domain). See [THIRD_PARTY.md](THIRD_PARTY.md). The live docs' overview uses Ansel Adams' *The Tetons and the Snake River* (1942, U.S. National Archives, public domain).

## License

MIT. See [LICENSE](LICENSE).
