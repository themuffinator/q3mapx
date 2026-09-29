// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace q3mapx {
struct BSPLumpLayout {
    const char* name;
    unsigned recordBytes; // zero denotes variable/opaque payload, not a record count
};
struct BSPFormat {
    const char* id;
    const char* title;
    const char* ident;
    int version;
    unsigned directoryOffset;
    std::vector<BSPLumpLayout> lumps;
};
struct BSPLumpRange { int32_t offset, length; };
struct BSPDirectory {
    std::vector<BSPLumpRange> lumps;
    std::vector<std::string> errors;
    uint64_t payloadBytes = 0;
};
// Layout facts from NRC and the pinned fnTech3 headers; see GAME-COVERAGE.md.
const std::vector<BSPFormat>& bspFormats();
int32_t bspLittleInt(const uint8_t* bytes);
BSPDirectory inspectBSPDirectory(std::span<const uint8_t> prefix, uint64_t fileBytes,
                                const BSPFormat& format);
} // namespace q3mapx
