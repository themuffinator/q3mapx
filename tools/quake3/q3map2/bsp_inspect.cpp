// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "bsp_formats.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <array>
#include <filesystem>
#include <fstream>

int InspectBSPMain(Args& args) {
    const bool json = args.takeArg("-json");
    std::vector<std::string> errors, notes;
    const game_t* selected = nullptr;
    if (args.takeArg("-game")) {
        if (!args.nextAvailable()) errors.emplace_back("Missing profile after -game");
        else {
            const char* name = args.takeNext(); selected = FindGame(name);
            if (!selected) errors.push_back(std::string("Unknown game profile: ") + name + "; use -games");
        }
    }
    std::string path;
    if (args.size() != 1) errors.emplace_back("Usage: q3mapx -inspect [-json] [-game PROFILE] file.bsp");
    else path = args.takeFront();
    std::array<uint8_t,256> prefix{}; size_t bytes=0; uint64_t fileBytes=0;
    if (errors.empty()) {
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0) errors.emplace_back("Cannot open BSP for reading");
        else {
            fileBytes = uint64_t(file.tellg()); file.seekg(0);
            const auto count = std::streamsize(std::min<uint64_t>(fileBytes,prefix.size()));
            file.read(reinterpret_cast<char*>(prefix.data()),count); bytes = size_t(file.gcount());
            if (bytes != size_t(count)) errors.emplace_back("BSP changed or could not be read");
            if (bytes < 8) errors.emplace_back("Truncated BSP signature/version");
        }
    }
    std::string ident; int32_t version=0;
    std::vector<const game_t*> candidates;
    struct Layout { const q3mapx::BSPFormat* format; q3mapx::BSPDirectory directory; };
    std::vector<Layout> layouts;
    if (errors.empty()) {
        ident.assign(reinterpret_cast<const char*>(prefix.data()),4);
        version = q3mapx::bspLittleInt(prefix.data()+4);
        for (const auto& game : g_games)
            if (ident == game.bspIdent && version == game.bspVersion) candidates.push_back(&game);
        if (selected && (ident != selected->bspIdent || version != selected->bspVersion))
            errors.emplace_back("Selected profile does not match the file signature/version");
        for (const auto& format : q3mapx::bspFormats()) {
            if (ident != format.ident || version != format.version) continue;
            if (selected && ident == "IBSP" && version == 47
                && (strEqual(selected->arg,"quakelive") != strEqual(format.id,"quakelive"))) continue;
            layouts.push_back({&format,q3mapx::inspectBSPDirectory({prefix.data(),bytes},fileBytes,format)});
        }
        if (layouts.empty()) errors.emplace_back("Unrecognized BSP signature/version; no directory was guessed");
        else if (std::none_of(layouts.begin(),layouts.end(),[](const auto& l){return l.directory.errors.empty();}))
            errors.emplace_back("No matching directory layout is structurally valid; see layout errors");
        if (candidates.size() > 1) notes.emplace_back("Shared signature: select -game explicitly for recovery or processing; format alone cannot identify the game");
        if (candidates.empty() && !layouts.empty()) notes.emplace_back("Recognized layout has no native reader in this build; inspection does not enable recovery or compilation");
        if (ident == "IBSP" && version == 47 && !selected)
            notes.emplace_back("Both 17-lump and 18-lump interpretations are reported; an empty advertisement extension cannot prove Quake Live identity");
        if (fileBytes > INT_MAX) notes.emplace_back("File exceeds the compiler's 2 GiB load/write limit; directory inspection alone remains available");
    }
    const bool valid=errors.empty();
    std::string displayIdent=ident, identHex;
    constexpr char digits[]="0123456789abcdef";
    for (char& c:displayIdent) if(uint8_t(c)<32 || uint8_t(c)>126) c='?';
    for (uint8_t c:ident) { identHex+=digits[c>>4]; identHex+=digits[c&15]; }
    if (json) {
        rapidjson::StringBuffer buffer; rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
        w.StartObject(); w.Key("schema_version"); w.Int(1);
        w.Key("inspection_scope"); w.String("signature_and_lump_directory");
        w.Key("geometry_validated"); w.Bool(false);
        w.Key("valid"); w.Bool(valid); w.Key("file"); w.String(path.c_str());
        w.Key("file_bytes"); w.Uint64(fileBytes);
        w.Key("ident"); w.String(displayIdent.c_str()); w.Key("ident_hex"); w.String(identHex.c_str());
        w.Key("version"); w.Int(version);
        w.Key("selected_profile"); if(selected) w.String(selected->arg); else w.Null();
        w.Key("profile_candidates"); w.StartArray(); for(const auto* p:candidates) w.String(p->arg); w.EndArray();
        w.Key("ambiguous_game"); w.Bool(candidates.size()>1);
        w.Key("layouts"); w.StartArray();
        for(const auto& l:layouts) {
            const auto& f=*l.format;
            w.StartObject(); w.Key("id"); w.String(f.id); w.Key("title"); w.String(f.title);
            w.Key("valid"); w.Bool(l.directory.errors.empty());
            w.Key("header_bytes"); w.Uint(unsigned(f.directoryOffset+f.lumps.size()*8));
            if(f.directoryOffset == 12 && bytes >= 12) { w.Key("stored_checksum"); w.Uint(uint32_t(q3mapx::bspLittleInt(prefix.data()+8))); }
            w.Key("payload_bytes"); w.Uint64(l.directory.payloadBytes);
            w.Key("lumps"); w.StartArray();
            for(size_t i=0;i<l.directory.lumps.size();++i) {
                const auto& range=l.directory.lumps[i];
                w.StartObject(); w.Key("index"); w.Uint(unsigned(i)); w.Key("name"); w.String(f.lumps[i].name);
                w.Key("offset"); w.Int(range.offset); w.Key("bytes"); w.Int(range.length);
                w.Key("record_bytes"); w.Uint(f.lumps[i].recordBytes);
                w.Key("records");
                if(f.lumps[i].recordBytes && range.length >= 0 && unsigned(range.length)%f.lumps[i].recordBytes==0)
                    w.Uint(unsigned(range.length)/f.lumps[i].recordBytes);
                else w.Null();
                w.EndObject();
            }
            w.EndArray(); w.Key("errors"); w.StartArray(); for(const auto& e:l.directory.errors) w.String(e.c_str()); w.EndArray(); w.EndObject();
        }
        w.EndArray();
        w.Key("errors"); w.StartArray(); for(const auto& e:errors) w.String(e.c_str()); w.EndArray();
        w.Key("notes"); w.StartArray(); for(const auto& n:notes) w.String(n.c_str()); w.EndArray(); w.EndObject();
        printf("%s\n",buffer.GetString());
    } else {
        printf("BSP inspection: %s\nSignature: %.4s %d; %llu bytes\n",path.c_str(),displayIdent.c_str(),version,(unsigned long long)fileBytes);
        printf("Directory checks only; geometry and game compatibility are not validated.\n");
        if(!candidates.empty()) { printf("Profile candidates:"); for(const auto* p:candidates) printf(" %s",p->arg); printf("\n"); }
        for(const auto& l:layouts) {
            printf("\n%s (%s): %s\n",l.format->title,l.format->id,l.directory.errors.empty()?"valid directory":"invalid directory");
            for(size_t i=0;i<l.directory.lumps.size();++i)
                printf("  %2u %-26s %10d bytes at %10d\n",unsigned(i),l.format->lumps[i].name,l.directory.lumps[i].length,l.directory.lumps[i].offset);
            for(const auto& e:l.directory.errors) printf("  ERROR: %s\n",e.c_str());
        }
        for(const auto& n:notes) printf("Note: %s\n",n.c_str());
        for(const auto& e:errors) fprintf(stderr,"ERROR: %s\n",e.c_str());
    }
    return valid ? 0 : 1;
}
