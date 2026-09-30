// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "arguments.h"
#include "bsp_evidence.h"
#include "bsp_formats.h"
#include "bspfile_abstract.h"
#include "bspfile_early.h"
#include "bspfile_native.h"
#include "q3mapx/atomic_file.h"
#include "rapidjson/prettywriter.h"
#include <glib.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
constexpr uint64_t maxReportBytes = 64 * 1024 * 1024;
struct Identity { uint64_t bytes; std::string sha256; };
Identity identify(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0) throw std::runtime_error("Cannot read evidence source BSP");
    const uint64_t size = uint64_t(input.tellg());
    if (size < 8 || size > 0x7fffffff) throw std::runtime_error("Evidence source must contain 8..2147483647 bytes");
    input.seekg(0);
    std::array<unsigned char, 64 * 1024> buffer{};
    std::unique_ptr<GChecksum, decltype(&g_checksum_free)> checksum(g_checksum_new(G_CHECKSUM_SHA256), g_checksum_free);
    uint64_t read = 0;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto bytes = input.gcount();
        if (read == 0 && (bytes < 8 || std::memcmp(buffer.data(), g_game->bspIdent, 4) != 0
                || q3mapx::bspLittleInt(buffer.data()+4) != g_game->bspVersion))
            throw std::runtime_error("Selected game profile does not match evidence source signature/version (also required with -force)");
        if (bytes > 0) g_checksum_update(checksum.get(), buffer.data(), size_t(bytes));
        read += uint64_t(bytes);
        if (read > size) throw std::runtime_error("Evidence source changed while being read");
    }
    if (!input.eof() || read != size) throw std::runtime_error("Evidence source changed or could not be read completely");
    return {size, g_checksum_get_string(checksum.get())};
}

// Stream JSON with an explicit output ceiling; no giant intermediate string.
class ReportStream {
public:
    using Ch = char;
    explicit ReportStream(FILE* file) : file_(file) {}
    void Put(char c) {
        if (bytes_ == maxReportBytes) throw std::runtime_error("BSP evidence report exceeds 64 MiB; existing output preserved");
        if (used_ == buffer_.size()) Flush();
        buffer_[used_++] = c; ++bytes_;
    }
    void Flush() {
        if (std::fwrite(buffer_.data(), 1, used_, file_) != used_) throw std::runtime_error("Cannot write BSP evidence report");
        used_ = 0;
    }
    size_t Tell() const { return size_t(bytes_); }
private:
    FILE* file_;
    std::array<char, 64 * 1024> buffer_{};
    size_t used_ = 0;
    uint64_t bytes_ = 0;
};
using Writer = rapidjson::PrettyWriter<ReportStream>;
void number(Writer& w, const char* key, uint64_t value) { w.Key(key); w.Uint64(value); }
template<typename Values> void values(Writer& w, const char* key, const Values& data) {
    w.Key(key); w.StartArray(); for (auto value : data) w.Int(value); w.EndArray();
}
template<typename Min, typename Max> void bounds(Writer& w, const char* key, const Min& lo, const Max& hi) {
    w.Key(key); w.StartObject();
    w.Key("mins"); w.StartArray(); for (size_t i=0; i<3; ++i) w.Double(lo[i]); w.EndArray();
    w.Key("maxs"); w.StartArray(); for (size_t i=0; i<3; ++i) w.Double(hi[i]); w.EndArray(); w.EndObject();
}
void flag(Writer& w, const char* key, int contents, const surfaceParm_t* parm) {
    w.Key(key);
    if (!parm || !parm->contentFlags) w.Null();
    else w.Bool((contents & parm->contentFlags) == parm->contentFlags);
}

