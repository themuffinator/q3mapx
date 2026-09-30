// SPDX-License-Identifier: GPL-3.0-or-later
#include "quake3_materials.h"
#include <algorithm>
#include <span>
#include <stdexcept>

namespace q3mapx {
std::string materialKey(std::string_view name) {
    std::string key(name);
    for(char& c:key) { if(c>='A' && c<='Z') c+=char('a'-'A'); if(c=='\\') c='/'; }
    return key;
}
namespace {
enum class Kind { word,open,close };
struct Token { Kind kind; std::string text; size_t line; };
std::vector<Token> tokenize(std::string_view text) {
    if(text.size()>4*1024*1024) throw std::runtime_error("Material script exceeds 4 MiB");
    // Engine parsers treat NUL as end-of-file even inside a comment. Reject it
    // before skipping comments, rather than inventorying definitions past it.
    if(text.find('\0')!=text.npos) throw std::runtime_error("NUL in material script");
    std::vector<Token> tokens;
    size_t line=0;
    for(size_t p=0;p<text.size();) {
        const unsigned char c=text[p];
        if(c==0) throw std::runtime_error("NUL in material script");
        if(c<=' ') { line+=c=='\n'; ++p; continue; }
        if(text.substr(p,2)=="//") { p=text.find('\n',p); if(p==text.npos) break; continue; }
        if(text.substr(p,2)=="/*") {
            const size_t end=text.find("*/",p+2);
            if(end==text.npos) throw std::runtime_error("Unterminated material comment");
            line+=std::count(text.begin()+p,text.begin()+end,'\n'); p=end+2; continue;
        }
        if(tokens.size()==262144) throw std::runtime_error("Material token limit exceeded");
        const bool quoted=c=='"'; if(quoted) ++p;
        const size_t begin=p;
        while(p<text.size() && (quoted?text[p]!='"':
            static_cast<unsigned char>(text[p])>' ')) {
            if(text[p]==0) throw std::runtime_error("NUL in material token");
            if(static_cast<unsigned char>(text[p])>=128) throw std::runtime_error("Non-ASCII material tokens are outside the renderer contract");
            if(quoted && text[p]=='\n') throw std::runtime_error("Multiline material token is unsupported");
            ++p;
        }
        if(p-begin>=1024) throw std::runtime_error("Material token exceeds 1023 bytes");
        if(quoted && p==text.size()) throw std::runtime_error("Unterminated material string");
        auto value=materialKey(text.substr(begin,p-begin));
        if(quoted && (value=="{" || value=="}")) throw std::runtime_error("Quoted material brace is unsupported");
        const Kind kind=value=="{"?Kind::open:value=="}"?Kind::close:Kind::word;
        tokens.push_back({kind,std::move(value),line});
        if(quoted) ++p;
    }
    return tokens;
}
bool oneOf(std::string_view value,std::initializer_list<std::string_view> values) {
    return std::find(values.begin(),values.end(),value)!=values.end();
}
struct Stage { bool mapped=false,filter=false,lightmap=false; };
std::string inspect(std::span<const Token> tokens,size_t closingLine,bool& lightmap,bool& marksDisabled,bool& dynamicLightsDisabled) {
    size_t pos=0;
    size_t directiveLine=0;
    std::vector<Stage> stages;
    auto word=[&]() -> std::string_view {
        if(pos==tokens.size() || tokens[pos].kind!=Kind::word) return {};
        return tokens[pos++].text;
    };
    auto argument=[&]() -> std::string_view {
        if(pos==tokens.size() || tokens[pos].line!=directiveLine) return {};
        return word();
    };
    auto skip=[&](unsigned count) { while(count--) if(argument().empty()) return false; return true; };
    while(pos<tokens.size()) {
        if(tokens[pos].kind==Kind::open) {
            ++pos; Stage stage;
            while(pos<tokens.size() && tokens[pos].kind!=Kind::close) {
                directiveLine=tokens[pos].line;
                const auto directive=word();
                if(directive=="map" || directive=="clampmap") {
                    if(stage.mapped) return "multiple_stage_maps";
                    const auto image=argument(); if(image.empty()) return "invalid_stage_map";
                    if(image.size()>63) return "oversized_stage_map";
                    if(image.front()=='$' && image!="$lightmap" && image!="$whiteimage") return "unsupported_builtin_image";
                    stage.mapped=true; stage.lightmap=image=="$lightmap";
                }
                else if(directive=="blendfunc") {
                    if(stage.filter) return "multiple_stage_blends";
                    const auto first=argument();
                    if(first=="filter") stage.filter=true;
                    else if(first=="gl_dst_color" && argument()=="gl_zero") stage.filter=true;
                    else return "nonopaque_blending";
                }
                else if(directive=="rgbgen") {
                    if(!oneOf(argument(),{"identity","identitylighting","vertex","exactvertex"})) return "vertex_dependent_rgb";
                }
                else if(directive=="alphagen") {
                    if(!oneOf(argument(),{"identity","vertex"})) return "vertex_dependent_alpha";
                }
                else if(directive=="tcgen" || directive=="texgen") {
                    if(!oneOf(argument(),{"base","lightmap"})) return "vertex_dependent_texcoords";
                }
                else if(directive=="depthfunc") {
                    if(!oneOf(argument(),{"equal","lequal"})) return "unsupported_depth_function";
                }
                else if(directive=="depthwrite") {}
                else return "unsupported_stage_directive:"+std::string(directive);
            }
            if(pos==tokens.size()) return "unclosed_stage";
            ++pos;
            if(!stage.mapped) return "missing_stage_map";
            if(stages.empty()?stage.filter:!stage.filter) return "unsupported_stage_composition";
            lightmap|=stage.lightmap;
            stages.push_back(stage);
            if(stages.size()>2) return "too_many_stages";
        }
        else {
            directiveLine=tokens[pos].line;
            const auto directive=word();
            if(directive=="qer_editorimage" || directive=="q3map_lightimage"
                || directive=="q3map_surfacelight" || directive=="q3map_lightmapsamplesize") {
                if(!skip(1)) return "missing_compiler_argument";
            }
            else if(directive=="q3map_sun" || directive=="q3map_sunext") {
                if(!skip(directive=="q3map_sun"?6:8)) return "missing_compiler_argument";
            }
            else if(directive=="q3map_nolightmap" || directive=="q3map_onlyvertexlighting"
                || directive=="nomipmaps" || directive=="nopicmip") {}
            else if(directive=="surfaceparm") {
                const auto value=argument(); marksDisabled|=value=="nomarks";
                dynamicLightsDisabled|=value=="nodlight";
                if(!oneOf(value,{"nodlight","nomarks","nodamage","nonsolid","detail","structural",
                                   "metalsteps","nosteps","slick","nolightmap"})) return "unsupported_surfaceparm";
            }
            else if(directive=="sort") { if(!oneOf(argument(),{"opaque","3"})) return "nonopaque_sort"; }
            else return "unsupported_material_directive:"+std::string(directive);
            // The renderer skips the rest of qer/q3map lines. Do not approve
            // syntax where that would swallow a stage or closing brace.
            if((directive.starts_with("qer_") || directive.starts_with("q3map_"))
                && (pos<tokens.size()?tokens[pos].line:closingLine)==directiveLine)
                return "compiler_directive_same_line";
        }
    }
    return stages.empty()?"no_render_stages":"";
}
}
std::vector<ReductionMaterial> scanReductionMaterials(std::string_view source) {
    const auto tokens=tokenize(source);
    std::vector<ReductionMaterial> materials;
    for(size_t pos=0;pos<tokens.size();) {
        if(tokens[pos].kind!=Kind::word || tokens[pos].text.empty() || tokens[pos].text.size()>63)
            throw std::runtime_error("Invalid material name");
        ReductionMaterial material{tokens[pos++].text,{}};
        if(pos==tokens.size() || tokens[pos++].kind!=Kind::open) throw std::runtime_error("Missing material opening brace");
        const size_t begin=pos;
        unsigned depth=1;
        while(pos<tokens.size() && depth) {
            if(tokens[pos].kind==Kind::open) { if(++depth>16) throw std::runtime_error("Material nesting limit exceeded"); }
            if(tokens[pos].kind==Kind::close) --depth;
            ++pos;
        }
        if(depth) throw std::runtime_error("Unterminated material definition");
        material.reason=inspect(std::span(tokens).subspan(begin,pos-begin-1),tokens[pos-1].line,material.lightmap,material.marksDisabled,material.dynamicLightsDisabled);
        materials.push_back(std::move(material));
    }
    return materials;
}
}
