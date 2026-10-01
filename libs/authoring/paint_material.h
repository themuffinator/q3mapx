// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace q3mapx::authoring {

// A deliberately bounded Quake III material contract for an unlit authoring
// preview. Unknown rendering/compiler directives reject the whole material.
// Identity lighting and $lightmap are white; this does not simulate a bake.
enum class PaintBlend { zero, one, srcAlpha, oneMinusSrcAlpha, dstColor, oneMinusDstColor,
                        srcColor, oneMinusSrcColor, dstAlpha, oneMinusDstAlpha, srcAlphaSaturate };
enum class PaintRGB { identity, vertex, exactVertex };
enum class PaintAlpha { identity, vertex, oneMinusVertex };
enum class PaintAlphaTest { none, gt0, lt128, ge128 };
enum class PaintCull { front, back, none };
struct PaintMaterialStage {
    std::string image;
    PaintRGB rgb = PaintRGB::identity;
    PaintAlpha alpha = PaintAlpha::identity;
    PaintBlend src = PaintBlend::one, dst = PaintBlend::zero;
    PaintAlphaTest alphaTest = PaintAlphaTest::none;
    bool clamp = false, depthWrite = true, depthEqual = false;
    bool blended() const { return src != PaintBlend::one || dst != PaintBlend::zero; }
    std::array<unsigned char, 4> color( const std::array<unsigned char, 4>& vertex ) const {
        auto out = vertex;
        if ( rgb == PaintRGB::identity ) out = {255,255,255,255};
        // Quake III's identity alpha retains vertex alpha for rgbGen vertex
        // when identityLight == 1, including an explicit alphaGen identity.
        if ( alpha == PaintAlpha::identity && rgb != PaintRGB::vertex ) out[3] = 255;
        else if ( alpha == PaintAlpha::vertex ) out[3] = vertex[3];
        else if ( alpha == PaintAlpha::oneMinusVertex ) out[3] = 255 - vertex[3];
        return out;
    }
};
struct PaintMaterial {
    std::vector<PaintMaterialStage> stages;
    PaintCull cull = PaintCull::front;
    std::string error;
    bool valid() const { return error.empty() && !stages.empty(); }
    bool translucent() const { return valid() && stages.front().blended(); }
};