void writeReport(FILE* output, const std::filesystem::path& source, const Identity& identity,
                 const q3mapx::BSPEvidence& evidence, unsigned regionDepth, uint64_t workLimit) {
    ReportStream stream(output); Writer w(stream);
    const auto path = source.generic_u8string();
    if (!g_utf8_validate(reinterpret_cast<const char*>(path.data()), path.size(), nullptr))
        throw std::runtime_error("Evidence source path cannot be represented as UTF-8");
    w.StartObject(); number(w,"schema_version",1);
    w.Key("report_kind"); w.String("bsp_evidence");
    w.Key("scope"); w.String(evidence.brushCellsRequested ? "normalized_geometry_partition_associations_brush_interiors_and_stored_pvs" : "normalized_geometry_partition_associations_and_stored_pvs");
    w.Key("source"); w.StartObject();
    w.Key("path"); w.String(reinterpret_cast<const char*>(path.c_str()));
    w.Key("sha256"); w.String(identity.sha256.c_str()); number(w,"bytes",identity.bytes);
    w.Key("game_profile"); w.String(g_game->arg);
    w.Key("compiler_version"); w.String(Q3MAPX_VERSION);
    w.Key("geometry_validated"); w.Bool(true);
    w.Key("external_assets_loaded"); w.Bool(false);
    w.Key("original_compile_settings_known"); w.Bool(false); w.EndObject();
    w.Key("limits"); w.StartObject(); number(w,"analysis_records",2'000'000);
    number(w,"expanded_brush_sides",8'000'000); number(w,"work_units",workLimit);
    number(w,"work_units_used",evidence.workUsed); number(w,"report_bytes",maxReportBytes); w.EndObject();
    if(evidence.brushCellsRequested) {
        w.Key("brush_cell_analysis"); w.StartObject();
        w.Key("method"); w.String("convex_brush_clipping_through_world_tree");
        w.Key("uses_stored_leaf_brush_references"); w.Bool(false);
        w.Key("author_classification_proven"); w.Bool(false);
        number(w,"max_faces_per_cell",256); number(w,"max_points_per_cell",2048);
        number(w,"max_pending_points_per_worker",8192); number(w,"max_active_workers",32);
        w.Key("max_absolute_coordinate"); w.Double(1e7);
        w.Key("minimum_witness_clearance"); w.Double(0.01);
        w.Key("cap_vertex_merge_distance"); w.Double(1e-7); w.EndObject();
    }
    w.Key("counts"); w.StartObject();
    number(w,"models",bspModels.size()); number(w,"brushes",bspBrushes.size());
    number(w,"brush_sides",bspBrushSides.size()); number(w,"planes",bspPlanes.size());
    number(w,"nodes",bspNodes.size()); number(w,"leaves",bspLeafs.size());
    number(w,"surfaces",bspDrawSurfaces.size()); number(w,"entities",entities.size());
    number(w,"native_terrain_triangles",bspNativeTerrainTriangles);
    number(w,"unloaded_static_model_instances",bspNativeStaticModels.size());
    number(w,"normalized_unused_lightmap_uv_pairs",bspNormalizedUnusedLightmapPairs);
    number(w,"normalized_unused_flare_fogs",bspNormalizedUnusedFlareFogs); w.EndObject();
    w.Key("world_graph"); w.StartObject(); w.Key("head");
    if(evidence.hasWorldHead) w.Int(evidence.worldHead); else w.Null();
    number(w,"reachable_nodes",evidence.reachableNodes); number(w,"reachable_leaves",evidence.reachableLeaves);
    w.Key("unique_node_paths"); w.Bool(evidence.uniqueNodePaths);
    w.Key("leaf_path_analysis_available"); w.Bool(evidence.hasWorldHead && evidence.uniqueNodePaths);
    w.EndObject();
    const auto& vis=evidence.visibility;
    w.Key("visibility"); w.StartObject(); w.Key("present"); w.Bool(vis.present);
    number(w,"referenced_clusters",vis.referencedClusters);
    w.Key("stored_clusters"); if(vis.present) w.Int(vis.clusters); else w.Null();
    w.Key("row_bytes"); if(vis.present) w.Int(vis.rowBytes); else w.Null();
    for(auto pair : {std::pair{"visible_pairs",vis.visiblePairs}, {"min_visible_clusters",vis.minVisible},
                     {"max_visible_clusters",vis.maxVisible}, {"missing_self_bits",vis.missingSelfBits}}) {
        w.Key(pair.first); if(vis.present) w.Uint64(pair.second); else w.Null();
    }
    w.Key("density"); if(vis.present && vis.clusters) w.Double(double(vis.visiblePairs)/(uint64_t(vis.clusters)*vis.clusters)); else w.Null();
    w.Key("all_visible"); if(vis.present && vis.clusters) w.Bool(vis.visiblePairs==uint64_t(vis.clusters)*vis.clusters); else w.Null();
    w.Key("compile_mode"); w.String("unknown"); w.EndObject();
    w.Key("models"); w.StartArray();
    for(size_t m=0; m<bspModels.size(); ++m) {
        const auto& model=bspModels[m]; w.StartObject(); number(w,"index",m);
        number(w,"first_brush",model.firstBSPBrush); number(w,"brush_count",model.numBSPBrushes);
        number(w,"first_surface",model.firstBSPSurface); number(w,"surface_count",model.numBSPSurfaces);
        bounds(w,"stored_bounds",model.minmax.mins,model.minmax.maxs); w.EndObject();
    }
    w.EndArray();
    const auto* detail=GetSurfaceParm("detail"); const auto* structural=GetSurfaceParm("structural");
    w.Key("brushes"); w.StartArray();
    for(size_t b=0; b<bspBrushes.size(); ++b) {
        const auto& brush=bspBrushes[b]; const auto& data=evidence.brushes[b];
        w.StartObject(); number(w,"index",b);
        number(w,"source_brush_index",bspEarlyBrushSources.empty()?b:size_t(bspEarlyBrushSources[b]));
        w.Key("model"); if(data.model>=0) w.Int(data.model); else w.Null();
        w.Key("model_ownership"); w.String(data.model>=0?"unique":data.model==-1?"unowned":"overlapping");
        number(w,"shader_index",brush.shaderNum); number(w,"first_side",brush.firstSide); number(w,"side_count",brush.numSides);
        const int contents=bspShaders[brush.shaderNum].contentFlags;
        number(w,"content_flags",uint32_t(contents));
        flag(w,"stored_detail_content_flag",contents,detail); flag(w,"stored_structural_content_flag",contents,structural);
        number(w,"leaf_references",data.leafReferences); number(w,"nonopaque_leaf_references",data.nonopaqueReferences);
        number(w,"world_leaf_references",data.worldReferences);
        values(w,"world_partition_side_indices",data.partitionSides);
        if(evidence.hasWorldHead && evidence.uniqueNodePaths) values(w,"leaf_path_partition_side_indices",data.leafPathSides);
        else { w.Key("leaf_path_partition_side_indices"); w.Null(); }
        if(data.axialEnclosureAvailable) bounds(w,"axial_plane_enclosure",data.mins,data.maxs);
        else { w.Key("axial_plane_enclosure"); w.Null(); }
        if(evidence.brushCellsRequested) {
            const auto& cells=evidence.brushCells[b];
            w.Key("interior_cells"); w.StartObject(); w.Key("status"); w.String(cells.status);
            number(w,"leaf_fragments",cells.leafFragments); number(w,"uncertain_fragments",cells.uncertainFragments);
            w.Key("brush_volume"); if(cells.brushVolume>0) w.Double(cells.brushVolume); else w.Null();
            w.Key("fragment_volume"); if(cells.brushVolume>0) w.Double(cells.fragmentVolume); else w.Null();
            for(auto pair:{std::pair{"open_witness",&cells.open},{"opaque_witness",&cells.opaque}}) {
                w.Key(pair.first); const auto& witness=*pair.second;
                if(!witness.available) { w.Null(); continue; }
                w.StartObject(); number(w,"leaf",witness.leaf); w.Key("cluster"); w.Int(witness.cluster);
                w.Key("point"); w.StartArray(); for(double v:witness.point) w.Double(v); w.EndArray();
                w.Key("clearance"); w.Double(witness.clearance); w.EndObject();
            }
            values(w,"interior_clusters",cells.interiorClusters);
            for(auto pair:{std::pair{"tested_pvs_pairs",cells.testedPVSPairs},{"invisible_pvs_pairs",cells.invisiblePVSPairs}}) {
                w.Key(pair.first); if(evidence.visibility.present) w.Uint64(pair.second); else w.Null();
            }
            w.EndObject();
        }
        w.EndObject();
    }
    w.EndArray();
    number(w,"region_depth",regionDepth);
    w.Key("region_order"); w.String("descending_subtree_split_nodes_then_node_index");
    w.Key("regions"); w.StartArray();
    for(const auto& region : evidence.regions) {
        w.StartObject(); number(w,"node",region.node); number(w,"depth",region.depth);
        number(w,"deepest_leaf_depth",region.deepestLeaf);
        const auto& node=bspNodes[region.node]; number(w,"partition_plane",node.planeNum);
        bounds(w,"stored_bounds",node.minmax.mins,node.minmax.maxs);
        number(w,"split_nodes",region.splitNodes); number(w,"leaf_paths",region.leafPaths);
        number(w,"nonopaque_leaf_paths",region.nonopaqueLeafPaths);
        number(w,"brush_references",region.brushReferences); number(w,"surface_references",region.surfaceReferences);
        number(w,"indexed_triangle_references",region.indexedTriangleReferences); number(w,"patch_references",region.patchReferences);
        w.EndObject();
    }
    w.EndArray();
    w.Key("limitations"); w.StartArray();
    if(evidence.brushCellsRequested) {
        w.String("Brush-cell clipping measures world-space convex interiors, independent of stored leaf-brush references. It does not identify source detail flags or material opacity; brushes in other models are excluded.");
        w.String("Witnesses have at least 0.01 units of clearance from every brush/path plane. Missing witnesses, thin fragments, unavailable enclosures, geometric limits or volume mismatch are inconclusive, not proof of an empty or structural brush.");
        w.String("Interior PVS pairs describe stored cluster visibility, including self/directed pairs. Missing VIS remains unknown; equal or different rows do not prove that a brush caused a split.");
    }
    for(const char* note : {
        "Partition associations are exact unoriented plane matches, not proof that a brush created a portal or was originally structural. Nearby/scaled planes are not merged.",
        "Leaf-path matches restrict associations to ancestors of referenced world leaves. Matching detail faces and submodel faces are still possible; no detail/group/light inference is performed.",
        "Stored PVS describes the compiled result; absent, fast or all-visible VIS cannot establish the original author's classifications. Compile mode cannot be recovered from these bytes.",
        "No PRT or leaf-cell adjacency is reconstructed. Regional split counts rank investigation sites; they do not establish excessive or removable portalling.",
        "Regions are non-overlapping node subtrees at the requested depth, or terminal nodes above it. Early leaf children outside those subtrees are omitted. Stored bounds are not reconstructed convex cells.",
        "References count multiplicity, not unique geometry or runtime draw calls. Indexed triangle references omit runtime patch tessellation, shader passes and external model meshes.",
        "Indices use native-reader normalization; early brush source indices are included. Native terrain is included as normalized triangles. Unsupported native extensions follow the recovery-loss list.",
        "No BSP, MAP or PVS edits are made. Source stability is checked by SHA-256 before loading and after analysis; concurrent source/output modification is unsupported."}) w.String(note);
    w.EndArray();
    w.Key("native_recovery_losses"); w.StartArray();
    for(const auto& loss:bspRecoveryLosses) {
        w.StartObject(); w.Key("feature"); w.String(loss.feature); number(w,"bytes",loss.bytes);
        w.Key("reason"); w.String(loss.reason); w.EndObject();
    }
    w.EndArray(); w.EndObject(); stream.Put('\n'); stream.Flush();
}
}

