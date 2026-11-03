/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
 *
 * The speexdsp resampler (third_party/speexdsp, BSD-3-Clause, see its COPYING), embedded the way speexdsp supports:
 * OUTSIDE_SPEEX (no speexdsp build system or types header), every symbol renamed with the nbt_speex prefix so it can't
 * clash with another speex in the process, fixed point (int16 in and out, integer multiply-adds: no float work on
 * the softfp, non-NEON head unit). Include this header, never third_party/speexdsp/speex_resampler.h directly.
 * The code itself is compiled once, in speex.c. */
#ifndef AUDIO_SPEEX_H
#define AUDIO_SPEEX_H

#ifndef OUTSIDE_SPEEX
#define OUTSIDE_SPEEX 1
#endif
#ifndef RANDOM_PREFIX
#define RANDOM_PREFIX nbt_speex
#endif

#include "../../third_party/speexdsp/speex_resampler.h"

#endif /* AUDIO_SPEEX_H */