namespace paint_material_detail {
inline std::string lower( std::string_view value ) {
    std::string out( value );
    for ( char& c : out ) if ( c >= 'A' && c <= 'Z' ) c += 'a' - 'A';
    return out;
}
inline bool imagePath( std::string_view path ) {
    if ( path.empty() || path.size() > 240 || path.front() == '/' ) return false;
    std::size_t start = 0;
    for ( std::size_t i = 0; i <= path.size(); ++i ) {
        if ( i == path.size() || path[i] == '/' ) {
            const auto part = path.substr( start, i-start );
            if ( part.empty() || part == "." || part == ".." ) return false;
            start = i + 1;
        }
        else if ( static_cast<unsigned char>( path[i] ) <= 32 || path[i] == ':' || path[i] == '\\'
               || path[i] == '$' || path[i] == '*' || path[i] == '{' || path[i] == '}' ) return false;
    }
    return true;
}
struct Tokens {
    std::string_view source;
    std::size_t at = 0, count = 0;
    std::string error;
    std::string next() {
        if ( !error.empty() ) return {};
        for ( ;; ) {
            while ( at < source.size() && static_cast<unsigned char>( source[at] ) <= 32 ) {
                if ( source[at] == '\0' ) { error = "NUL in shader source"; return {}; }
                ++at;
            }
            if ( at+1 < source.size() && source.substr( at,2 ) == "//" ) {
                at += 2; while ( at < source.size() && source[at] != '\n' ) ++at;
            }
            else if ( at+1 < source.size() && source.substr( at,2 ) == "/*" ) {
                const auto end = source.find( "*/",at+2 );
                if ( end == std::string_view::npos ) { error = "Unterminated shader comment"; return {}; }
                at = end + 2;
            }
            else break;
        }
        if ( at == source.size() ) return {};
        if ( ++count > 262144 ) { error = "Shader token limit exceeded"; return {}; }
        const char first = source[at++];
        if ( first == '{' || first == '}' ) return std::string( 1,first );
        const auto begin = first == '"' ? at : at-1;
        if ( first == '"' ) {
            while ( at < source.size() && source[at] != '"' ) {
                if ( static_cast<unsigned char>( source[at] ) < 32 || at-begin > 1024 ) { error = "Invalid quoted shader token"; return {}; }
                ++at;
            }
            if ( at == source.size() ) { error = "Unterminated shader quote"; return {}; }
            return std::string( source.substr( begin,at++-begin ) );
        }
        while ( at < source.size() && static_cast<unsigned char>( source[at] ) > 32 ) {
            if ( source[at]=='{' || source[at]=='}' || source[at]=='"' || at-begin>1024
              || (at+1<source.size() && (source.substr(at,2)=="//" || source.substr(at,2)=="/*")) ) {
                error="Invalid or unseparated shader token"; return {};
            }
            ++at;
        }
        return std::string( source.substr( begin,at-begin ) );
    }
};
inline bool factor( const std::string& token, PaintBlend& result, bool source ) {
    static const std::array<std::string_view,11> names = {"gl_zero","gl_one","gl_src_alpha","gl_one_minus_src_alpha",
        "gl_dst_color","gl_one_minus_dst_color","gl_src_color","gl_one_minus_src_color","gl_dst_alpha","gl_one_minus_dst_alpha","gl_src_alpha_saturate"};
    for ( std::size_t i = 0; i < names.size(); ++i ) if ( token == names[i] ) {
        // Destination-alpha factors depend on framebuffer alpha availability;
        // NRC/Qt and a game's window format need not agree on that contract.
        if ( i>=8 || (source ? i==6 || i==7 : i==4 || i==5) ) return false;
        result = static_cast<PaintBlend>( i ); return true;
    }
    return false;
}
inline bool stage( Tokens& reader, PaintMaterialStage& result, std::string& error ) {
    bool map = false, depthExplicit = false, closed = false;
    unsigned seen = 0;
    auto unique = [&]( unsigned flag ) { if ( seen & flag ) return false; seen |= flag; return true; };
    for ( ;; ) {
        const auto key = lower( reader.next() );
        if ( key == "}" ) { closed = true; break; }
        if ( key.empty() ) { error = "Incomplete shader stage"; return false; }
        if ( key == "map" || key == "clampmap" ) {
            result.image = reader.next();
            const auto special = lower( result.image );
            if ( !unique(1) || ( !imagePath(result.image) && (key=="clampmap" || (special!="$whiteimage" && special!="$lightmap")) ) ) break;
            if ( special=="$whiteimage" || special=="$lightmap" ) result.image = special;
            result.clamp = key=="clampmap"; map = true;
        }
        else if ( key == "rgbgen" ) {
            const auto value = lower( reader.next() );
            if ( !unique(2) ) break;
            if ( value == "identity" || value == "identitylighting" ) result.rgb = PaintRGB::identity;
            else if ( value == "exactvertex" ) result.rgb = PaintRGB::exactVertex;
            else if ( value == "vertex" ) {
                result.rgb = PaintRGB::vertex;
                if ( result.alpha == PaintAlpha::identity ) result.alpha = PaintAlpha::vertex;
            }
            else { error = "Unsupported rgbGen: " + value; return false; }
        }
        else if ( key == "alphagen" ) {
            const auto value = lower( reader.next() );
            if ( !unique(4) ) break;
            if ( value == "identity" ) result.alpha = PaintAlpha::identity;
            else if ( value == "vertex" ) result.alpha = PaintAlpha::vertex;
            else if ( value == "oneminusvertex" ) result.alpha = PaintAlpha::oneMinusVertex;
            else { error = "Unsupported alphaGen: " + value; return false; }
        }
        else if ( key == "blendfunc" ) {
            const auto value = lower( reader.next() );
            if ( !unique(8) ) break;
            if ( value == "add" ) { result.src = PaintBlend::one; result.dst = PaintBlend::one; }
            else if ( value == "filter" ) { result.src = PaintBlend::dstColor; result.dst = PaintBlend::zero; }
            else if ( value == "blend" ) { result.src = PaintBlend::srcAlpha; result.dst = PaintBlend::oneMinusSrcAlpha; }
            else if ( !factor( value,result.src,true ) || !factor(lower(reader.next()),result.dst,false) ) break;
            if ( !depthExplicit ) result.depthWrite = false;
        }
        else if ( key == "alphafunc" ) {
            const auto value = lower( reader.next() );
            if ( !unique(16) ) break;
            if ( value == "gt0" ) result.alphaTest = PaintAlphaTest::gt0;
            else if ( value == "lt128" ) result.alphaTest = PaintAlphaTest::lt128;
            else if ( value == "ge128" ) result.alphaTest = PaintAlphaTest::ge128;
            else break;
        }
        else if ( key == "depthfunc" ) {
            const auto value = lower( reader.next() );
            if ( !unique(32) || (value!="equal" && value!="lequal") ) break;
            result.depthEqual = value=="equal";
        }
        else if ( key == "depthwrite" ) {
            if ( !unique(64) ) break;
            depthExplicit = result.depthWrite = true;
        }
        else { error = "Unsupported stage directive: " + key; return false; }
        continue;
    }
    if ( !map || !reader.error.empty() ) { error = "Missing/invalid stage map"; return false; }
    // A rejected directive must not accidentally accept a partially read stage.
    // The cursor must have ended at the matching closing brace.
    if ( !closed ) { error = "Invalid or repeated shader directive"; return false; }
    if ( !result.blended() ) result.depthWrite = true;
    // Quake3e changes explicit depthWrite on some alpha blends. Those materials
    // are excluded until renderer-specific ordering/depth behavior is qualified.
    if ( depthExplicit && result.blended() ) { error = "Blended depthWrite is not supported"; return false; }
    return true;
}
} // namespace paint_material_detail

