// Stand-in for src/Splash.cpp (tools/build_sim.py): no start-up animation. The page shows the unit starting up
// on its own. Same functions (include/DisplayManager.h); drawBrandLogo is the real one's, with the picture.
#include "DisplayManager.h"
#include "BrandDraw.h"
#include "logo_image.h"

void DisplayManager::drawBrandLogo(int x, int y) { drawLogoPixels(gfx, LOGO_PIXELS, LOGO_W, LOGO_H, x - LOGO_W / 2, y); }
void DisplayManager::startSplash() {}
void DisplayManager::finishSplash() {}
bool DisplayManager::splashRunning() const { return false; }
