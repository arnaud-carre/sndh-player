# Changelog

What changed in CUI, newest first. Every component mentioned here runs live in the docs.

## Unreleased · phones and tablets

### Added

- `cui_state_on_free(ui, id, fn)`: a function that frees what a piece of state holds (textures, buffers) when the state is dropped or the context is destroyed.
- `cui_request_wakeup(ui, seconds)`: a frame within that many seconds with no input, for content that changes on a timer (a clock, polling, messages arriving). An app that used to redraw continuously for that can now sleep in between.
- **Soak test** (`tests/soak.c`): runs an app the way a person would, clicking what it drew, typing, scrolling, dragging, resizing and leaving it idle, for as long as asked. It records library memory, allocations per frame, frame times, textures and process memory, and fails if anything is left allocated after `cui_destroy`. It runs headless at many times real speed, or in a real window driven by real input events.
- **Emoji picker and color emoji**: the 1,914 emoji of Unicode 17 with names, groups, search, recent emoji, skin tones and a preview. It has keys and touch scrolling. Emoji come from the system's color emoji font (sbix, CBDT or COLR version 0), read from the file as needed, with sequences found through the font's ligatures. `cui_emoji` and `cui_draw_emoji` draw an emoji anywhere. The built-in list adds about 16 KB.
- **Terminal**: output over an input line. It reads ANSI colors (16, 256 and 24-bit), bold, dim, underline and inverse, `\r` progress bars, tabs and clear sequences, and drops the escapes it does not draw. Lines wrap at the panel width, only the rows in view are drawn, and it follows new output. It has drag-to-copy without the colors, command history, Ctrl+C to interrupt and Ctrl+L to clear. The app owns a fixed ring buffer, so writing never allocates.
- **File browser and file picker**: a folder view with sorting, natural name order, a filter, back, forward and up, a typeable path bar, places, type-ahead and keys. The open, save and folder dialog is drawn by CUI. Listing goes through a callback (the local disk by default), and big folders are read once and built only where they show.
- **Image viewer and cropper**: zoom around the pointer, pan, double-click for actual pixels, keys, eased motion and a zoom pill. The cropper has corner and edge handles, aspect lock, a round mask for avatars, thirds lines and the size while dragging.
- **Guided tour**: a spotlight that glides from control to control with a card saying what each one does. It has Back, Next and Done, progress dots and keys, and can show a centered card for steps without a target. The spotlighted control can still be tried, and the button text can be translated.
- **Month calendar**: a month grid with events. Multi-day and all-day events are bars that share lanes across a week; timed events show in time order with a dot and the time, and a full day says "+N more" and opens a list. It supports keyboard navigation across months, double-click to add, localized month and day names, any first weekday, and a phone layout with colored dots and an agenda of the selected day.
- **Radial gauge**: a value on a dial with colored bands, labelled ticks, a target marker and a needle that springs to new values, or a filled arc without the needle.
- **Candlestick chart**: open, high, low and close with volume, overlay lines and a last price tag. It zooms around the pointer, pans by dragging, follows new candles, and shows a crosshair with each candle's values. Zoomed out, candles merge per pixel column, so 100,000 of them build in about a millisecond.
- **Knob, fader and level meter**: the controls of audio, synth and mixer panels. Knobs turn by dragging up and down with a fine mode on Shift, can be bipolar or logarithmic, and reset on double-click. Faders have a labelled scale and an optional meter beside the track. Level meters have peak hold, a falling bar, a clip light and LED segments. The wheel only turns a control that has focus.

### Changed

