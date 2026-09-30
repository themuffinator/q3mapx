// SPDX-License-Identifier: GPL-3.0-or-later
#include "portal_graph.h"
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace q3mapx {
namespace {
class Reader {
    std::string_view text_;
    size_t position_ = 0;
public:
    explicit Reader(std::string_view text) : text_(text) {}
    [[noreturn]] void fail(const char* field) const {
        throw std::runtime_error("Invalid PRT1 " + std::string(field) + " at byte " + std::to_string(position_));
    }
    void spaces() {
        while(position_ < text_.size()) {
            const char c = text_[position_];
            if(c!=' ' && c!='\t' && c!='\r' && c!='\n' && c!='\f' && c!='\v') break;
            ++position_;
        }
    }
    std::string_view token() {
        spaces();
        const size_t start = position_;
        while(position_ < text_.size()) {
            const char c = text_[position_];
            if(c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f' || c=='\v' || c=='(' || c==')') break;
            ++position_;
        }
        return text_.substr(start,position_-start);
    }
    template<typename T> T number(const char* field) {
        auto value = token();
        if(!value.empty() && value.front()=='+') {
            value.remove_prefix(1);
            if(!value.empty() && (value.front()=='+' || value.front()=='-')) fail(field);
        }
        T result{};
        if(value.empty()) fail(field);
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) fail(field);
        return result;
    }
    int integer(const char* field, int low, int high) {
        const int value=number<int>(field);
        if(value<low || value>high) fail(field);
        return value;
    }
    void punctuation(char expected) {
        spaces();
        if(position_==text_.size() || text_[position_++]!=expected) fail("point parentheses");
    }
    bool done() { spaces(); return position_==text_.size(); }
};
}

PortalGraph parsePortalGraph(std::string_view text, const PortalLimits& limits) {
    if(text.size()>limits.bytes) throw std::runtime_error("PRT1 input exceeds byte limit");
    if(limits.clusters<1 || limits.portals<0 || limits.faces<0 || limits.pointsPerWinding<3 || limits.windingsPerCluster<1)
        throw std::invalid_argument("Invalid PRT1 parser limits");
    Reader reader(text);
    if(reader.token()!="PRT1") reader.fail("signature");
    PortalGraph graph;
    graph.clusters=reader.integer("cluster count",1,limits.clusters);
    const int portals=reader.integer("portal count",0,limits.portals);
    const int faces=reader.integer("face count",0,limits.faces);
    graph.portals.reserve(portals); graph.faces.reserve(faces);
    std::vector<int> degrees(graph.clusters), faceCounts(graph.clusters);
    for(bool face : {false,true}) {
        auto& polygons=face?graph.faces:graph.portals;
        for(int i=0; i<(face?faces:portals); ++i) {
            PortalPolygon polygon;
            const int points=reader.integer("winding point count",3,limits.pointsPerWinding);
            if(size_t(points)>limits.points-graph.pointCount) reader.fail("total point limit");
            graph.pointCount+=size_t(points);
            polygon.front=reader.integer("front cluster",0,graph.clusters-1);
            if(!face) {
                polygon.back=reader.integer("back cluster",0,graph.clusters-1);
                if(polygon.front==polygon.back) throw std::runtime_error("PRT1 portal " + std::to_string(i) + " connects a leaf to itself");
                polygon.flags=reader.integer("portal flags",0,INT_MAX);
                if(++degrees[polygon.front]>limits.windingsPerCluster || ++degrees[polygon.back]>limits.windingsPerCluster)
                    reader.fail("portals per cluster limit");
            }
            else if(++faceCounts[polygon.front]>limits.windingsPerCluster) reader.fail("faces per cluster limit");
            polygon.points.resize(points);
            for(auto& point : polygon.points) {
                reader.punctuation('(');
                for(float& coordinate : point) {
                    coordinate=reader.number<float>("point coordinate");
                    if(!std::isfinite(coordinate)) reader.fail("finite point coordinate");
                }
                reader.punctuation(')');
            }
            polygons.push_back(std::move(polygon));
        }
    }
    if(!reader.done()) reader.fail("trailing data");
    return graph;
}

PortalGraph readPortalGraph(const std::filesystem::path& path, const PortalLimits& limits) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file || file.tellg()<0) throw std::runtime_error("Cannot open PRT1 input " + path.string());
    const auto size=uint64_t(file.tellg());
    if(size>limits.bytes) throw std::runtime_error("PRT1 input exceeds byte limit");
    std::string text(size_t(size),'\0');
    file.seekg(0);
    if(!file.read(text.data(),std::streamsize(text.size())) || file.peek()!=std::char_traits<char>::eof() || file.bad())
        throw std::runtime_error("PRT1 input changed or could not be read completely");
    return parsePortalGraph(text,limits);
}
}
