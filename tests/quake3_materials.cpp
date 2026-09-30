// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/quake3_materials.h"
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() try {
    using namespace q3mapx;
    auto good=scanReductionMaterials(R"(
        // An unrelated unsupported definition must remain inventoried.
        bad { deformVertexes wave 1 sin 0 1 0 1 { map x } }
        Textures\Grid { qer_editorimage x.tga /* comment */
          { map $lightmap rgbGen identity tcGen lightmap }
          { map "textures/grid" blendFunc GL_DST_COLOR GL_ZERO tcGen base } }
        unlit { surfaceparm nolightmap surfaceparm nomarks surfaceparm nodlight { map x rgbGen vertex alphaGen vertex } }
        compiler { q3map_sun 1 1 1 50 20 60
          q3map_surfacelight 40
          { map x } }
    )");
    require(good.size()==4 && !good[0].reason.empty(),"Unsupported definition lost");
    require(good[1].name=="textures/grid" && good[1].reason.empty() && good[1].lightmap,"Lightmapped grammar rejected");
    require(good[2].reason.empty() && good[3].reason.empty(),"Simple opaque grammar rejected");
    require(good[2].marksDisabled && good[2].dynamicLightsDisabled && !good[1].marksDisabled
        && !good[1].dynamicLightsDisabled,"Authored runtime protections not preserved");
    unsigned checks=5;
    for(const char* body:{
        "deformVertexes autosprite { map x }", "polygonOffset { map x }", "cull none { map x }",
        "fogparms ( 1 1 1 ) 10 { map x }", "surfaceparm sky { map x }", "sort additive { map x }",
        "{ map x blendFunc add }", "{ map x alphaFunc GE128 }", "{ map x rgbGen lightingDiffuse }",
        "{ map x alphaGen lightingSpecular }", "{ map x tcGen environment }", "{ map x tcMod turb 0 1 0 1 }",
        "{ map x } { map y }", "{ map x } { map y blendFunc add }", "{ animMap 3 x y }",
        "{ map $deluxemap }", "{ map x map y }", "{ map x depthFunc greater }", "{ rgbGen identity }",
        "qer_editorimage", "{ map x { map y } }", "qer_editorimage x { map y }", "{ map\nx }", "{map x}"}) {
        const auto result=scanReductionMaterials("m { "+std::string(body)+" }");
        require(result.size()==1 && !result[0].reason.empty(),"Unsupported behavior accepted"); ++checks;
    }
    for(const char* source:{"m", "m x", "m {", "m { { }", "m { /*", "m { \"", "{ }", "m { { map \"}\" } }"}) {
        bool rejected=false;
        try { scanReductionMaterials(source); } catch(const std::runtime_error&) { rejected=true; }
        require(rejected,"Malformed structure accepted"); ++checks;
    }
    require(scanReductionMaterials("m { { map x } } M { { map y } }").size()==2,"Duplicate inventory lost");
    for(const auto& source:{std::string("/*")+char(0)+"*/ m { { map x } }", std::string("//")+char(0)+"\nm { { map x } }"}) {
        bool rejected=false;
        try { scanReductionMaterials(source); } catch(const std::runtime_error&) { rejected=true; }
        require(rejected,"NUL inside comment hid a renderer end-of-file"); ++checks;
    }
    require(scanReductionMaterials("m { { map "+std::string(63,'x')+" } }")[0].reason.empty(),"Bounded image name rejected");
    require(scanReductionMaterials("m { { map "+std::string(64,'x')+" } }")[0].reason=="oversized_stage_map","Overlong runtime image name accepted");
    checks+=2;
    for(const auto& token:{std::string(1024,'x'),std::string("x")+char(0xc3)+char(0xa9)}) {
        bool rejected=false;
        try { scanReductionMaterials("m { { map "+token+" } }"); } catch(const std::runtime_error&) { rejected=true; }
        require(rejected,"Truncated or signed-char-dependent renderer token accepted"); ++checks;
    }
    std::cout<<checks+1<<" material grammar, protection and malformed-input controls passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
