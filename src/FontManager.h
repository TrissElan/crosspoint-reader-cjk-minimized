#pragma once

#include <GfxRenderer.h>

// Expose the global renderer for modules that need font operations.
GfxRenderer& getGlobalRenderer();
