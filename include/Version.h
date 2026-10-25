/**
 * Track Rat - the firmware's version, for Settings and the Wi-Fi page
 *
 * The version is `git describe --always --dirty` at build time (a commit like
 * "b65364c", "-dirty" with uncommitted changes), stamped into Version.cpp by
 * tools/build_web.py; "dev" when built without git.
 */

#ifndef VERSION_H
#define VERSION_H

const char* firmwareVersion();  // "b65364c"
const char* firmwareBuilt();    // "Oct 24 2026 17:03:12", the compiler's date and time

#endif  // VERSION_H
