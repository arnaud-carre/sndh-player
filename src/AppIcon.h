#pragma once
#include <cui/cui.h>

// Caller releases the texture before destroying its CUI context.
cui_texture LoadAboutIcon(cui_ctx* ui);