int BSPEvidenceMain(Args& args) try {
    const char* report = nullptr;
    unsigned regionDepth=4; uint64_t workLimit=50'000'000;
    const bool brushCells=args.takeArg("-brush-cells");
    if(args.takeArg("-report")) report=args.takeNext();
    if(args.takeArg("-region-depth")) regionDepth=ParseIntegerOption("-region-depth",args.takeNext(),0,8);
    if(args.takeArg("-max-work")) workLimit=ParseIntegerOption("-max-work",args.takeNext(),1,100'000'000);
    if(args.size()!=1 || args.getVector().front()[0]=='-')
        throw std::runtime_error("Usage: q3mapx -game PROFILE -bsp-evidence [-brush-cells] [-report file.json] [-region-depth 0..8] [-max-work N] file.bsp");
    const char* input=args.takeFront();
    const auto source=std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(input))).lexically_normal();
    auto destination=source; destination.replace_extension(".evidence.json");
    if(report) destination=std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(report))).lexically_normal();
    if(destination.extension()!=".json") throw std::runtime_error("BSP evidence output must have a .json extension");
    std::error_code ec;
    if(std::filesystem::weakly_canonical(source)==std::filesystem::weakly_canonical(destination)
        || std::filesystem::equivalent(source,destination,ec))
        throw std::runtime_error("BSP evidence output must not replace its input");
    const auto before=identify(source);
    LoadBSPFile(input); ParseEntities();
    auto evidence=q3mapx::analyzeBSPEvidence(regionDepth,workLimit);
    if(brushCells) q3mapx::analyzeBSPBrushCells(evidence,workLimit);
    const auto after=identify(source);
    if(before.bytes!=after.bytes || before.sha256!=after.sha256) throw std::runtime_error("BSP changed during evidence analysis; report not published");
    q3mapx::OutputFiles outputs;
    writeReport(outputs.open(destination),source,before,evidence,regionDepth,workLimit);
    outputs.commit();
    Sys_Printf("BSP evidence: %zu brushes, %llu world nodes, %zu regional summaries; PVS %s\n",
        evidence.brushes.size(),(unsigned long long)evidence.reachableNodes,evidence.regions.size(),evidence.visibility.present?"present":"absent");
    Sys_Printf("Wrote %s\n",destination.string().c_str());
    return 0;
}
catch(const std::exception& error) { Sys_FPrintf(SYS_ERR,"BSP evidence: %s\n",error.what()); return 1; }
