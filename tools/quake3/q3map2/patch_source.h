// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace q3mapx {
inline bool retainPatchSources = true;
struct PatchSource {
	int model, entity, primitive, width, height, mode, subdivisions, sampleSize;
	std::string shader;
	std::vector<bspDrawVert_t> controls; // Model-local, before tessellation/material modifiers.
	std::vector<int> surfaces;
};
void ResetPatchSources();
int CapturePatchSource( const parseMesh_t& patch );
void LinkPatchSourceSurface( const mapDrawSurface_t& surface );
void ReadPatchSourceTrailer( const char* filename );
std::vector<byte> PatchSourceTrailer(); // Validate retained geometry before serialization.
void ValidatePatchSources();
const std::vector<PatchSource>& PatchSources();
}
