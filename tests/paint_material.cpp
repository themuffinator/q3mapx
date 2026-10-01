// SPDX-License-Identifier: GPL-3.0-or-later
#include "authoring/paint_material.h"
#include "authoring/paint_tga.h"
#include <cstdlib>
#include <iostream>
#include <random>

using namespace q3mapx::authoring;
static unsigned checks=0;
static void require(bool pass, const std::string& what) {
    ++checks; if (!pass) { std::cerr << "Material check: " << what << '\n'; std::exit(1); }
}
static PaintMaterial parse(const std::string& body) { return parsePaintMaterial("textures/test { "+body+" }","textures/test"); }
int main() {
    auto m=parse("{ map $whiteimage rgbGen vertex }");
    require(m.valid() && m.stages.size()==1,"vertex shader");
    for (int value=0;value<256;++value) {
        const std::array<unsigned char,4> rgba={static_cast<unsigned char>(value),51,202,static_cast<unsigned char>(255-value)};
        for (const auto* rgb : {"identity","identityLighting","vertex","exactVertex"}) {
            for (const auto* alpha : {"identity","vertex","oneMinusVertex"}) {
                m=parse(std::string("{ map $whiteimage rgbGen ")+rgb+" alphaGen "+alpha+" }");
                require(m.valid(),"supported color generators");
                auto want=rgba;
                if (std::string(rgb)=="identity" || std::string(rgb)=="identityLighting") want={255,255,255,255};
                if (std::string(alpha)=="identity" && std::string(rgb)!="vertex") want[3]=255;
                if (std::string(alpha)=="vertex") want[3]=rgba[3];
                if (std::string(alpha)=="oneMinusVertex") want[3]=255-rgba[3];
                require(m.stages[0].color(rgba)==want,"neutral-light vertex channel semantics");
            }
        }
    }
    m=parse("cull disable { clampMap textures/a.tga rgbGen vertex alphaGen vertex alphaFunc GE128 depthWrite } { map $lightmap blendFunc filter depthFunc equal }");
    require(m.valid() && m.cull==PaintCull::none && m.stages.size()==2 && m.stages[0].depthWrite && !m.stages[1].depthWrite && m.stages[1].depthEqual,"two-pass cutout and equal-depth lightmap");
    const std::string names[]={"GL_ZERO","GL_ONE","GL_SRC_ALPHA","GL_ONE_MINUS_SRC_ALPHA","GL_DST_COLOR","GL_ONE_MINUS_DST_COLOR",
        "GL_SRC_COLOR","GL_ONE_MINUS_SRC_COLOR","GL_DST_ALPHA","GL_ONE_MINUS_DST_ALPHA","GL_SRC_ALPHA_SATURATE"};
    for (unsigned s=0;s<11;++s) for(unsigned d=0;d<11;++d) {
        m=parse("{ map $whiteimage blendFunc "+names[s]+" "+names[d]+" }");
        const bool allowed=s<8 && d<8 && s!=6 && s!=7 && d!=4 && d!=5;
        require(m.valid()==allowed,"valid source/destination blend factors");
        if (allowed) require(m.stages[0].depthWrite==(s==1&&d==0),"implicit opaque depth-write normalization");
    }
    for (const auto& body : {
        "{ map $whiteimage rgbGen wave sin 0 1 0 1 }", "{ map $whiteimage alphaGen portal 256 }",
        "{ map $whiteimage tcMod scroll 1 1 }", "{ animMap 1 a b }", "deformVertexes wave 1 sin 0 1 0 1 { map $whiteimage }",
        "surfaceparm sky { map $whiteimage }", "surfaceparm fog { map $whiteimage }", "surfaceparm nodraw { map $whiteimage }",
        "q3map_rgbMod scale 0.5 { map $whiteimage }", "q3map_alphaMod volume { map $whiteimage }",
        "{ map $whiteimage blendFunc blend depthWrite }", "{ map $whiteimage depthWrite blendFunc add }",
        "{ map $whiteimage map $lightmap }", "{ map $whiteimage rgbGen vertex rgbGen identity }",
        "{ map $whiteimage depthFunc foo }", "{ map $whiteimage blendFunc GL_ONE }", "{ map $whiteimage alphaFunc BAD }",
        "{ map $whiteimage alphaGen }", "{ map $whiteimage { } }", "{ rgbGen vertex }",
        "{ map ../private }", "{ map C:/private }", "{ map /private }", "{ map a/../b }", "{ map a\\b }", "{ map $unknown }",
        "{ clampMap $lightmap }", "cull front cull back { map $whiteimage }", "{ map $whiteimage } trailing",
        "qer_editorimage }", "{ map $whiteimage detail }", "{ map $whiteimage tcGen environment }"
    }) require(!parse(body).valid(),std::string("reject unsupported or malformed: ")+body);
    const std::string complete="textures/test { /* comment */ qer_editorimage \"textures/a.tga\"\n { map $whiteimage rgbGen vertex } }";
    require(parsePaintMaterial(complete,"TEXTURES/TEST").valid(),"case-insensitive name, quotes and comments");
    for (std::size_t n=0;n<complete.size();++n) require(!parsePaintMaterial(complete.substr(0,n),"textures/test").valid(),"every truncated definition rejected");
    require(!parsePaintMaterial(complete+complete,"textures/test").valid(),"duplicate rejected");
    require(!parsePaintMaterial(complete+" /*", "textures/test").valid(),"unterminated trailing comment rejected");
    require(!parsePaintMaterial(complete+std::string(1,'\0'),"textures/test").valid(),"trailing NUL rejected");
    require(!parsePaintMaterial("/*"+std::string(1,'\0')+"*/"+complete,"textures/test").valid(),"comment NUL cannot hide an engine source terminator");
    require(!parse("{ map $whiteimage rgbGen vertex/*adjacent*/ }").valid(),"unseparated comment has no invented token boundary");
    require(!parsePaintMaterial(std::string(4*1024*1024+1,' '),"textures/test").valid(),"source size bounded");
    require(!parse(std::string(1026,'a')).valid(),"token length bounded");
    std::string stages; for(int i=0;i<9;++i) { stages+="{ map $whiteimage }"; require(parse(stages).valid()==(i<8),"stage budget"); }
    require(parsePaintMaterial("unrelated { unsupported { tcMod noise 1 } } "+complete,"textures/test").valid(),"unrelated bounded definitions skipped");
    std::mt19937 rng(0x51ade);
    const auto header=[](unsigned type,unsigned bits,unsigned flags) {
        std::string out(18,'\0'); out[2]=type; out[12]=2; out[14]=2; out[16]=bits; out[17]=flags; return out;
    };
    for(unsigned flags : {0u,8u,16u,24u,32u,40u,48u,56u}) for(unsigned bits : {24u,32u}) {
        std::string raw=header(2,bits,flags);
        for(unsigned i=0;i<4;++i) { raw+=char(10+i); raw+=char(20+i); raw+=char(30+i); if(bits==32) raw+=char(0); }
        auto image=decodePaintTga(raw);
        require(image && image->width==2 && image->height==2,"TGA bounds and formats");
        for(unsigned i=0;i<4;++i) {
            const unsigned src=i^2;
            require(image->rgba[i*4]==30+src && image->rgba[i*4+1]==20+src && image->rgba[i*4+2]==10+src && image->rgba[i*4+3]==(bits==32?0:255),"Quake III origin/alpha semantics");
        }
        auto rle=header(10,bits,flags); rle+=char(3); rle+=raw.substr(18);
        require(decodePaintTga(rle)->rgba==image->rgba,"raw-packet TGA parity");
        auto repeated=header(10,bits,flags); repeated+=char(0x83); repeated+=raw.substr(18,bits/8);
        auto decoded=decodePaintTga(repeated); require(decoded && decoded->rgba.size()==16,"RLE run across row boundary");
        for(unsigned i=0;i<4;++i) require(decoded->rgba[i*4]==30 && decoded->rgba[i*4+3]==(bits==32?0:255),"RLE channels and zero alpha");
        for(std::size_t n=0;n<raw.size();++n) require(!decodePaintTga(raw.substr(0,n)),"truncated TGA rejected");
        for(std::size_t n=0;n<rle.size();++n) require(!decodePaintTga(rle.substr(0,n)),"truncated RLE TGA rejected");
        repeated[18]=char(0x84); require(!decodePaintTga(repeated),"excess RLE pixel count rejected");
    }
    for(unsigned field : {1u,2u,12u,13u,14u,15u,16u,17u}) {
        auto bad=header(2,32,8); bad.append(16,'\0'); bad[field]=char(255);
        require(!decodePaintTga(bad),"unsupported or excessive TGA header rejected");
    }
    for (int i=0;i<20000;++i) {
        std::string text; const unsigned n=rng()%256;
        for(unsigned j=0;j<n;++j) text+=char(rng()%128);
        require(!parsePaintMaterial(text,"textures/test").valid(),"malformed byte corpus rejects without crashing");
        require(!decodePaintTga(text),"malformed TGA byte corpus rejects without crashing");
    }
    std::cout << "Paint material: " << checks << " checks passed\n";
}
