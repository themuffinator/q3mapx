// SPDX-License-Identifier: GPL-3.0-or-later
#include "bsp_formats.h"
#include <algorithm>
#include <bit>

namespace q3mapx {
int32_t bspLittleInt(const uint8_t* b) {
    return std::bit_cast<int32_t>(uint32_t(b[0]) | uint32_t(b[1]) << 8
        | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24);
}

const std::vector<BSPFormat>& bspFormats() {
    static const auto formats = [] {
        std::vector<BSPFormat> result;
        const std::vector<BSPLumpLayout> ibsp = {
            {"entities",0}, {"shaders",72}, {"planes",16}, {"nodes",36}, {"leafs",48},
            {"leaf_surfaces",4}, {"leaf_brushes",4}, {"models",40}, {"brushes",12},
            {"brush_sides",8}, {"vertices",44}, {"indices",4}, {"fogs",72},
            {"surfaces",104}, {"lightmaps",0}, {"lightgrid",8}, {"visibility",0}
        };
        result.push_back({"ibsp46","Quake III family","IBSP",46,8,ibsp});
        result.push_back({"ibsp47","Wolfenstein family / Quake Live base layout","IBSP",47,8,ibsp});
        auto ql = ibsp; ql.push_back({"advertisements",128});
        result.push_back({"quakelive","Quake Live extended directory","IBSP",47,8,ql});
        auto raven = ibsp;
        raven[9].recordBytes=12; raven[10].recordBytes=80;
        raven[13].recordBytes=148; raven[15].recordBytes=30;
        raven.push_back({"light_array",2});
        result.push_back({"rbsp1","Raven (Jedi / Soldier of Fortune II)","RBSP",1,8,raven});
        result.push_back({"fbsp1","Qfusion","FBSP",1,8,raven});
        const std::vector<BSPLumpLayout> ritual = {
            {"shaders",76}, {"planes",16}, {"lightmaps",0}, {"surfaces",108},
            {"vertices",44}, {"indices",4}, {"leaf_brushes",4}, {"leaf_surfaces",4},
            {"leafs",48}, {"nodes",36}, {"brush_sides",8}, {"brushes",12}, {"fogs",72},
            {"models",40}, {"entities",0}, {"visibility",0}, {"lightgrid",8},
            {"entity_lights",0}, {"entity_light_visibility",0}, {"light_definitions",52}
        };
        result.push_back({"fakk12","Heavy Metal: F.A.K.K.2","FAKK",12,12,ritual});
        result.push_back({"fakk42","American McGee's Alice","FAKK",42,12,ritual});
        const std::vector<BSPLumpLayout> mohaa = {
            {"shaders",140}, {"planes",16}, {"lightmaps",0}, {"surfaces",108},
            {"vertices",44}, {"indices",4}, {"leaf_brushes",4}, {"leaf_surfaces",4},
            {"leafs",64}, {"nodes",36}, {"side_equations",32}, {"brush_sides",12},
            {"brushes",12}, {"models",40}, {"entities",0}, {"visibility",0},
            {"lightgrid_palette",0}, {"lightgrid_offsets",2}, {"lightgrid_data",0},
            {"sphere_lights",56}, {"sphere_light_visibility",4}, {"light_definitions",0},
            {"terrain",388}, {"terrain_indices",2}, {"static_model_colors",0},
            {"static_models",164}, {"static_model_indices",2}, {"unused",0}
        };
        result.push_back({"mohaa19","Medal of Honor: Allied Assault","2015",19,12,mohaa});
        auto v45 = ibsp;
        v45[2].recordBytes=20; v45[7].recordBytes=56; v45[12].recordBytes=68;
        result.push_back({"ibsp45","Public Q3Test 1.06-1.08","IBSP",45,8,v45});
        std::vector<BSPLumpLayout> early = {
            {"entities",0}, {"planes",20}, {"nodes",36}, {"leafs",48},
            {"leaf_surfaces",4}, {"leaf_brushes",4}, {"models",48}, {"brushes",12},
            {"brush_sides",8}, {"lightmaps",0}, {"visibility",0}, {"vertices",44},
            {"surfaces",156}, {"fogs",68}
        };
        result.push_back({"ibsp43","Quake III IHV Test","IBSP",43,8,early});
        early[12].recordBytes=164; early.push_back({"indices",4});
        result.push_back({"ibsp44","Public Q3Test 1.02-1.05","IBSP",44,8,early});
        return result;
    }();
    return formats;
}

BSPDirectory inspectBSPDirectory(std::span<const uint8_t> prefix, uint64_t fileBytes,
                                const BSPFormat& format) {
    BSPDirectory result;
    const size_t headerBytes = format.directoryOffset + format.lumps.size() * 8;
    if (prefix.size() < headerBytes || fileBytes < headerBytes) {
        result.errors.push_back("Truncated directory: need " + std::to_string(headerBytes) + " bytes");
        return result;
    }
    for (size_t i=0; i<format.lumps.size(); ++i) {
        const auto* entry = prefix.data() + format.directoryOffset + i*8;
        BSPLumpRange lump{bspLittleInt(entry), bspLittleInt(entry+4)};
        result.lumps.push_back(lump);
        const std::string label = "Lump " + std::to_string(i) + " (" + format.lumps[i].name + "): ";
        if (lump.offset < 0 || lump.length < 0 || uint64_t(lump.offset) > fileBytes
            || uint64_t(lump.length) > fileBytes - uint64_t(lump.offset)
            || (lump.length && uint64_t(lump.offset) < headerBytes)) {
            result.errors.push_back(label + "range is outside the file or overlaps the header");
            continue;
        }
        result.payloadBytes += uint32_t(lump.length);
        const auto record = format.lumps[i].recordBytes;
        if (record && uint32_t(lump.length) % record)
            result.errors.push_back(label + "length is not a multiple of " + std::to_string(record));
        if (!lump.length) continue;
        for (size_t j=0; j<i; ++j) {
            const auto& other=result.lumps[j];
            if (other.offset >= 0 && other.length > 0
                && int64_t(lump.offset) < int64_t(other.offset)+other.length
                && int64_t(other.offset) < int64_t(lump.offset)+lump.length)
                result.errors.push_back(label + "overlaps lump " + std::to_string(j));
        }
    }
    return result;
}
} // namespace q3mapx