- **No GPU, no OpenGL.** When the OpenGL driver draws on the CPU (Mesa llvmpipe, SwiftShader and the like, common in virtual machines and remote sessions), X11 and Win32 apps on the Auto renderer use CUI's software renderer instead, which redraws only what changed. Notes under llvmpipe went from 27% of a core to 4%. `cui_gl_is_software` tells, and `CUI_RENDERER=opengl` keeps OpenGL.
- **Full software repaints are about twice as fast.** An opaque solid, or the inside of an opaque rounded rectangle, is filled as one run of a color instead of pixel by pixel, which was most of the work on a typical screen of panels and cards. The output is identical, byte for byte, on every screenshot.
- **Per-frame buffers shrink again.** Vertices, indices, draw commands, nodes and the frame arena shrink once they have been more than four times bigger than needed for 600 frames, so a heavy page no longer leaves its buffers behind.
- **Long lists keep less state.** Scrolling through thousands of rows used to leave three state entries per row for five seconds, and the table never shrank: paging through a 5,000 row grid held 15,000 entries. Now an entry that holds only last frame's rectangle expires after 30 frames, hover fades take state only once a row is hovered, buttons and links keep none until touched, and the table packs itself when mostly empty. The same paging holds about 1,700 live entries, and 150 at rest.
- **Emoji cost less.** The emoji texture is released once no emoji has been drawn for 600 frames, which frees 1 to 4 MB after the picker closes. Small emoji use a quarter of the texture. Bitmap emoji are scaled with precomputed weights, which makes drawing new ones about three times faster, and at most 12 are drawn per frame, so scrolling the picker stays near 2 ms a frame.
- **The software renderer draws only what changed.** `cui_sw_set_incremental` keeps the last frame in the buffer and repaints only the 64 pixel tiles whose triangles changed, so a hover or a caret blink costs a few tiles rather than the window. New glyphs and emoji arriving in a texture redraw only what samples them. A frame where nothing changed draws nothing, and `cui_sw_last_damage` tells the backend which part to copy to the screen. The app's software path and the browser build use it: with the same input, the Mail example used 3.9% of a core instead of 12.9%. Output is identical to a full repaint (tested frame by frame).
- **The glyph atlas shrinks again.** It still grows when it fills up, but glyphs not drawn for five seconds (sizes from before a zoom or a change of display scale) are dropped, and the rest move into the smallest atlas they fit. A long session no longer keeps a 2048 or 4096 pixel atlas it no longer needs.
- Truncated right-to-left text puts its ellipsis on the left, where the text ends (with text shaping).
- Text field placeholders are shaped like other labels, so Arabic and other joined scripts read correctly in them (with text shaping). The text being edited stays unshaped, as before.
- **Every component page fits a phone.** Layouts that used to need more than a phone's width now rearrange instead of shrinking the preview. The calendar, countdown, radar chart, activity rings and donut legend scale down. The waffle legend moves under its grid, and the banner puts its action under the message. The player drops its previous button, and the stepper keeps only the current label. Pagination collapses to the first, current and last pages, and the status bar keeps icons and drops labels. Toolbars, path bars and inspectors fold, and the dock shrinks its icons and stops magnifying.
- **Docking** shows every panel as a tab of one stack when the dockspace is too narrow for panes side by side. The saved layout is untouched, and the panes come back when there is room.
- **Segmented controls and button groups** tighten their padding before a label truncates.
- **Sankey, funnel and heatmaps** keep their labels readable on narrow charts. Sankey labels stack over their values, funnel step rates shorten, heatmaps label every other row, and the calendar heatmap drops its Less and More words.
- **Gantt charts** use a slimmer task list on phones, so the timeline keeps most of the width. Tables fade at the edge that has more columns.
- Prompt suggestions and radio cards drop to fewer columns before their text gets cramped. The command palette's key hints wrap to a second line.
- Signatures scale down with a narrower pad instead of being cut off.
- The docs site lays out each preview at the width it has before measuring it, so components that adapt are shown at full size.

### Fixed

- The gallery's image compare page made new textures each time it was visited and never freed the old ones, about 2 MB per visit. Found by the soak test.
- Joined wheel pickers in the demo sit flush, so the selection band runs across all three.
- A demo sankey read past the end of its link array.

## 2026-11-07 · 1.1: text, code and workspace

### Added

- **Docking**: drag panels by their tabs to stack them or split any side, with drop zones and a preview while dragging, resizable dividers, closing and reopening in place, and a layout that saves and loads as a short readable string. It builds on the existing document tabs and split view.
- **Tree table and path bar**: an expandable tree with columns on top of the data grid, for file explorers, scene graphs and debugger variables. It handles large trees, loads folders lazily, and follows the usual tree keys. The path bar collapses long paths, offers sibling menus and switches to a text field for typing a path.
- **Timeline editor**: grouped tracks with keyframes, a time ruler with in and out markers, a scrubbable playhead, clips, box selection, snapping to frames and zoom around the pointer. Its keys are curve editor keys, so the selected track's curve can be edited right beside it.
- **Markdown view**: headings, emphasis, links, nested and task lists, quotes, tables and fenced code with the existing highlighter. It renders partial text sensibly while an answer streams in, reports link clicks and task toggles instead of acting on them, and AI chat messages can render their body as markdown.
- **Code editor**: an editable code block with syntax highlighting, a line number gutter, selection, undo and redo, auto-indent, bracket matching, find (Ctrl+F) and go to line (Ctrl+G). Only the lines in view are laid out, so long files stay fast.
- **Diff viewer**: split and inline modes, word-level marks inside changed lines, folded unchanged stretches, next and previous change, and syntax colors. The diff runs only when a text changes, and `cui_diff_compute` gives the same diff without any UI.