inline PaintMaterial parsePaintMaterial( std::string_view source, std::string_view name ) {
    using namespace paint_material_detail;
    PaintMaterial out;
    auto fail = [&]( std::string reason ) { out.stages.clear(); out.error = std::move(reason); return out; };
    if ( source.size() > 4*1024*1024 ) return fail("Shader source exceeds 4 MiB");
    if ( source.find('\0')!=std::string_view::npos ) return fail("NUL in shader source");
    Tokens reader{source};
    bool found = false;
    while ( true ) {
        const auto candidate = reader.next();
        if ( candidate.empty() ) break;
        if ( candidate=="{" || candidate=="}" || reader.next()!="{" ) return fail("Malformed shader definition");
        const bool selected = lower(candidate)==lower(name);
        if ( selected && found ) return fail("Duplicate shader definition");
        if ( !selected ) {
            int depth = 1;
            while ( depth ) {
                const auto token = reader.next();
                if ( token.empty() ) return fail("Incomplete shader definition");
                if ( token=="{" && ++depth > 32 ) return fail("Shader nesting limit exceeded");
                if ( token=="}" ) --depth;
            }
            continue;
        }
        found = true;
        bool hasCull = false;
        for ( ;; ) {
            const auto key = lower(reader.next());
            if ( key=="}" ) break;
            if ( key.empty() ) return fail("Incomplete shader definition");
            if ( key=="{" ) {
                PaintMaterialStage value;
                if ( out.stages.size() >= 8 ) return fail("More than eight shader stages");
                if ( !stage(reader,value,out.error) ) return fail(out.error);
                out.stages.push_back(std::move(value));
            }
            else if ( key=="cull" ) {
                if ( hasCull ) return fail("Repeated cull directive");
                hasCull = true;
                const auto value = lower(reader.next());
                if ( value=="none" || value=="disable" || value=="twosided" ) out.cull = PaintCull::none;
                else if ( value=="front" ) out.cull = PaintCull::front;
                else if ( value=="back" || value=="backsided" ) out.cull = PaintCull::back;
                else return fail("Unsupported cull mode: "+value);
            }
            else if ( key=="qer_editorimage" || key=="qer_trans" ) {
                const auto value = reader.next();
                if ( value.empty() || value=="{" || value=="}" ) return fail("Missing editor directive argument");
            }
            else if ( key=="surfaceparm" ) {
                const auto value = lower(reader.next());
                if ( value!="trans" && value!="nonsolid" && value!="nolightmap" && value!="nomarks" && value!="nodlight" )
                    return fail("Unsupported surfaceparm: "+value);
            }
            else if ( key!="nomipmaps" && key!="nopicmip" ) return fail("Unsupported shader directive: "+key);
        }
    }
    if ( !reader.error.empty() ) return fail(reader.error);
    if ( !found ) return fail("Shader definition was not found");
    if ( out.stages.empty() ) return fail("Shader has no render stages");
    return out;
}

} // namespace q3mapx::authoring
