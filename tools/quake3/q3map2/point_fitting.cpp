// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "point_fitting.h"
#include <atomic>
#include <limits>
#include <stdexcept>

namespace q3mapx {
namespace {
constexpr double infinity=std::numeric_limits<double>::infinity();
bool finite(const Vector3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
bool encodable(const Vector3& v) {
    return finite(v) && std::all_of(v.data(),v.data()+3,[](float f){ return f>=0 && f<256; });
}
struct Candidate {
    light_t source{};
    Vector3 energy{0};
    double cost=infinity;
    bool valid=false;
};
struct Engine {
    const PointFitOptions& options;
    PointFitResult result;
    std::vector<Vector3> base;
    std::vector<Candidate> grid;
    std::atomic<uint64_t> work{0},positions{0},unknown{0},invalidEncoding{0};
    std::atomic<size_t> next{0};
    static Engine* active;
    explicit Engine(const PointFitOptions& settings):options(settings){}

    void spend(uint64_t amount) {
        const uint64_t before=work.fetch_add(amount);
        if(before>options.maxWork || amount>options.maxWork-before)
            throw std::runtime_error("Point fitting work budget exceeded; no report was published");
    }
    light_t light(const Candidate& candidate,const Vector3& energy) const {
        light_t source=candidate.source;
        const float intensity=vector3_max_component(energy);
        source.photons=intensity*pointScale;
        source.color=intensity>0?energy/intensity:Vector3(0);
        // For the supported inverse-square point family, only this part of the
        // prepared native envelope depends on intensity. PVS bounds do not.
        if(source.flags&LightFlags::FastActual)
            source.envelope=std::min(source.envelope,std::sqrt(source.photons/source.falloffTolerance));
        source.envelope2=source.envelope*source.envelope;
        return source;
    }
    bool trace(const PointFitReceiver& receiver,const light_t& source,Vector3& color,bool* subsampling=nullptr) const {
        color.set(0);
        if(subsampling) *subsampling=false;
        if(source.photons<=0) return true;
        trace_t trace{};
        const auto& info=surfaceInfos[receiver.surface];
        trace.testOcclusion=!noTrace; trace.forceSunlight=info.si->forceSunlight;
        trace.recvShadows=info.recvShadows; trace.numSurfaces=1;
        int surface=receiver.surface; trace.surfaces=&surface;
        trace.inhibitRadius=DEFAULT_INHIBIT_RADIUS; trace.twoSided=info.si->twoSided;
        trace.cluster=receiver.cluster; trace.origin=receiver.origin; trace.normal=receiver.normal;
        trace.light=&source;
        LightContributionToSample(&trace);
        color=trace.color;
        if(subsampling) *subsampling=trace.forceSubsampling!=0;
        return trace.numTestNodes<MAX_TRACE_TEST_NODES && finite(color);
    }
    Vector3 encode(size_t index,const Vector3& addition) const {
        const auto& r=result.receivers[index];
        Vector3 color=base[index]+addition;
        if(r.slot==0) for(int a=0;a<3;++a) color[a]=std::max(color[a],minLight[a]);
        return ColorToFloat(color,1,r.brightness);
    }
    double cost(const Candidate& candidate,const Vector3& energy) {
        spend(result.baseline[0].samples);
        const auto source=light(candidate,energy);
        double squared=0;
        for(size_t i=0;i<result.receivers.size();++i) {
            const auto& r=result.receivers[i]; if(r.withheld) continue;
            Vector3 addition(0);
            if(!trace(r,source,addition)) { ++unknown; return infinity; }
            const Vector3 encoded=encode(i,addition);
            if(!encodable(encoded)) { ++invalidEncoding; return infinity; }
            // Native byte conversion truncates. Its bin centre gives a smooth
            // training objective; the acceptance scores use actual bytes.
            for(int a=0;a<3;++a) {
                const double target=r.observed[a]+(lightmapsRGB?0.0:0.5);
                const double d=encoded[a]-target; squared+=d*d;
            }
        }
        return squared/(3*result.baseline[0].samples);
    }
    Candidate prepare(const Vector3& origin) {
        spend(bspLeafs.size()+1); ++positions;
        Candidate candidate;
        if(!finite(origin) || ClusterForPointExt(origin,0.125f)<0) return candidate;
        for(int a=0;a<3;++a) if(origin[a]<options.mins[a] || origin[a]>options.maxs[a]) return candidate;
        auto& source=candidate.source;
        source.type=ELightType::Point; source.flags=LightFlags::DefaultQ3A;
        source.origin=origin; source.photons=options.maxIntensity*pointScale;
        source.color.set(1); source.fade=1; source.extraDist=extraDist;
        source.style=int(options.style); source.falloffTolerance=falloffTolerance;
        candidate.valid=SetupLightEnvelope(source,false,fast);
        return candidate;
    }
    Candidate optimize(Candidate candidate,const Vector3& initial=Vector3(0)) {
        if(!candidate.valid) return candidate;
        candidate.energy=initial; candidate.cost=cost(candidate,candidate.energy);
        // Coarse bounded brackets prevent an arbitrary starting color from
        // trapping a channel at a clipped/normalized end of the transfer.
        for(int sweep=0;sweep<2;++sweep) for(int axis=0;axis<3;++axis) {
            auto evaluate=[&](float value) {
                Vector3 energy=candidate.energy; energy[axis]=value;
                const double score=cost(candidate,energy);
                if(score<candidate.cost) { candidate.energy=energy; candidate.cost=score; }
                return score;
            };
            float best=candidate.energy[axis]; double bestScore=candidate.cost;
            for(int step=0;step<=4;++step) {
                const float value=options.maxIntensity*(float(step)/4);
                const double score=evaluate(value);
                if(score<bestScore) { best=value; bestScore=score; }
            }
            const float radius=options.maxIntensity/4;
            float lo=std::max(0.f,best-radius),hi=std::min(options.maxIntensity,best+radius);
            constexpr float ratio=0.61803398875f;
            float x=hi-ratio*(hi-lo),y=lo+ratio*(hi-lo);
            double fx=evaluate(x),fy=evaluate(y);
            for(int step=0;step<14;++step) {
                if(fx<fy) { hi=y; y=x; fy=fx; x=hi-ratio*(hi-lo); fx=evaluate(x); }
                else { lo=x; x=y; fx=fy; y=lo+ratio*(hi-lo); fy=evaluate(y); }
            }
        }
        return candidate;
    }
    Candidate refine(Candidate candidate) {
        float step=options.spacing/2;
        for(unsigned level=0;level<options.refinementSteps;++level,step/=2) {
            for(int pass=0;pass<2;++pass) {
                Candidate best=candidate;
                for(int axis=0;axis<3;++axis) for(int sign:{-1,1}) {
                    Vector3 origin=candidate.source.origin; origin[axis]+=sign*step;
                    auto trial=optimize(prepare(origin),candidate.energy);
                    if(trial.cost<best.cost) best=std::move(trial);
                }
                if(!(best.cost<candidate.cost)) break;
                candidate=std::move(best);
            }
        }
        return candidate;
    }
    static void worker(int) {
        for(size_t i=active->next.fetch_add(1);i<active->grid.size();i=active->next.fetch_add(1))
            active->grid[i]=active->optimize(active->grid[i]);
    }
    bool contributions(const Candidate& candidate,std::vector<Vector3>& output,bool includeWithheld) {
        const auto source=light(candidate,candidate.energy);
        spend(includeWithheld?result.receivers.size():result.baseline[0].samples);
        output.assign(result.receivers.size(),Vector3(0));
        for(size_t i=0;i<output.size();++i) if(includeWithheld || !result.receivers[i].withheld) {
            bool subsampling=false;
            if(!trace(result.receivers[i],source,output[i],&subsampling)) { ++unknown; return false; }
            if(includeWithheld && subsampling) result.subsampling[i]=1;
        }
        return true;
    }
    std::array<PointFitMetrics,2> metrics(bool save,bool includeWithheld) {
        std::array<PointFitMetrics,2> totals{};
        if(save) result.prediction.resize(result.receivers.size());
        spend(includeWithheld?result.receivers.size():result.baseline[0].samples);
        for(size_t i=0;i<result.receivers.size();++i) {
            const auto& r=result.receivers[i]; if(r.withheld && !includeWithheld) continue;
            const auto encoded=encode(i,Vector3(0));
            if(!encodable(encoded)) throw std::runtime_error("Unrepresentable point fitting prediction");
            const Vector3b predicted=encoded;
            if(save) result.prediction[i]=predicted;
            auto& total=totals[r.withheld]; ++total.samples;
            for(int a=0;a<3;++a) {
                const double error=double(predicted[a])-r.observed[a];
                total.mae+=std::abs(error); total.rmse+=error*error;
                total.maximum=std::max(total.maximum,std::abs(error));
            }
        }
        for(auto& total:totals) if(total.samples) {
            total.mae/=3*total.samples; total.rmse=std::sqrt(total.rmse/(3*total.samples));
        }
        return totals;
    }
    void run() {
        if(!result.receivers.empty()) result.baseline=metrics(false,true);
        result.trial=result.baseline;
        if(result.baseline[0].samples<24 || result.baseline[1].samples<12) {
            result.status="insufficient_observations"; return;
        }
        // Withheld values are never consulted for candidate selection, stopping,
        // position/color refinement or the number of fitted lights.
        if(result.baseline[0].rmse<=options.minImprovement) {
            result.status=result.baseline[0].rmse<=options.maxRMSE && result.baseline[1].rmse<=options.maxRMSE
                ?"baseline_explains_observations":"baseline_below_search_threshold_but_unqualified"; return;
        }
        std::array<unsigned,3> cells{};
        uint64_t count=1;
        for(int a=0;a<3;++a) {
            const double n=std::ceil((double(options.maxs[a])-options.mins[a])/options.spacing);
            if(!std::isfinite(n) || n<1 || n>options.maxCandidates || count>options.maxCandidates/uint64_t(n))
                throw std::runtime_error("Point fitting candidate grid exceeds limit; increase grid_spacing or restrict bounds");
            cells[a]=unsigned(n); count*=cells[a];
        }
        result.gridPoints=count; grid.reserve(count);
        for(unsigned z=0;z<cells[2];++z) for(unsigned y=0;y<cells[1];++y) for(unsigned x=0;x<cells[0];++x) {
            Vector3 origin;
            const std::array<unsigned,3> index{x,y,z};
            for(int a=0;a<3;++a) origin[a]=float(double(options.mins[a])+(index[a]+.5)*(double(options.maxs[a])-options.mins[a])/cells[a]);
            auto candidate=prepare(origin);
            if(candidate.valid) { ++result.usableCandidates; grid.push_back(std::move(candidate)); }
        }
        if(grid.empty()) { result.status="no_usable_candidates"; return; }
        const auto original=base;
        std::vector<Candidate> selected;
        std::vector<std::vector<Vector3>> responses;
        double previous=result.baseline[0].rmse;
        for(unsigned round=0;round<options.maxLights;++round) {
            next=0; active=this;
            struct Reset { ~Reset(){ Engine::active=nullptr; } } reset;
            RunThreadsOnIndividual(int(std::min({grid.size(),size_t(std::max(numthreads,1)),size_t(32)})),true,worker,"FitPointCandidates",1);
            auto best=std::min_element(grid.begin(),grid.end(),[](const auto& a,const auto& b){ return a.cost<b.cost; });
            if(best==grid.end() || !std::isfinite(best->cost)) break;
            if(round==0) {
                for(const auto& c:grid) if(std::isfinite(c.cost))
                    result.trainingAlternatives.push_back({{c.source.origin,c.energy},std::sqrt(c.cost)});
                std::stable_sort(result.trainingAlternatives.begin(),result.trainingAlternatives.end(),
                    [](const auto& a,const auto& b){ return a.trainingRMSE<b.trainingRMSE; });
                if(result.trainingAlternatives.size()>8) result.trainingAlternatives.resize(8);
            }
            Candidate candidate=refine(*best);
            std::vector<Vector3> contribution;
            if(!contributions(candidate,contribution,false)) break;
            const auto before=base;
            for(size_t i=0;i<base.size();++i) base[i]+=contribution[i];
            const auto score=metrics(false,false);
            if(previous-score[0].rmse<options.minImprovement) { base=before; break; }
            selected.push_back(std::move(candidate)); responses.push_back(std::move(contribution));
            // Refit existing lights with all other selected lights held fixed.
            // This reduces greedy position/color bias in overlapping sources.
            if(selected.size()>1) for(int pass=0;pass<2;++pass) for(size_t light=0;light<selected.size();++light) {
                base=original;
                for(size_t other=0;other<selected.size();++other) if(other!=light)
                    for(size_t i=0;i<base.size();++i) base[i]+=responses[other][i];
                auto trial=refine(optimize(selected[light],selected[light].energy));
                if(!contributions(trial,responses[light],false)) throw std::runtime_error("Unknown trace while refining point fit");
                selected[light]=std::move(trial);
            }
            base=original;
            for(const auto& colors:responses) for(size_t i=0;i<base.size();++i) base[i]+=colors[i];
            previous=metrics(false,false)[0].rmse;
            if(previous<=options.minImprovement) break;
        }
        // First evaluate candidate transport at the withheld receivers here.
        base=original;
        result.subsampling.resize(result.receivers.size());
        for(const auto& candidate:selected) {
            if(vector3_max_component(candidate.energy)<=0) continue;
            result.lights.push_back({candidate.source.origin,candidate.energy});
            std::vector<Vector3> colors;
            if(!contributions(candidate,colors,true)) {
                result.status="unknown_validation_trace"; result.trial={}; return;
            }
            for(size_t i=0;i<base.size();++i) base[i]+=colors[i];
        }
        result.trial=metrics(true,true);
        result.accepted=!result.lights.empty();
        for(size_t split=0;split<2;++split)
            result.accepted&=result.baseline[split].rmse-result.trial[split].rmse>=options.minImprovement
                && result.trial[split].rmse<=options.maxRMSE;
        result.status=result.accepted?"conditional_point_proposal":selected.empty()?"no_supported_improvement":"validation_rejected";
    }
};
Engine* Engine::active=nullptr;
}

PointFitResult fitPointLights(const PointFitOptions& options,std::vector<PointFitReceiver> receivers) {
    if(!std::isfinite(pointScale) || pointScale<=0 || !std::isfinite(extraDist) || extraDist<0
        || !std::isfinite(falloffTolerance) || falloffTolerance<=0)
        throw std::runtime_error("Point fitting requires positive point scale/falloff and nonnegative extra distance");
    Engine engine{options};
    engine.result.receivers=std::move(receivers);
    for(auto& receiver:engine.result.receivers) {
        uint64_t hash=14695981039346656037ull;
        for(uint64_t value:{uint64_t(receiver.page),uint64_t(receiver.x)/options.blockSize,uint64_t(receiver.y)/options.blockSize}) {
            hash^=value; hash*=1099511628211ull;
        }
        receiver.withheld=hash%5==0;
        ++engine.result.baseline[receiver.withheld].samples;
        engine.base.push_back(receiver.baseline);
    }
    engine.run();
    engine.result.work=engine.work; engine.result.positionsTested=engine.positions;
    engine.result.unknownCandidates=engine.unknown; engine.result.encodingFailures=engine.invalidEncoding;
    return std::move(engine.result);
}
}