### Changed

- **Component pages** in the docs are now organized into Usage, Behavior, Keyboard, Options and Related, with an "On this page" index. The options table is generated from `cui.h`, keys are listed on their own, related components are linked, screenshots sit behind a toggle, source links open the exact lines on GitHub, and every live preview can switch between full, tablet and phone widths.
- **The components index** has a filter box, category chips, "New" labels and a compact list view.
- A focused editor can keep the Tab key for indenting; Esc gives Tab back to focus movement.
- **Data grid**: columns can declare a priority and hide on narrow widths, rows can select without a checkbox column, a cursor set from code scrolls into view, and truncated cells show their full text on hover.
- **Changelog**: this page, also on the docs site.
- The README lists every component group, including the new app chrome, code and tool panels, and leads its showcase with docking.
- Version 1.1.0, with about 160 components.

### Fixed

- The data grid's sticky header no longer disappears once the rows scroll past it.
- A scroll area squeezed to zero width no longer produces invalid values in its edge fade.

## 2026-11-06 · Tools and a new landing page

### Added

- **App chrome**: a menu bar with full keyboard access, a toolbar that collapses into a "more" menu, a status bar, and editor-style document tabs that close, reorder and scroll.
- **Tool panels**: a property inspector with typed editors in folding groups, and a log console over a caller-owned ring buffer with level filters, search and follow.
- **Node editor**: a pannable, zoomable canvas of nodes, typed pins and curved links, with box selection, a minimap and ordinary components inside nodes.
- **Flame graph**: a profiler timeline that stays fast with 100,000 spans, with zoom around the pointer, hover timings and zoom to a span.
- **Hex viewer**: synced offset, hex and ASCII columns, virtualized for large files, with highlighted ranges, copy and optional editing.
- **Curve and gradient editors**: keyframes with bezier handles and a color ramp with stops. `cui_curve_eval` and `cui_gradient_eval` sample exactly what the editors draw.
- **Arabic-script shaping and bidirectional text**, optional, contributed in pull request #1 by omid3098.

### Changed

- **A new landing page** for the live docs: a lighter hero ending in the C mark, a tabbed live showcase, a bento of live components, a theme playground, guide cards and motion throughout, all off with reduced motion.
- The phone navigation fades the edge it scrolls past instead of cutting a word in half.
- A truncated label no longer shows its tooltip through a menu, popover or dialog open above it.
- The smallest complete app now measures about 250 KB stripped.

## 2026-11-05 · A calmer design and the live docs

### Changed

- **A more timeless look**: one concentric radius scale, quieter edges, flat solid controls, and shared motion, state, radius, edge and icon tokens.
- **Responsive layout**: rows wrap, grids pick their column count, and components adapt to narrow widths.
- Keyboard focus behaves after a click, modifier keys release correctly, and avatar stacks are tidier.

### Added

- **Live docs** on GitHub Pages: every component running in the browser, compiled to WebAssembly, with its C source and docs beside it. The site has a raster C mark and favicon.
- A shorter, code-first README.
- A contributing guide with disclosure and evidence rules for people and AI agents, and issue and pull request forms.

## 2026-11-05 · 1.0

The first public version.

### Added

- **The core**: layout, draw list, text with kerning and font fallback, themes with light and dark and eight accents, an OpenGL renderer and a software renderer.
- **Backends** for Win32, Cocoa, X11, SDL2 and GLFW, with native file dialogs, URL opening and OS file drag and drop.
- **Components** for actions, inputs, pickers, selection, navigation, overlays, feedback, data display, a virtualized data grid, a command palette, kanban, week calendar and gantt charts, a dozen kinds of chart, conversation and media components, text effects and AI interface components.
- **Example apps**: gallery, dashboard, settings, notes, mail, tracker, music, finance and an AI assistant, each one C file.
- **Tests and CI** on Linux, Windows and macOS: unit, interaction and render tests, a randomized input fuzzer, and AddressSanitizer and UndefinedBehaviorSanitizer builds.
- **Documentation** with screenshots rendered by the tests, so visual changes and their docs move together.

### Changed

- Contrast meets WCAG AA, wrapped text avoids lone last words, and tabular figures line up.
- The library stays small: compressed fonts and dead code stripping, so apps carry only what they use.
