// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_SCREENSHOT_HPP
#define NBT_SCREENSHOT_HPP

namespace nbt {

// nbt-aad --screenshot: PNG of display 0 on stdout, one status line on stderr. Returns the exit code.
int runScreenshotCommand(int argc, char** argv);

}  // namespace nbt

#endif
