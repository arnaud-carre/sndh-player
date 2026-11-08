Vendored from https://github.com/yappologistic/CUI
Revision: 036fc12ec187f695d7cc02038ae7796ed3d4bb0d
License: MIT (see LICENSE and THIRD_PARTY.md).
The player uses the core C library with its own GLFW integration.
Local patch: cui_dialog_opts.center_title centers the modal title and balances
the space occupied by its standard close button. Menu items also accept a
tooltip anchored to the item itself, used for the WAV export filename.
The data grid calculates its visible row range after applying scroll input and
animation, using the same offset as rendering and the sticky header. This fixes
missing rows during fast wheel input, scrollbar dragging and cursor jumps.
Other upstream sources are unchanged.

Only build inputs and license notices are included; upstream demos, tests,
documentation images and web tooling are omitted.
