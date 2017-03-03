// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <intel-hda-driver-utils/audio2-proto.h>

namespace audio2_proto {

const char* SampleFormatToString(SampleFormat sample_format) {
    auto fmt = static_cast<SampleFormat>(sample_format & ~AUDIO2_SAMPLE_FORMAT_FLAG_MASK);
    switch (fmt) {
    case AUDIO2_SAMPLE_FORMAT_BITSTREAM:    return "BITSTREAM";
    case AUDIO2_SAMPLE_FORMAT_8BIT:         return "8BIT";
    case AUDIO2_SAMPLE_FORMAT_16BIT:        return "16BIT";
    case AUDIO2_SAMPLE_FORMAT_20BIT_PACKED: return "20BIT_PACKED";
    case AUDIO2_SAMPLE_FORMAT_24BIT_PACKED: return "24BIT_PACKED";
    case AUDIO2_SAMPLE_FORMAT_20BIT_IN32:   return "20BIT_IN32";
    case AUDIO2_SAMPLE_FORMAT_24BIT_IN32:   return "24BIT_IN32";
    case AUDIO2_SAMPLE_FORMAT_32BIT_FLOAT:  return "32BIT_FLOAT";
    default:                                return "<unknown>";
    }
}

}  // audio2_proto
