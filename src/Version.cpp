#include "Version.h"

#ifndef TRACKRAT_VERSION
#define TRACKRAT_VERSION "dev"  // set from git by tools/build_web.py
#endif
#ifndef TRACKRAT_BUILT
#define TRACKRAT_BUILT __DATE__ " " __TIME__  // (tools/render_screens.py fixes it, for the same picture every time)
#endif

const char* firmwareVersion() { return TRACKRAT_VERSION; }
const char* firmwareBuilt() { return TRACKRAT_BUILT; }
