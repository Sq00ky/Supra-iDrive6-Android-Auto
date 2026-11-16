#pragma once

#include "wels/codec_api.h"
#include "cluster/profiles/video.hpp"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

namespace aa_cluster {

// One decoder instance exclusively for display_id=2; the dashboard decoder is
// never touched. Only complete Annex-B access units enter this interface.
class Decoder {
    enum {
        SourceWidth = nbt::cluster::VideoProfile::SourceWidth,
        SourceHeight = nbt::cluster::VideoProfile::SourceHeight,
        ChromaWidth = SourceWidth / 2,
        ChromaHeight = SourceHeight / 2,
        ContentWidth = nbt::cluster::VideoProfile::ContentWidth,
        ContentHeight = nbt::cluster::VideoProfile::ContentHeight,
        FittedWidth = nbt::cluster::VideoProfile::FittedWidth,
        HorizontalPadding = nbt::cluster::VideoProfile::PaddingX
    };

    ISVCDecoder* decoder_;
    uint8_t* previous_;
    bool have_previous_;
    unsigned unchanged_, coalesced_;

    static uint8_t clamp(int value) {
        return value < 0 ? 0 : value > 255 ? 255 : uint8_t(value);
    }

    bool matches_previous(uint8_t* const planes[3], const SSysMEMBuffer& buffer) const {
        bool same = have_previous_;
        unsigned offset = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned width = channel ? ChromaWidth : SourceWidth;
            const unsigned height = channel ? ChromaHeight : SourceHeight;
            const unsigned stride = buffer.iStride[channel ? 1 : 0];
            for (unsigned row = 0; row < height; ++row) {
                if (same && memcmp(previous_ + offset, planes[channel] + row * stride, width)) {
                    same = false;
                }
                offset += width;
            }
        }
        return same;
    }

