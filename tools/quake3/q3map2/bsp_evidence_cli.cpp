// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "arguments.h"
#include "bsp_evidence.h"
#include "bsp_lighting_evidence.h"
#include "q3mapx/bezier_uv.h"
#include "portal_evidence.h"
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
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
constexpr uint64_t maxReportBytes = 64 * 1024 * 1024;
struct Identity { uint64_t bytes; std::string sha256; };
Identity identify(const std::filesystem::path& path, bool bsp = true) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0) throw std::runtime_error("Cannot read evidence source");
    const uint64_t size = uint64_t(input.tellg());
    if (size < 8 || size > (bsp ? uint64_t(0x7fffffff) : q3mapx::PortalLimits{}.bytes))
        throw std::runtime_error("Evidence source exceeds its input byte limits");
    input.seekg(0);
    std::array<unsigned char, 64 * 1024> buffer{};
    std::unique_ptr<GChecksum, decltype(&g_checksum_free)> checksum(g_checksum_new(G_CHECKSUM_SHA256), g_checksum_free);
    uint64_t read = 0;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto bytes = input.gcount();
        if (bsp && read == 0 && (bytes < 8 || std::memcmp(buffer.data(), g_game->bspIdent, 4) != 0
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

template<typename Point> void point(Writer& w,const char* key,const Point& value) {
    w.Key(key); w.StartArray(); for (int i=0;i<3;++i) w.Double(value[i]); w.EndArray();
}
void point2(Writer& w,const char* key,const std::array<double,2>& value) {
    w.Key(key); w.StartArray(); for(double x:value) w.Double(x); w.EndArray();
}
void normal(Writer& w,const char* key,const std::array<double,3>& value) {
    if(value==std::array<double,3>{}) { w.Key(key); w.Null(); } else point(w,key,value);
}
void lightmapRGB(Writer& w,int page,int size,int x,int y) {
    const size_t offset=(size_t(page)*size*size+size_t(y)*size+x)*3;
    const std::array<unsigned char,3> rgb{bspLightBytes[offset],bspLightBytes[offset+1],bspLightBytes[offset+2]};
    values(w,"rgb",rgb); w.Key("has_255_channel"); w.Bool(std::find(rgb.begin(),rgb.end(),255)!=rgb.end());
}
void writeConstantFootprint(Writer& w,const std::array<double,2>& uv,int page,int size) {
    const double x=uv[0]*size-.5,y=uv[1]*size-.5;
    const bool inside=x>=0 && x<=size-1 && y>=0 && y<=size-1;
    w.Key("footprint_status"); w.String(inside?"internal_bilinear_footprint":"outside_internal_texel_centers");
    w.Key("footprint"); w.StartArray();
    if(inside) {
        const int x0=int(std::floor(x)),y0=int(std::floor(y));
        const double fx=x-x0,fy=y-y0;
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);
            if(weight==0) continue;
            const int tx=std::min(x0+dx,size-1),ty=std::min(y0+dy,size-1);
            w.StartObject(); values(w,"texel",std::array{tx,ty}); w.Key("weight"); w.Double(weight);
            lightmapRGB(w,page,size,tx,ty); w.EndObject();
        }
    }
    w.EndArray();
}
void writeLighting(Writer& w,const q3mapx::LightingEvidence& data) {
    w.Key("baked_lighting"); w.StartObject();
    number(w,"schema_version",1); w.Key("status"); w.String(data.status);
    w.Key("light_inference_performed"); w.Bool(false);
    w.Key("encoding"); w.String("stored_bytes_unknown_bake_transfer_function");
    w.Key("sampling"); w.StartObject();
    number(w,"stride",data.options.stride); number(w,"max_observations",data.options.maxObservations);
    number(w,"observations",data.observations); number(w,"max_surfaces",200'000);
    number(w,"max_vertices",2'000'000); number(w,"max_grid_records",2'000'000); number(w,"max_active_workers",32);
    w.Key("method"); w.String("atlas_xy_multiples_of_stride_and_surface_local_vertex_grid_record_constant_primitive_multiples_of_stride");
    w.EndObject();
    w.Key("patch_inverse"); w.StartObject();
    w.Key("method"); w.String("bounded_tensor_biquadratic_uv_subdivision_and_preconditioned_interval_bounds");
    w.Key("tile_order"); w.String("row_major_3x3_control_nets_step_two");
    w.Key("normal_method"); w.String("normalized_biquadratic_stored_control_field");
    w.Key("geometric_normal_method"); w.String("normalized_du_cross_dv_parameter_orientation");
    number(w,"max_depth",q3mapx::bezierMaxDepth); number(w,"max_nodes_per_tile",q3mapx::bezierMaxNodes);
    number(w,"max_root_hits_per_tile_query",q3mapx::bezierMaxRootHits); number(w,"max_iterations",q3mapx::bezierMaxIterations);
    w.Key("uv_residual_limit_texels"); w.Double(q3mapx::bezierUVTolerance);
    w.Key("local_parameter_radius_limit"); w.Double(q3mapx::bezierParameterTolerance); w.EndObject();
    w.Key("atlas"); w.StartObject(); w.Key("status"); w.String(data.atlasStatus);
    number(w,"page_size",data.pageSize); number(w,"bytes",bspLightBytes.size());
    number(w,"complete_pages",data.pages); number(w,"referenced_pages",data.referencedPages);
    w.Key("page_interpretation"); w.String("surface_references_only_no_unreferenced_or_deluxe_page_guessing"); w.EndObject();
    w.Key("surfaces"); w.StartArray();
    for (size_t i=0;i<data.surfaces.size();++i) {
        const auto& item=data.surfaces[i]; const auto& surface=bspDrawSurfaces[i];
        w.StartObject(); number(w,"surface",i); number(w,"surface_type",surface.surfaceType);
        w.Key("model"); if(item.model>=0) w.Int(item.model); else w.Null();
        w.Key("model_ownership"); w.String(item.model==-2?"overlapping":item.model==-1?"unowned":"unique");
        w.Key("coordinate_space"); w.String(item.model==0?"world":item.model>0?"model_local_untransformed":"stored_bsp_unknown_model");
        number(w,"shader",surface.shaderNum);
        w.Key("shader_name"); w.String(bspShaders[surface.shaderNum].shader);
        number(w,"first_vertex",surface.firstVert); number(w,"vertices",surface.numVerts);
        w.Key("lightmap_slots"); w.StartArray();
        for (int slot=0;slot<MAX_LIGHTMAPS;++slot) {
            const auto& info=item.slots[slot];
            w.StartObject(); number(w,"slot",slot); number(w,"style",surface.lightmapStyles[slot]);
            w.Key("page"); w.Int(surface.lightmapNum[slot]);
            w.Key("status"); w.String(info.status);
            number(w,"degenerate_or_ill_conditioned_uv_triangles",info.degenerateUVTriangles);
            number(w,"degenerate_geometry_triangles",info.degenerateGeometryTriangles);
            number(w,"candidate_texels",info.candidateTexels);
            w.Key("observations"); w.StartArray();
            for (const auto& sample:info.observations) {
                w.StartObject(); values(w,"texel",std::array{sample.x,sample.y});
                number(w,"first_triangle",sample.firstTriangle); number(w,"triangle_hits",sample.triangleHits);
                w.Key("ambiguous_mapping"); w.Bool(sample.ambiguous); w.Key("triangle_boundary"); w.Bool(sample.boundary);
                if (sample.ambiguous) { w.Key("position"); w.Null(); w.Key("normal"); w.Null(); }
                else {
                    point(w,"position",sample.position);
                    if(sample.normal==std::array<double,3>{}) { w.Key("normal"); w.Null(); }
                    else point(w,"normal",sample.normal);
                }
                lightmapRGB(w,surface.lightmapNum[slot],data.pageSize,sample.x,sample.y); w.EndObject();
            }
            w.EndArray();
            number(w,"patch_tiles",info.patchTiles); number(w,"patch_subdivision_nodes",info.patchNodes);
            number(w,"patch_unresolved_parameter_regions",info.patchUnresolvedRegions); number(w,"patch_candidate_texels",info.patchCandidateTexels);
            w.Key("patch_observations"); w.StartArray();
            for(const auto& sample:info.patchObservations) {
                w.StartObject(); values(w,"texel",std::array{sample.x,sample.y});
                number(w,"first_tile",sample.firstTile); number(w,"root_hits",sample.rootHits);
                w.Key("ambiguous_mapping"); w.Bool(sample.ambiguous);
                w.Key("unresolved_coverage"); w.Bool(sample.unresolved);
                w.Key("parameter_boundary_tolerance"); w.Bool(sample.boundary);
                const bool known=sample.rootHits && !sample.ambiguous && !sample.unresolved;
                if(known) {
                    point2(w,"tile_parameter",sample.parameter); point2(w,"parameter_radius",sample.parameterRadius);
                    point(w,"position",sample.position); normal(w,"normal",sample.normal); normal(w,"geometric_normal",sample.geometricNormal);
                }
                else for(const char* key:{"tile_parameter","parameter_radius","position","normal","geometric_normal"}) { w.Key(key); w.Null(); }
                w.Key("max_uv_residual_texels"); if(sample.rootHits) w.Double(sample.maxUVResidual); else w.Null();
                lightmapRGB(w,surface.lightmapNum[slot],data.pageSize,sample.x,sample.y); w.EndObject();
            }
            w.EndArray(); number(w,"constant_primitive_regions",info.constantRegions);
            w.Key("constant_regions"); w.StartArray();
            for(const auto& region:info.constants) {
                w.StartObject(); w.Key("primitive_kind"); w.String(region.patch?"bezier_tile":"indexed_triangle");
                number(w,"primitive",region.primitive);
                w.Key("support"); w.String("entire_primitive_shares_one_stored_uv"); point2(w,"uv",region.uv);
                w.Key("representative_method"); w.String(region.patch?"tile_parameter_center":"triangle_centroid");
                point(w,"representative_position",region.position); normal(w,"representative_normal",region.normal);
                normal(w,"representative_geometric_normal",region.geometricNormal);
                writeConstantFootprint(w,region.uv,surface.lightmapNum[slot],data.pageSize); w.EndObject();
            }
            w.EndArray(); w.EndObject();
        }
        w.EndArray();
        w.Key("vertex_role"); w.String(surface.surfaceType==MST_PATCH?"bezier_control":"stored_vertex");
        w.Key("vertex_observations"); w.StartArray();
        for (int v=0;v<surface.numVerts;v+=int(data.options.stride)) for (int slot=0;slot<MAX_LIGHTMAPS;++slot) {
            if (surface.vertexStyles[slot]>=LS_UNUSED) continue;
            const auto& vertex=bspDrawVerts[surface.firstVert+v];
            w.StartObject(); number(w,"vertex",surface.firstVert+v); number(w,"slot",slot); number(w,"style",surface.vertexStyles[slot]);
            point(w,"position",vertex.xyz); point(w,"stored_normal",vertex.normal);
            const auto& color=vertex.color[slot]; values(w,"rgba",std::array{color[0],color[1],color[2],color[3]}); w.EndObject();
        }
        w.EndArray(); w.EndObject();
    }
    w.EndArray();
    const auto& grid=data.grid;
    w.Key("grid"); w.StartObject(); w.Key("status"); w.String(grid.status);
    number(w,"normalized_records",bspGridPoints.size());
    w.Key("pitch_source"); w.String(grid.storedPitch?"worldspawn_gridsize":"conventional_64_64_128");
    if (grid.positionAvailable) {
        point(w,"pitch",grid.pitch); point(w,"origin",grid.origin); values(w,"dimensions",grid.dimensions);
    }
    w.Key("observations"); w.StartArray();
    // Unsupported adapters emit no samples, including normalized grid records.
    if (std::string_view(data.status)=="observations_only") for (size_t i=0;i<bspGridPoints.size();i+=data.options.stride) {
        const auto& record=bspGridPoints[i];
        for (int slot=0;slot<MAX_LIGHTMAPS;++slot) if (record.styles[slot]<LS_UNUSED) {
            w.StartObject(); number(w,"record",i); number(w,"slot",slot); number(w,"style",record.styles[slot]);
            if (grid.positionAvailable) {
                auto position=grid.origin; uint64_t remaining=i;
                for (int a=0;a<3;++a) { position[a]+=double(remaining%grid.dimensions[a])*grid.pitch[a]; remaining/=grid.dimensions[a]; }
                point(w,"conventional_position",position);
            }
            else { w.Key("conventional_position"); w.Null(); }
            const auto& ambient=record.ambient[slot]; const auto& directed=record.directed[slot];
            values(w,"ambient_rgb",std::array{ambient[0],ambient[1],ambient[2]});
            values(w,"directed_rgb",std::array{directed[0],directed[1],directed[2]}); values(w,"latlong_bytes",record.latLong);
            w.EndObject();
        }
    }
    w.EndArray(); w.EndObject();
    w.Key("limitations"); w.StartArray();
    for (const char* note:{
        "Only native IBSP/RBSP adapters are qualified. Styles 254/255 are unused; per-style observations remain separate. No shader assets or external lightmaps are loaded.",
        "RGB is encoded stored data, not linear irradiance. Gamma, exposure, overbright, clamping, debug output and source contributions are unknown. A 255 channel is an endpoint observation, not proof of saturation.",
        "Indexed-triangle atlas positions are barycentric geometric texel centers, not recovered bake rays: padding, dilation, supersampling, nudges, bump normals and filtering cannot be undone here. Shared triangle hits are merged within one surface/slot; disagreeing positions or normals are null and ambiguous.",
        "Indexed-triangle UV inversion rejects determinants <= 1e-12 times squared maximum edge component; barycentric boundary tolerance is 1e-9. Mapping agreement uses 1e-4 + 1e-9 times coordinate magnitude and 1e-5 per normal component. These are floating-point evidence tolerances, not exact topology proofs.",
        "Patch positions evaluate the tensor biquadratic control net, not a guessed bake/runtime tessellation. Stored-normal control interpolation and geometric derivative normals are reported separately; neither reconstructs original bake normals or rays. Vertex RGB/alpha can include author paint and material effects.",
        "Patch subdivision explores folded UV mappings. Conflicting geometric hits or unresolved parameter regions make positions/normals null. Root hits can repeat at subdivision or tile boundaries. Interior contraction enclosures establish a local inverse; boundary-tolerance results meet numeric residual/radius limits without proving exact coverage at the parameter edge.",
        "Constant regions identify full primitives sharing one stored UV, with representative geometry and a conventional internal bilinear texel footprint. Repeated regions/texels are correlated evidence, not independent lighting measurements. No clamping/wrapping is assumed outside internal texel centers; byte interpolation/transfer functions are not applied.",
        "World-model samples are world coordinates. Other model samples remain untransformed; owner entities and runtime poses are not inferred. Coincident surfaces remain distinct observations.",
        "Lightgrid positions use conventional world bounds and stored/default pitch only when their record counts match. Matching counts do not prove the original sampling layout. Zero records may be unpopulated or dark; compiler sample nudges are lost. Raven dictionary indices are already expanded by the native reader.",
        "Only referenced atlas pages are read. External pages, deluxe direction pages, original lights, sky/sun, emitters, ambient and bounce are not identified or fitted. No inferred lights or targets are exported."}) w.String(note);
    w.EndArray(); w.EndObject();
}

void writeCellAdjacency(Writer& w,const q3mapx::CellGraph& graph) {
    w.Key("cell_adjacency"); w.StartObject();
    w.Key("status"); w.String(graph.status);
    w.Key("method"); w.String("bounded_convex_world_path_cells_and_coplanar_face_intersections");
    w.Key("original_prt_recovered"); w.Bool(false);
    w.Key("author_classification_proven"); w.Bool(false);
    bounds(w,"enclosure",graph.mins,graph.maxs);
    w.Key("enclosure_volume"); w.Double(graph.enclosureVolume);
    w.Key("summed_cell_volume"); w.Double(graph.cellVolume);
    number(w,"open_faces_on_enclosure",graph.enclosedOpenFaces);
    number(w,"degenerate_fragments",graph.degenerateFragments);
    const q3mapx::CellLimits limits;
    w.Key("limits"); w.StartObject();
    number(w,"cells",limits.cells); number(w,"stored_boundary_faces",limits.faces);
    number(w,"output_interfaces",limits.faces);
    number(w,"stored_boundary_points",limits.points); number(w,"output_interface_points",limits.points);
    number(w,"faces_per_cell",limits.cellFaces); number(w,"points_per_cell",limits.cellPoints);
    number(w,"pending_points",limits.pendingPoints);
    w.Key("cap_vertex_merge_distance"); w.Double(q3mapx::cellVertexMergeDistance);
    w.Key("minimum_face_area"); w.Double(q3mapx::cellMinimumArea);
    w.Key("max_absolute_coordinate"); w.Double(q3mapx::cellCoordinateLimit);
    w.Key("volume_absolute_tolerance"); w.Double(q3mapx::cellVolumeAbsoluteTolerance);
    w.Key("volume_relative_tolerance"); w.Double(q3mapx::cellVolumeRelativeTolerance); w.EndObject();
    w.Key("cells"); w.StartArray();
    for(size_t i=0;i<graph.cells.size();++i) {
        const auto& cell=graph.cells[i]; w.StartObject(); number(w,"index",i);
        w.Key("leaf"); w.Int(cell.leaf); w.Key("cluster"); w.Int(cell.cluster);
        w.Key("volume"); w.Double(cell.volume); number(w,"faces",cell.faces);
        bounds(w,"bounds",cell.mins,cell.maxs);
        w.Key("interior_point"); w.StartArray(); for(double v:cell.center) w.Double(v); w.EndArray(); w.EndObject();
    }
    w.EndArray(); w.Key("interfaces"); w.StartArray();
    for(const auto& face:graph.interfaces) {
        w.StartObject(); number(w,"front_cell",face.front); number(w,"back_cell",face.back);
        w.Key("partition_node"); w.Int(face.node); w.Key("area"); w.Double(face.area);
        w.Key("kind");
        const int front=graph.cells[face.front].cluster,back=graph.cells[face.back].cluster;
        w.String(front<0 || back<0?"open_opaque":front==back?"within_cluster":"between_clusters");
        w.Key("points"); w.StartArray();
        for(const auto& point:face.points) { w.StartArray(); for(double v:point) w.Double(v); w.EndArray(); }
        w.EndArray(); w.EndObject();
    }
    w.EndArray(); w.EndObject();
}

void writePortals(Writer& w, const q3mapx::PortalGraph& graph, const q3mapx::PortalEvidence& data,
                  const std::filesystem::path& source, const Identity& identity) {
    w.Key("portal_analysis"); w.StartObject();
    w.Key("source"); w.StartObject();
    const auto path=source.generic_u8string();
    if(!g_utf8_validate(reinterpret_cast<const char*>(path.data()),path.size(),nullptr))
        throw std::runtime_error("Portal evidence source path cannot be represented as UTF-8");
    w.Key("path"); w.String(reinterpret_cast<const char*>(path.c_str()));
    w.Key("sha256"); w.String(identity.sha256.c_str()); number(w,"bytes",identity.bytes);
    w.Key("format"); w.String("PRT1"); w.Key("pairing_proven"); w.Bool(false); w.EndObject();
    number(w,"clusters",graph.clusters); number(w,"portals",graph.portals.size()); number(w,"faces",graph.faces.size());
    number(w,"point_occurrences",graph.pointCount); number(w,"components",data.components); number(w,"bridge_portals",data.bridges);
    w.Key("world_mapping_available"); w.Bool(data.worldMapping); number(w,"unmapped_clusters",data.unmappedClusters);
    w.Key("stored_pvs_costs_available"); w.Bool(data.pvsCosts);
    number(w,"ordered_portal_pairs_upper_bound",data.passagePairs);
    const uint64_t portalBytes=((graph.portals.size()*2+63)/64)*8;
    number(w,"portal_bitset_bytes",portalBytes);
    number(w,"three_portal_bitsets_bytes",3*graph.portals.size()*2*portalBytes);
    number(w,"passage_bitsets_bytes_upper_bound",data.passagePairs*portalBytes);
    w.Key("pair_probes"); w.StartObject(); w.Key("normal_offset_units"); w.Double(0.02);
    number(w,"agree",data.probeMatches); number(w,"disagree",data.probeDisagreements);
    number(w,"unavailable",graph.portals.size()-data.probeMatches-data.probeDisagreements);
    number(w,"unusable_windings",data.invalidGeometry); w.EndObject();
    w.Key("limits"); w.StartObject(); number(w,"input_bytes",q3mapx::PortalLimits{}.bytes);
    number(w,"point_occurrences",q3mapx::PortalLimits{}.points); number(w,"brush_sample_per_region",64);
    w.Key("coordinate_magnitude"); w.Double(10'000'000); w.Key("plane_edge_tolerance"); w.Double(0.01); w.EndObject();
    w.Key("regional_mapping"); w.String("exclusive_frontier_membership_with_remainder_for_spanning_or_unmapped_clusters");
    w.Key("region_order"); w.String("descending_ordered_portal_pairs_then_frontier_index");
    w.Key("regions"); w.StartArray();
    for(int index:data.rankedRegions) {
        const auto& r=data.regions[index]; w.StartObject(); number(w,"frontier_index",index);
        w.Key("node"); if(r.node>=0) w.Int(r.node); else w.Null();
        w.Key("kind"); w.String(r.node>=0?"subtree":"remainder_or_spanning");
        values(w,"clusters",r.clusters); number(w,"cluster_count",r.clusters.size());
        number(w,"ordered_portal_pairs_upper_bound",r.passagePairs);
        number(w,"incident_portals",r.incident); number(w,"internal_portals",r.internal); number(w,"boundary_portals",r.boundary);
        number(w,"hint_portals",r.hints); number(w,"sky_portals",r.skies); number(w,"unknown_flag_portals",r.unknownFlags);
        number(w,"bridge_portals",r.bridges); number(w,"small_portals",r.small); number(w,"slender_portals",r.slender);
        number(w,"extra_parallel_openings",r.parallelOpenings);
        values(w,"associated_world_brush_sample",r.associatedBrushSample);
        w.Key("brush_sample_is_complete"); w.Bool(false);
        const bool hasCosts=data.pvsCosts && !r.clusters.empty();
        const double density=hasCosts?double(r.visibleInternalPairs)/(uint64_t(r.clusters.size())*r.clusters.size()):0;
        w.Key("stored_internal_pvs_density"); if(hasCosts) w.Double(density); else w.Null();
        w.Key("mean_visible_world_indexed_triangles"); if(hasCosts) w.Double(double(r.sumVisibleTriangles)/r.clusters.size()); else w.Null();
        w.Key("max_visible_world_indexed_triangles"); if(hasCosts) w.Uint64(r.maxVisibleTriangles); else w.Null();
        w.Key("investigation_reasons"); w.StartArray();
        if(r.clusters.size()>=8 && r.passagePairs>=128) w.String("high_local_portal_pair_count");
        if(hasCosts && r.clusters.size()>=8 && density>=0.9) w.String("weak_stored_occlusion_within_region");
        if(r.parallelOpenings) w.String("multiple_openings_between_same_clusters");
        if(r.slender>=4 && r.slender*4>=r.incident) w.String("many_slender_openings");
        w.EndArray(); w.EndObject();
    }
    w.EndArray();
    w.Key("cluster_costs"); w.StartArray();
    for(size_t i=0;i<data.clusters.size();++i) {
        const auto& c=data.clusters[i]; w.StartObject(); number(w,"cluster",i); number(w,"frontier_index",c.region);
        number(w,"component",c.component); number(w,"reachable_leaf_records",c.leaves); number(w,"degree",c.degree);
        number(w,"unique_world_surfaces",c.worldSurfaces); number(w,"unique_world_indexed_triangles",c.worldTriangles);
        for(auto pair:{std::pair{"visible_world_surfaces",c.visibleSurfaces},
                       {"visible_world_indexed_triangles",c.visibleTriangles},{"visible_world_patch_surfaces",c.visiblePatches}}) {
            w.Key(pair.first); if(data.pvsCosts) w.Uint64(pair.second); else w.Null();
        }
        w.EndObject();
    }
    w.EndArray();
    w.Key("openings"); w.StartArray();
    for(size_t i=0;i<graph.portals.size();++i) {
        const auto& p=graph.portals[i]; const auto& m=data.portals[i]; w.StartObject(); number(w,"portal",i);
        number(w,"front_cluster",p.front); number(w,"back_cluster",p.back); number(w,"flags",p.flags);
        w.Key("planar_convex_within_tolerance"); w.Bool(m.planarConvex); w.Key("graph_bridge"); w.Bool(m.bridge);
        w.Key("cluster_probe"); w.String(m.probe); w.Key("center");
        if(m.planarConvex) { w.StartArray(); for(double v:m.center) w.Double(v); w.EndArray(); } else w.Null();
        for(auto pair:{std::pair{"area",m.area},{"perimeter",m.perimeter},
                       {"compactness",m.compactness},{"area_perimeter_width",m.width}}) {
            w.Key(pair.first); if(m.planarConvex) w.Double(pair.second); else w.Null();
        }
        w.EndObject();
    }
    w.EndArray();
    w.Key("limitations"); w.StartArray();
    for(const char* note:{
        "PRT dimensions and center probes cannot prove that these inputs belong together. Probe disagreement can indicate stale inputs or thin cells; inspect the pair before drawing conclusions.",
        "A region is ranked by sum(degree*(degree-1)), before orientation/PVS pruning. This is a traversal/storage upper bound, not measured VIS time or memory. Bitset estimates omit objects, scratch and allocation overhead.",
        "Small means area below 64 square units; slender means 4*pi*area/perimeter^2 below 0.1. These are observations, not proof of unnecessary splits. Bridge, hint, sky and unknown-flag openings are exposed for protection/review.",
        "World triangle costs deduplicate surface IDs over stored PVS rows and exclude submodels, runtime patch tessellation, external meshes and shader passes. They describe the existing BSP, not a hypothetical transformation.",
        "Brush samples retain up to 64 lowest-index world brushes referenced by regional leaves, then require a local node-plane/leaf-path association. They are incomplete and do not establish which source brush caused a cut.",
        "Repeated cluster neighbors may be separate doorways; similar PVS rows may hide meaningful occlusion. Findings justify source/geometry investigation and controlled trial rebuilds, never automatic deletion or detail conversion.",
        "Opaque face syntax/indices are checked, but their geometry is not analyzed here. No graph, BSP, MAP or PVS transformation is applied."}) w.String(note);
    w.EndArray(); w.EndObject();
}

void writeReport(FILE* output, const std::filesystem::path& source, const Identity& identity,
                 const q3mapx::BSPEvidence& evidence, unsigned regionDepth, uint64_t workLimit,
                 const q3mapx::PortalGraph* portals, const q3mapx::PortalEvidence* portalEvidence,
                 const std::filesystem::path& portalSource, const Identity& portalIdentity,
                 const q3mapx::CellGraph* cellGraph, const q3mapx::LightingEvidence* lighting) {
    ReportStream stream(output); Writer w(stream);
    const auto path = source.generic_u8string();
    if (!g_utf8_validate(reinterpret_cast<const char*>(path.data()), path.size(), nullptr))
        throw std::runtime_error("Evidence source path cannot be represented as UTF-8");
    w.StartObject(); number(w,"schema_version",1);
    w.Key("report_kind"); w.String("bsp_evidence");
    std::string scope="normalized_geometry_partition_associations";
    if(evidence.brushCellsRequested) scope+="_brush_interiors";
    if(portals) scope+="_portal_graph";
    if(cellGraph) scope+="_cell_adjacency";
    if(lighting) scope+="_baked_lighting_observations";
    scope+="_and_stored_pvs";
    w.Key("scope"); w.String(scope.c_str());
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
    if(portals) writePortals(w,*portals,*portalEvidence,portalSource,portalIdentity);
    if(cellGraph) writeCellAdjacency(w,*cellGraph);
    if(lighting) writeLighting(w,*lighting);
    w.Key("limitations"); w.StartArray();
    if(cellGraph) {
        w.String("Cell adjacency reconstructs bounded geometric world-tree leaf paths. Repeated leaf references produce distinct cells; opaque/opaque interfaces are omitted. Original PRT hint/sky flags and compiler portal history are not recoverable from this geometry alone.");
        w.String("The enclosure is the stored world-model bounds expanded by one unit. Open faces touching it make exterior completeness unknown. Volume balance is a consistency check, not proof of exact topology; floating-point clipping, vertex merging and sliver thresholds remain explicit.");
        w.String("Interfaces do not prove source brush causality, detail classification, visibility equivalence or supplied PRT completeness. No portal graph is automatically substituted into VIS.");
    }
    if(evidence.brushCellsRequested) {
        w.String("Brush-cell clipping measures world-space convex interiors, independent of stored leaf-brush references. It does not identify source detail flags or material opacity; brushes in other models are excluded.");
        w.String("Witnesses have at least 0.01 units of clearance from every brush/path plane. Missing witnesses, thin fragments, unavailable enclosures, geometric limits or volume mismatch are inconclusive, not proof of an empty or structural brush.");
        w.String("Interior PVS pairs describe stored cluster visibility, including self/directed pairs. Missing VIS remains unknown; equal or different rows do not prove that a brush caused a split.");
    }
    for(const char* note : {
        "Partition associations are exact unoriented plane matches, not proof that a brush created a portal or was originally structural. Nearby/scaled planes are not merged.",
        "Leaf-path matches restrict associations to ancestors of referenced world leaves. Matching detail faces and submodel faces are still possible; no detail/group/light inference is performed.",
        "Stored PVS describes the compiled result; absent, fast or all-visible VIS cannot establish the original author's classifications. Compile mode cannot be recovered from these bytes.",
        cellGraph?nullptr:"No PRT or leaf-cell adjacency is reconstructed. Regional split counts rank investigation sites; they do not establish excessive or removable portalling.",
        "Regions are non-overlapping node subtrees at the requested depth, or terminal nodes above it. Early leaf children outside those subtrees are omitted. Stored bounds are not reconstructed convex cells.",
        "References count multiplicity, not unique geometry or runtime draw calls. Indexed triangle references omit runtime patch tessellation, shader passes and external model meshes.",
        "Indices use native-reader normalization; early brush source indices are included. Native terrain is included as normalized triangles. Unsupported native extensions follow the recovery-loss list.",
        "No BSP, MAP or PVS edits are made. Source stability is checked by SHA-256 before loading and after analysis; concurrent source/output modification is unsupported."}) if(note) w.String(note);
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
    const char* portalFile = nullptr;
    unsigned regionDepth=4; uint64_t workLimit=50'000'000;
    const bool brushCells=args.takeArg("-brush-cells");
    const bool cellAdjacency=args.takeArg("-cell-adjacency");
    const bool lightingRequested=args.takeArg("-lighting");
    q3mapx::LightingEvidenceOptions lightingOptions;
    const bool lightingStride=args.takeArg("-lighting-stride");
    if(lightingStride) lightingOptions.stride=ParseIntegerOption("-lighting-stride",args.takeNext(),1,1024);
    const bool lightingLimit=args.takeArg("-lighting-max-samples");
    if(lightingLimit) lightingOptions.maxObservations=ParseIntegerOption("-lighting-max-samples",args.takeNext(),1,200'000);
    if((lightingStride || lightingLimit) && !lightingRequested) throw std::runtime_error("Lighting sampling options require -lighting");
    if(args.takeArg("-report")) report=args.takeNext();
    if(args.takeArg("-portals")) portalFile=args.takeNext();
    if(args.takeArg("-region-depth")) regionDepth=ParseIntegerOption("-region-depth",args.takeNext(),0,8);
    if(args.takeArg("-max-work")) workLimit=ParseIntegerOption("-max-work",args.takeNext(),1,100'000'000);
    const char* input=args.size()==1?args.takeFront():nullptr;
    if(!input || !*input || input[0]=='-')
        throw std::runtime_error("Usage: q3mapx -game PROFILE -bsp-evidence [-brush-cells] [-cell-adjacency] [-lighting [-lighting-stride N] [-lighting-max-samples N]] [-portals matching.prt] [-report file.json] [-region-depth 0..8] [-max-work N] file.bsp");
    const auto source=std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(input))).lexically_normal();
    auto destination=source; destination.replace_extension(".evidence.json");
    if(report) destination=std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(report))).lexically_normal();
    if(destination.extension()!=".json") throw std::runtime_error("BSP evidence output must have a .json extension");
    std::error_code ec;
    if(std::filesystem::weakly_canonical(source)==std::filesystem::weakly_canonical(destination)
        || std::filesystem::equivalent(source,destination,ec))
        throw std::runtime_error("BSP evidence output must not replace its input");
    const auto before=identify(source);
    std::filesystem::path portalSource;
    Identity portalIdentity{};
    std::optional<q3mapx::PortalGraph> portals;
    std::optional<q3mapx::PortalEvidence> portalEvidence;
    if(portalFile) {
        portalSource=std::filesystem::absolute(std::filesystem::path(reinterpret_cast<const char8_t*>(portalFile))).lexically_normal();
        if(std::filesystem::weakly_canonical(portalSource)==std::filesystem::weakly_canonical(destination)
            || std::filesystem::equivalent(portalSource,destination,ec))
            throw std::runtime_error("BSP evidence output must not replace its PRT input");
        portalIdentity=identify(portalSource,false);
        portals=q3mapx::readPortalGraph(portalSource);
    }
    LoadBSPFile(input); ParseEntities();
    auto evidence=q3mapx::analyzeBSPEvidence(regionDepth,workLimit);
    std::optional<q3mapx::CellGraph> cellGraph;
    std::optional<q3mapx::LightingEvidence> lighting;
    if(brushCells) q3mapx::analyzeBSPBrushCells(evidence,workLimit);
    if(cellAdjacency) {
        cellGraph=q3mapx::analyzeBSPCellAdjacency(evidence,workLimit);
        Sys_Printf("Cell adjacency: %s; %zu path cells, %zu interfaces, %llu open enclosure faces, %llu degenerate fragments\n",
            cellGraph->status,cellGraph->cells.size(),cellGraph->interfaces.size(),
            (unsigned long long)cellGraph->enclosedOpenFaces,(unsigned long long)cellGraph->degenerateFragments);
    }
    if(portals) {
        portalEvidence=q3mapx::analyzePortalEvidence(evidence,*portals,workLimit);
        if(!portalEvidence->worldMapping || portalEvidence->unmappedClusters || portalEvidence->probeDisagreements || portalEvidence->invalidGeometry)
            Sys_Warning("Portal evidence: world paths %s, %llu unmapped clusters, %llu cluster-probe disagreements, %llu unusable windings; review input correspondence and geometry before using regional findings\n",
                portalEvidence->worldMapping?"available":"unavailable",(unsigned long long)portalEvidence->unmappedClusters,
                (unsigned long long)portalEvidence->probeDisagreements,(unsigned long long)portalEvidence->invalidGeometry);
        const auto after=identify(portalSource,false);
        if(portalIdentity.bytes!=after.bytes || portalIdentity.sha256!=after.sha256)
            throw std::runtime_error("PRT changed during evidence analysis; report not published");
    }
    if(lightingRequested) lighting=q3mapx::analyzeBSPLighting(evidence,workLimit,lightingOptions);
    const auto after=identify(source);
    if(before.bytes!=after.bytes || before.sha256!=after.sha256) throw std::runtime_error("BSP changed during evidence analysis; report not published");
    q3mapx::OutputFiles outputs;
    writeReport(outputs.open(destination),source,before,evidence,regionDepth,workLimit,
        portals?&*portals:nullptr,portalEvidence?&*portalEvidence:nullptr,portalSource,portalIdentity,
        cellGraph?&*cellGraph:nullptr,lighting?&*lighting:nullptr);
    outputs.commit();
    Sys_Printf("BSP evidence: %zu brushes, %llu world nodes, %zu regional summaries; PVS %s\n",
        evidence.brushes.size(),(unsigned long long)evidence.reachableNodes,evidence.regions.size(),evidence.visibility.present?"present":"absent");
    Sys_Printf("Wrote %s\n",destination.string().c_str());
    return 0;
}
catch(const std::exception& error) { Sys_FPrintf(SYS_ERR,"BSP evidence: %s\n",error.what()); return 1; }
