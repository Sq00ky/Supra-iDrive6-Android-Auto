/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
 *
 * Builds the vendored speexdsp resampler (third_party/speexdsp/resample.c, speexdsp 1.2.1, unmodified) with the
 * configuration nbt_speex.h declares: OUTSIDE_SPEEX, nbt_speex_ symbol prefix, fixed point. */
#define FIXED_POINT 1
#define EXPORT
#include "nbt_speex.h"
#include "../third_party/speexdsp/resample.c"