    void remember_picture(uint8_t* const planes[3], const SSysMEMBuffer& buffer) {
        unsigned offset = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const unsigned width = channel ? ChromaWidth : SourceWidth;
            const unsigned height = channel ? ChromaHeight : SourceHeight;
            const unsigned stride = buffer.iStride[channel ? 1 : 0];
            for (unsigned row = 0; row < height; ++row) {
                memcpy(previous_ + offset, planes[channel] + row * stride, width);
                offset += width;
            }
        }
        have_previous_ = true;
    }

    static void convert_picture(uint8_t* const planes[3], const SSysMEMBuffer& buffer,
                                uint8_t* bgra) {
        // Fit the negotiated source into the profile's content rectangle. The
        // profile specifies the fitted width and padding. This buffer contains
        // content only; ScreenOutput supplies the separate row-0 metadata.
        for (unsigned y = 0; y < ContentHeight; ++y) {
            for (unsigned x = 0; x < ContentWidth; ++x) {
                uint8_t* pixel = bgra + (y * ContentWidth + x) * 4;
                pixel[0] = pixel[1] = pixel[2] = 0;
                pixel[3] = 255;
                if (x < HorizontalPadding || x >= HorizontalPadding + FittedWidth) {
                    continue;
                }
                // Fixed-point bilinear sampling before YUV->RGB, including chroma.
                const unsigned source_x =
                    (x - HorizontalPadding) * (SourceWidth - 1u) * 256 / (FittedWidth - 1);
                const unsigned source_y = y * (SourceHeight - 1u) * 256 / (ContentHeight - 1);
                const int luma = sample(planes[0], buffer.iStride[0], SourceWidth, SourceHeight,
                                        source_x, source_y) -
                                 16;
                const int u = sample(planes[1], buffer.iStride[1], ChromaWidth, ChromaHeight,
                                     source_x / 2, source_y / 2) -
                              128;
                const int v = sample(planes[2], buffer.iStride[1], ChromaWidth, ChromaHeight,
                                     source_x / 2, source_y / 2) -
                              128;
                // Limited-range BT.601, as negotiated SD H.264. BGRA memory is the
                // car-tested byte order despite Screen calling the format RGBA8888.
                pixel[0] = clamp((298 * luma + 516 * u + 128) >> 8);
                pixel[1] = clamp((298 * luma - 100 * u - 208 * v + 128) >> 8);
                pixel[2] = clamp((298 * luma + 409 * v + 128) >> 8);
            }
        }
    }

  public:
    Decoder() : decoder_(0), previous_(0), have_previous_(false), unchanged_(0), coalesced_(0) {}
    ~Decoder() {
        close();
    }

    void close() {
        if (decoder_) {
            decoder_->Uninitialize();
            WelsDestroyDecoder(decoder_);
            decoder_ = 0;
        }
        free(previous_);
        previous_ = 0;
        have_previous_ = false;
    }

    unsigned unchanged() const {
        return unchanged_;
    }
    unsigned coalesced() const {
        return coalesced_;
    }

    bool open() {
        close();
        if (WelsCreateDecoder(&decoder_) || !decoder_) {
            return false;
        }
        // The QNX port deliberately has no codec worker primitives. Decoding
        // stays on the player thread, with error concealment disabled.
        int threads = 0;
        if (decoder_->SetOption(DECODER_OPTION_NUM_OF_THREADS, &threads)) {
            close();
            return false;
        }
        SDecodingParam parameters = {};
        parameters.eEcActiveIdc = ERROR_CON_DISABLE;
        parameters.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
        if (decoder_->Initialize(&parameters)) {
            close();
            return false;
        }
        previous_ = (uint8_t*)malloc(SourceWidth * SourceHeight * 3 / 2);
        if (!previous_) {
            close();
            return false;
        }
        return true;
    }

    // Returns 1 for a changed picture, 0 for config/no change, -1 on error.
    int decode(const uint8_t* bytes, unsigned size, uint64_t pts, uint8_t* bgra,
               bool present = true) {
        if (!decoder_ || !size || size > 512 * 1024) {
            return -1;
        }
        uint8_t* planes[3] = {};
        SBufferInfo info = {};
        info.uiInBsTimeStamp = pts;
        const DECODING_STATE status = decoder_->DecodeFrameNoDelay(bytes, int(size), planes, &info);
        if (status != dsErrorFree) {
            return -1;
        }
        if (!info.iBufferStatus) {
            return 0;
        }
        const SSysMEMBuffer& buffer = info.UsrData.sSystemBuffer;
        // Refuse dimensions outside the profile's negotiated stream before
        // indexing its planes; renegotiation requires a new tested profile.
        if (buffer.iWidth != SourceWidth || buffer.iHeight != SourceHeight ||
            buffer.iStride[0] < SourceWidth || buffer.iStride[1] < ChromaWidth || !planes[0] ||
            !planes[1] || !planes[2]) {
            return -1;
        }
        // Preserve every predictive reference, but let a slow display catch up.
        // The comparison cache remains the last picture selected for display,
        // not a picture skipped while draining the transport backlog.
        if (!present) {
            ++coalesced_;
            return 0;
        }
        // AA can encode identical pictures at 30 Hz. Decode every access unit
        // to preserve references, but avoid Screen RPCs and colour conversion
        // when all three visible YUV planes are byte-identical.
        if (matches_previous(planes, buffer)) {
            ++unchanged_;
            return 0;
        }
        remember_picture(planes, buffer);
        convert_picture(planes, buffer, bgra);
        return 1;
    }

    static unsigned sample(const uint8_t* plane, int stride, unsigned width, unsigned height,
                           unsigned x, unsigned y) {
        const unsigned left = x >> 8;
        const unsigned top = y >> 8;
        const unsigned fraction_x = x & 255;
        const unsigned fraction_y = y & 255;
        const unsigned right = left + 1 < width ? left + 1 : left;
        const unsigned bottom = top + 1 < height ? top + 1 : top;
        const unsigned top_sample = plane[top * stride + left] * (256 - fraction_x) +
                                    plane[top * stride + right] * fraction_x;
        const unsigned bottom_sample = plane[bottom * stride + left] * (256 - fraction_x) +
                                       plane[bottom * stride + right] * fraction_x;
        return (top_sample * (256 - fraction_y) + bottom_sample * fraction_y + 32768) >> 16;
    }
};

} // namespace aa_cluster
