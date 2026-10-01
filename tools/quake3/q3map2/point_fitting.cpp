// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "point_fitting.h"
#include <atomic>
#include <limits>
#include <numeric>
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
    Vector3 aim{0};
    int target=-1;
    double cost=infinity;
    bool valid=false;
};
struct Engine {
    const PointFitOptions& options;
    PointFitResult result;
    std::vector<Vector3> base;
    std::vector<Candidate> grid;
    std::vector<Vector3> gridPositions;
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
        source.photons=intensity*(options.spot?spotScale:pointScale);
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
    Vector3 encode(size_t index,const Vector3& addition,bool continuous=false) const {
        const auto& r=result.receivers[index];
        Vector3 color=base[index]+addition;
        if(r.slot==0) for(int a=0;a<3;++a) color[a]=std::max(color[a],minLight[a]);
        return ColorToFloat(color,1,r.brightness,!continuous);
    }
    double cost(const Candidate& candidate,const Vector3& energy,std::vector<double>* residuals=nullptr) {
        spend(result.baseline[0].samples);
        if(residuals) { residuals->clear(); residuals->reserve(3*result.baseline[0].samples); }
        const auto source=light(candidate,energy);
        double squared=0;
        for(size_t i=0;i<result.receivers.size();++i) {
            const auto& r=result.receivers[i]; if(r.withheld) continue;
            Vector3 addition(0);
            if(!trace(r,source,addition)) { ++unknown; return infinity; }
            const Vector3 encoded=encode(i,addition,options.spot);
            if(!encodable(encoded)) { ++invalidEncoding; return infinity; }
            // Native byte conversion truncates. Its bin centre gives a smooth
            // training objective; the acceptance scores use actual bytes.
            for(int a=0;a<3;++a) {
                const double target=r.observed[a]+(lightmapsRGB?0.0:0.5);
                const double d=encoded[a]-target; squared+=d*d;
                if(residuals) residuals->push_back(d);
            }
        }
        return squared/(3*result.baseline[0].samples);
    }
    Candidate prepare(const Vector3& origin,const Vector3& aim=Vector3(0),float slope=0,int target=-1) {
        spend(bspLeafs.size()+1); ++positions;
        Candidate candidate;
        if(!finite(origin) || ClusterForPointExt(origin,0.125f)<0) return candidate;
        for(int a=0;a<3;++a) if(origin[a]<options.mins[a] || origin[a]>options.maxs[a]) return candidate;
        auto& source=candidate.source;
        source.type=options.spot?ELightType::Spot:ELightType::Point; source.flags=LightFlags::DefaultQ3A;
        source.origin=origin; source.photons=options.maxIntensity*(options.spot?spotScale:pointScale);
        source.color.set(1); source.fade=1; source.extraDist=extraDist;
        source.style=int(options.style); source.falloffTolerance=falloffTolerance;
        if(options.spot) {
            candidate.aim=aim; candidate.target=target;
            source.normal=aim-origin;
            const float distance=VectorNormalize(source.normal);
            if(!finite(source.normal) || distance<1 || !std::isfinite(slope) || slope<=0) return candidate;
            // A retained target must be representable by a positive native radius.
            if(target>=0 && (slope*distance<=16.01f || slope*distance>1'000'016.f)) return candidate;
            source.radiusByDist=slope;
        }
        candidate.valid=SetupLightEnvelope(source,false,fast);
        return candidate;
    }
    struct Solution { std::vector<Candidate> lights; double cost=infinity; };
    Solution polishSources(const std::vector<Candidate>& start) {
        struct Frame {
            size_t offset,cone,energy;
            Vector3 normal,tangent,bitangent;
            float distance;
        };
        std::vector<Frame> frames;
        std::vector<double> parameters,steps;
        for(const auto& candidate:start) {
            const size_t offset=parameters.size(),cone=offset+(candidate.target>=0?3:5),energy=cone+1;
            parameters.resize(energy+3); steps.resize(energy+3,.001);
            for(int a=0;a<3;++a) { parameters[energy+a]=candidate.energy[a]/options.maxIntensity; steps[energy+a]=.0001; }
            const auto& normal=candidate.source.normal;
            int axis=0; for(int a=1;a<3;++a) if(std::abs(normal[a])<std::abs(normal[axis])) axis=a;
            Vector3 helper(0); helper[axis]=1;
            Vector3 tangent=vector3_cross(normal,helper); VectorNormalize(tangent);
            frames.push_back({offset,cone,energy,normal,tangent,vector3_cross(normal,tangent),float(vector3_length(candidate.aim-candidate.source.origin))});
        }
        const size_t count=parameters.size();
        const auto project=[&](std::vector<double>& p) {
            for(size_t light=0;light<start.size();++light) {
                const auto& frame=frames[light]; const auto& initial=start[light];
                for(int a=0;a<3;++a) {
                    p[frame.offset+a]=std::clamp(p[frame.offset+a],double(options.mins[a]-initial.source.origin[a])/options.spacing,
                        double(options.maxs[a]-initial.source.origin[a])/options.spacing);
                    p[frame.energy+a]=std::clamp(p[frame.energy+a],0.,1.);
                }
                p[frame.cone]=std::clamp(p[frame.cone],std::log(std::tan(degrees_to_radians(options.minHalfAngle))/initial.source.radiusByDist),
                    std::log(std::tan(degrees_to_radians(options.maxHalfAngle))/initial.source.radiusByDist));
                if(initial.target<0) for(size_t a=3;a<5;++a) p[frame.offset+a]=std::clamp(p[frame.offset+a],-4.,4.);
            }
        };
        const auto evaluate=[&](const std::vector<double>& p,std::vector<double>& residuals) {
            Solution solution;
            std::vector<light_t> sources;
            for(size_t light=0;light<start.size();++light) {
                const auto& initial=start[light]; const auto& frame=frames[light];
                Vector3 origin;
                for(int a=0;a<3;++a) origin[a]=float(initial.source.origin[a]+p[frame.offset+a]*options.spacing);
                Vector3 aim=initial.aim;
                if(initial.target<0) {
                    Vector3 direction=frame.normal+frame.tangent*float(p[frame.offset+3])+frame.bitangent*float(p[frame.offset+4]);
                    VectorNormalize(direction); aim=origin+direction*frame.distance;
                }
                auto candidate=prepare(origin,aim,float(initial.source.radiusByDist*std::exp(p[frame.cone])),initial.target);
                if(!candidate.valid) return solution;
                for(int a=0;a<3;++a) candidate.energy[a]=float(p[frame.energy+a]*options.maxIntensity);
                sources.push_back(this->light(candidate,candidate.energy)); solution.lights.push_back(std::move(candidate));
            }
            spend(result.baseline[0].samples*(sources.size()+1));
            residuals.clear(); residuals.reserve(result.baseline[0].samples*3);
            double squared=0;
            for(size_t i=0;i<result.receivers.size();++i) {
                const auto& receiver=result.receivers[i]; if(receiver.withheld) continue;
                Vector3 addition(0);
                for(const auto& source:sources) {
                    Vector3 color(0);
                    if(!trace(receiver,source,color)) { ++unknown; return solution; }
                    addition+=color;
                }
                const auto encoded=encode(i,addition,true);
                if(!encodable(encoded)) { ++invalidEncoding; return solution; }
                for(int a=0;a<3;++a) {
                    const double error=encoded[a]-receiver.observed[a]-(lightmapsRGB?0.:.5);
                    residuals.push_back(error); squared+=error*error;
                }
            }
            solution.cost=squared/(3*result.baseline[0].samples);
            for(auto& candidate:solution.lights) candidate.cost=solution.cost;
            return solution;
        };
        project(parameters);
        std::vector<double> residuals;
        Solution best=evaluate(parameters,residuals);
        if(!std::isfinite(best.cost)) return best;
        double damping=.01;
        for(unsigned iteration=0;iteration<options.refinementSteps*10;++iteration) {
            std::vector<std::vector<double>> jacobian(count);
            for(size_t variable=0;variable<count;++variable) {
                auto plus=parameters,minus=parameters; plus[variable]+=steps[variable]; minus[variable]-=steps[variable];
                project(plus); project(minus);
                std::vector<double> high,low;
                const auto a=evaluate(plus,high),b=evaluate(minus,low);
                jacobian[variable].resize(residuals.size());
                const bool highValid=std::isfinite(a.cost),lowValid=std::isfinite(b.cost);
                const double span=plus[variable]-minus[variable];
                for(size_t i=0;i<residuals.size();++i) {
                    if(highValid && lowValid && span>0) jacobian[variable][i]=(high[i]-low[i])/span;
                    else if(highValid && plus[variable]>parameters[variable]) jacobian[variable][i]=(high[i]-residuals[i])/(plus[variable]-parameters[variable]);
                    else if(lowValid && minus[variable]<parameters[variable]) jacobian[variable][i]=(residuals[i]-low[i])/(parameters[variable]-minus[variable]);
                }
            }
            spend(result.baseline[0].samples*count*(count+1));
            std::vector<std::vector<double>> matrix(count,std::vector<double>(count+1));
            for(size_t row=0;row<count;++row) {
                for(size_t col=0;col<count;++col)
                    for(size_t i=0;i<residuals.size();++i) matrix[row][col]+=jacobian[row][i]*jacobian[col][i];
                for(size_t i=0;i<residuals.size();++i) matrix[row][count]-=jacobian[row][i]*residuals[i];
                matrix[row][row]+=damping*std::max(1.,matrix[row][row]);
            }
            bool valid=true;
            for(size_t col=0;col<count;++col) {
                size_t pivot=col;
                for(size_t row=col+1;row<count;++row) if(std::abs(matrix[row][col])>std::abs(matrix[pivot][col])) pivot=row;
                if(std::abs(matrix[pivot][col])<1e-20) { valid=false; break; }
                std::swap(matrix[col],matrix[pivot]);
                const double divisor=matrix[col][col];
                for(size_t k=col;k<=count;++k) matrix[col][k]/=divisor;
                for(size_t row=0;row<count;++row) if(row!=col) {
                    const double factor=matrix[row][col];
                    for(size_t k=col;k<=count;++k) matrix[row][k]-=factor*matrix[col][k];
                }
            }
            if(!valid) break;
            auto changed=parameters;
            double largest=0;
            for(size_t variable=0;variable<count;++variable) {
                const double delta=matrix[variable][count];
                if(!std::isfinite(delta)) { valid=false; break; }
                largest=std::max(largest,std::abs(delta));
            }
            if(!valid || largest<1e-7) break;
            const double scale=std::min(1.,.25/largest);
            for(size_t variable=0;variable<count;++variable) changed[variable]+=matrix[variable][count]*scale;
            project(changed);
            std::vector<double> trialResiduals;
            auto trial=evaluate(changed,trialResiduals);
            if(trial.cost<best.cost) {
                best=std::move(trial); parameters=std::move(changed); residuals=std::move(trialResiduals);
                damping=std::max(1e-8,damping/3);
            }
            else { damping*=10; if(damping>1e8) break; }
        }
        return best;
    }
    Candidate polish(Candidate start) {
        auto solution=polishSources({start});
        return solution.cost<start.cost?std::move(solution.lights[0]):start;
    }
    Candidate matchTarget(Candidate candidate) {
        if(candidate.target>=0) return candidate;
        Candidate best;
        for(size_t index=0;index<options.targets.size();++index) {
            Vector3 direction=options.targets[index].origin-candidate.source.origin;
            if(VectorNormalize(direction)<1 || vector3_dot(direction,candidate.source.normal)<std::cos(degrees_to_radians(5.))) continue;
            auto trial=prepare(candidate.source.origin,options.targets[index].origin,candidate.source.radiusByDist,int(index));
            if(!trial.valid) continue;
            trial=polish(optimize(std::move(trial),candidate.energy));
            if(trial.cost<best.cost) best=std::move(trial);
        }
        // Prefer one existing static marker when its training explanation differs
        // by no more than 0.01 squared byte units. Withheld qualification is later.
        return std::isfinite(best.cost) && best.cost<=candidate.cost+0.01?best:candidate;
    }
    Candidate optimize(Candidate candidate,const Vector3& initial=Vector3(0),unsigned sweeps=2,unsigned iterations=14) {
        if(!candidate.valid) return candidate;
        candidate.energy=initial; candidate.cost=cost(candidate,candidate.energy);
        // Coarse bounded brackets prevent an arbitrary starting color from
        // trapping a channel at a clipped/normalized end of the transfer.
        for(unsigned sweep=0;sweep<sweeps;++sweep) for(int axis=0;axis<3;++axis) {
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
            for(unsigned step=0;step<iterations;++step) {
                if(fx<fy) { hi=y; y=x; fy=fx; x=hi-ratio*(hi-lo); fx=evaluate(x); }
                else { lo=x; x=y; fx=fy; y=lo+ratio*(hi-lo); fy=evaluate(y); }
            }
        }
        return candidate;
    }
    Candidate refine(Candidate candidate) {
        float step=options.spacing/2;
        float angularStep=degrees_to_radians(20.f);
        for(unsigned level=0;level<options.refinementSteps;++level,step/=2) {
            for(int pass=0;pass<(options.spot?4:2);++pass) {
                Candidate best=candidate;
                for(int axis=0;axis<3;++axis) for(int sign:{-1,1}) {
                    Vector3 origin=candidate.source.origin; origin[axis]+=sign*step;
                    auto trial=optimize(prepare(origin,candidate.aim,candidate.source.radiusByDist,candidate.target),candidate.energy);
                    if(trial.cost<best.cost) best=std::move(trial);
                }
                if(options.spot) {
                    const auto consider=[&](const Vector3& aim,float slope) {
                        auto trial=optimize(prepare(candidate.source.origin,aim,slope,candidate.target),candidate.energy);
                        if(trial.cost<best.cost) best=std::move(trial);
                    };
                    if(candidate.target<0) {
                        const auto& normal=candidate.source.normal;
                        int axis=0;
                        for(int a=1;a<3;++a) if(std::abs(normal[a])<std::abs(normal[axis])) axis=a;
                        Vector3 helper(0); helper[axis]=1;
                        Vector3 tangent=vector3_cross(normal,helper); VectorNormalize(tangent);
                        const Vector3 bitangent=vector3_cross(normal,tangent);
                        const float distance=vector3_length(candidate.aim-candidate.source.origin);
                        for(const auto& basis:{tangent,bitangent}) for(int sign:{-1,1}) {
                            const Vector3 direction=normal*std::cos(angularStep)+basis*(sign*std::sin(angularStep));
                            consider(candidate.source.origin+direction*distance,candidate.source.radiusByDist);
                        }
                    }
                    const float angle=std::atan(candidate.source.radiusByDist);
                    for(int sign:{-1,1}) {
                        const float changed=std::clamp<float>(angle+sign*angularStep,degrees_to_radians(options.minHalfAngle),degrees_to_radians(options.maxHalfAngle));
                        consider(candidate.aim,std::tan(changed));
                    }
                }
                if(!(best.cost<candidate.cost)) break;
                candidate=std::move(best);
            }
            angularStep/=2;
        }
        return candidate;
    }
    static void worker(int) {
        for(size_t i=active->next.fetch_add(1);i<active->grid.size();i=active->next.fetch_add(1))
            active->grid[i]=active->options.spot?active->optimize(active->grid[i],Vector3(0),1,10):active->optimize(active->grid[i]);
    }
    PointFitLight description(const Candidate& c) const {
        return {c.source.origin,c.energy,c.source.normal,c.source.radiusByDist,c.target};
    }
    void seedSpots() {
        struct Accumulator {
            std::array<double,3> sum{};
            double weight=0;
            void add(const Vector3& p,double w) { weight+=w; for(int a=0;a<3;++a) sum[a]+=p[a]*w; }
            Vector3 mean() const { return Vector3(float(sum[0]/weight),float(sum[1]/weight),float(sum[2]/weight)); }
        } total;
        std::array<Accumulator,3> channels;
        struct WeightedPoint { Vector3 point; double weight; };
        std::vector<WeightedPoint> points;
        std::map<int,Accumulator> surfaces;
        spend(result.baseline[0].samples);
        for(size_t i=0;i<result.receivers.size();++i) {
            const auto& r=result.receivers[i]; if(r.withheld) continue;
            const auto predicted=encode(i,Vector3(0));
            double weight=0;
            for(int a=0;a<3;++a) {
                const double channel=std::max(0.,double(r.observed[a])-predicted[a]);
                weight+=channel; if(channel>1) channels[a].add(r.origin,channel);
            }
            if(weight<=1) continue;
            points.push_back({r.origin,weight}); total.add(r.origin,weight); surfaces[r.surface].add(r.origin,weight);
        }
        std::vector<Vector3> seeds;
        if(total.weight>0) {
            seeds.push_back(total.mean());
            // Spatial residual clusters seed separated lobes even on one draw surface.
            std::vector<Vector3> centers;
            for(size_t k=0;k<std::min(size_t(4),points.size());++k) {
                spend(points.size()*std::max(size_t(1),centers.size()));
                const WeightedPoint* best=nullptr; double maximum=-1;
                for(const auto& p:points) {
                    double distance=centers.empty()?1:infinity;
                    for(const auto& c:centers) distance=std::min(distance,double(vector3_length_squared(p.point-c)));
                    const double score=distance*p.weight;
                    if(score>maximum) { best=&p; maximum=score; }
                }
                centers.push_back(best->point);
            }
            for(int iteration=0;iteration<8;++iteration) {
                spend(points.size()*centers.size());
                std::vector<Accumulator> groups(centers.size());
                for(const auto& p:points) {
                    size_t nearest=0;
                    for(size_t k=1;k<centers.size();++k)
                        if(vector3_length_squared(p.point-centers[k])<vector3_length_squared(p.point-centers[nearest])) nearest=k;
                    groups[nearest].add(p.point,p.weight);
                }
                for(size_t k=0;k<centers.size();++k) if(groups[k].weight>0) centers[k]=groups[k].mean();
            }
            const auto append=[&](const Vector3& p) {
                if(seeds.size()<8 && std::none_of(seeds.begin(),seeds.end(),[&](const auto& s){ return vector3_length_squared(p-s)<64; })) seeds.push_back(p);
            };
            for(const auto& channel:channels) if(channel.weight>0) append(channel.mean());
            for(const auto& c:centers) append(c);
            std::vector<Accumulator> ordered;
            for(const auto& entry:surfaces) ordered.push_back(entry.second);
            std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){ return a.weight>b.weight; });
            for(const auto& surface:ordered) append(surface.mean());
        }
        result.residualSeeds+=seeds.size();
        const uint64_t variants=uint64_t(gridPositions.size())*(seeds.size()+options.targets.size())*5;
        if(variants>options.maxSpotCandidates) throw std::runtime_error("Spot fitting candidate limit exceeded; restrict bounds, increase grid_spacing or max_spot_candidates");
        grid.clear(); grid.reserve(size_t(variants));
        for(const auto& origin:gridPositions) {
            const auto append=[&](const Vector3& aim,int target) {
                for(int cone=0;cone<5;++cone) {
                    const float angle=options.minHalfAngle+(options.maxHalfAngle-options.minHalfAngle)*(cone/4.f);
                    auto candidate=prepare(origin,aim,std::tan(degrees_to_radians(angle)),target);
                    if(candidate.valid) grid.push_back(std::move(candidate));
                }
            };
            for(const auto& aim:seeds) append(aim,-1);
            for(size_t target=0;target<options.targets.size();++target) append(options.targets[target].origin,int(target));
        }
        result.spotCandidates+=grid.size();
    }
    Candidate refineSpotGrid() {
        Candidate candidate;
        std::vector<size_t> order(grid.size()); std::iota(order.begin(),order.end(),size_t(0));
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){ return grid[a].cost<grid[b].cost; });
        std::vector<Vector3> origins;
        for(size_t index:order) {
            const auto& start=grid[index];
            if(!std::isfinite(start.cost)) break;
            if(std::any_of(origins.begin(),origins.end(),[&](const auto& p){ return vector3_length_squared(p-start.source.origin)<1; })) continue;
            auto trial=polish(refine(optimize(start,start.energy)));
            if(trial.cost<candidate.cost) candidate=std::move(trial);
            origins.push_back(start.source.origin);
            if(origins.size()>=options.refineCandidates) break;
        }
        candidate=matchTarget(std::move(candidate));
        return candidate;
    }
    void optimizeGrid() {
        next=0; active=this;
        struct Reset { ~Reset(){ Engine::active=nullptr; } } reset;
        RunThreadsOnIndividual(int(std::min({grid.size(),size_t(std::max(numthreads,1)),size_t(32)})),true,worker,
            options.spot?"FitSpotCandidates":"FitPointCandidates",1);
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
    std::array<PointFitMetrics,2> metrics(bool save,bool includeWithheld,bool illuminatedOnly=false) {
        std::array<PointFitMetrics,2> totals{};
        if(save) result.prediction.resize(result.receivers.size());
        spend(includeWithheld?result.receivers.size():result.baseline[0].samples);
        for(size_t i=0;i<result.receivers.size();++i) {
            const auto& r=result.receivers[i]; if(r.withheld && !includeWithheld) continue;
            if(illuminatedOnly && !r.illuminated) continue;
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
        if(options.spot) {
            spend(result.receivers.size());
            for(size_t i=0;i<result.receivers.size();++i) {
                const Vector3b predicted=encode(i,Vector3(0)); auto& r=result.receivers[i];
                double residual=0;
                for(int a=0;a<3;++a) { const double d=std::max(0,int(r.observed[a])-int(predicted[a])); residual+=d*d; }
                r.illuminated=residual>3*options.minImprovement*options.minImprovement;
            }
            result.illuminatedBaseline=metrics(false,true,true); result.illuminatedTrial=result.illuminatedBaseline;
        }
        if(result.baseline[0].samples<24 || result.baseline[1].samples<12) {
            result.status="insufficient_observations"; return;
        }
        // Withheld values are never consulted for candidate selection, stopping,
        // position/color refinement or the number of fitted lights.
        if(result.baseline[0].rmse<=options.minImprovement) {
            bool qualified=result.baseline[0].rmse<=options.maxRMSE && result.baseline[1].rmse<=options.maxRMSE;
            if(options.spot) for(const auto& support:result.illuminatedBaseline) qualified&=support.rmse<=options.maxRMSE;
            result.status=qualified
                ?"baseline_explains_observations":"baseline_below_search_threshold_but_unqualified"; return;
        }
        if(options.spot && result.illuminatedBaseline[0].samples<12) { result.status="insufficient_spot_support"; return; }
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
            if(options.spot) {
                spend(bspLeafs.size()+1); ++positions;
                if(ClusterForPointExt(origin,0.125f)>=0) { ++result.usableCandidates; gridPositions.push_back(origin); }
            }
            else {
                auto candidate=prepare(origin);
                if(candidate.valid) { ++result.usableCandidates; grid.push_back(std::move(candidate)); }
            }
        }
        if(result.usableCandidates==0) { result.status="no_usable_candidates"; return; }
        const auto original=base;
        std::vector<Candidate> selected;
        std::vector<std::vector<Vector3>> responses;
        double previous=result.baseline[0].rmse;
        for(unsigned round=0;round<options.maxLights;++round) {
            if(options.spot) seedSpots();
            if(grid.empty()) break;
            optimizeGrid();
            auto best=std::min_element(grid.begin(),grid.end(),[](const auto& a,const auto& b){ return a.cost<b.cost; });
            if(best==grid.end() || !std::isfinite(best->cost)) break;
            if(round==0) {
                for(const auto& c:grid) if(std::isfinite(c.cost))
                    result.trainingAlternatives.push_back({description(c),std::sqrt(c.cost)});
                std::stable_sort(result.trainingAlternatives.begin(),result.trainingAlternatives.end(),
                    [](const auto& a,const auto& b){ return a.trainingRMSE<b.trainingRMSE; });
                if(result.trainingAlternatives.size()>8) result.trainingAlternatives.resize(8);
            }
            Candidate candidate;
            if(!options.spot) candidate=refine(*best);
            else candidate=refineSpotGrid();
            std::vector<Vector3> contribution;
            if(!contributions(candidate,contribution,false)) break;
            const auto before=base;
            for(size_t i=0;i<base.size();++i) base[i]+=contribution[i];
            const auto score=metrics(false,false);
            if(previous-score[0].rmse<(options.spot && !selected.empty()?0.01:options.minImprovement)) { base=before; break; }
            const auto oldSelected=options.spot?selected:std::vector<Candidate>{};
            const auto oldResponses=options.spot?responses:std::vector<std::vector<Vector3>>{};
            selected.push_back(std::move(candidate)); responses.push_back(std::move(contribution));
            // Refit existing lights with all other selected lights held fixed.
            // This reduces greedy position/color bias in overlapping sources.
            if(selected.size()>1) for(int pass=0;pass<2;++pass) for(size_t light=0;light<selected.size();++light) {
                base=original;
                for(size_t other=0;other<selected.size();++other) if(other!=light)
                    for(size_t i=0;i<base.size();++i) base[i]+=responses[other][i];
                auto trial=refine(optimize(selected[light],selected[light].energy));
                if(options.spot) trial=polish(std::move(trial));
                if(!contributions(trial,responses[light],false)) throw std::runtime_error("Unknown trace while refining point fit");
                selected[light]=std::move(trial);
            }
            base=original;
            if(options.spot && selected.size()>1) {
                // A first greedy source can explain overlapping lobes from the
                // wrong region. Reconsider each origin grid with the other
                // sources fixed before final joint polishing.
                for(size_t light=0;light<selected.size();++light) {
                    base=original;
                    for(size_t other=0;other<selected.size();++other) if(other!=light)
                        for(size_t i=0;i<base.size();++i) base[i]+=responses[other][i];
                    const double current=cost(selected[light],selected[light].energy);
                    seedSpots(); optimizeGrid();
                    auto relocated=refineSpotGrid();
                    if(relocated.cost<current) {
                        selected[light]=std::move(relocated);
                        if(!contributions(selected[light],responses[light],false)) throw std::runtime_error("Unknown trace while relocating spot fit");
                    }
                }
                base=original;
                auto joint=polishSources(selected);
                if(std::isfinite(joint.cost)) {
                    selected=std::move(joint.lights);
                    for(size_t i=0;i<selected.size();++i)
                        if(!contributions(selected[i],responses[i],false)) throw std::runtime_error("Unknown trace while jointly refining spot fit");
                }
            }
            for(const auto& colors:responses) for(size_t i=0;i<base.size();++i) base[i]+=colors[i];
            const double updated=metrics(false,false)[0].rmse;
            if(options.spot && previous-updated<options.minImprovement) {
                selected=oldSelected; responses=oldResponses; base=before; break;
            }
            previous=updated;
            if(previous<=options.minImprovement) break;
        }
        // First evaluate candidate transport at the withheld receivers here.
        base=original;
        result.subsampling.resize(result.receivers.size());
        for(const auto& candidate:selected) {
            if(vector3_max_component(candidate.energy)<=0) continue;
            result.lights.push_back(description(candidate));
            std::vector<Vector3> colors;
            if(!contributions(candidate,colors,true)) {
                result.status="unknown_validation_trace"; result.trial={}; result.illuminatedTrial={}; return;
            }
            for(size_t i=0;i<base.size();++i) base[i]+=colors[i];
        }
        result.trial=metrics(true,true);
        result.accepted=!result.lights.empty();
        for(size_t split=0;split<2;++split)
            result.accepted&=result.baseline[split].rmse-result.trial[split].rmse>=options.minImprovement
                && result.trial[split].rmse<=options.maxRMSE;
        if(options.spot) {
            result.illuminatedTrial=metrics(false,true,true);
            for(size_t split=0;split<2;++split) result.accepted&=result.illuminatedTrial[split].samples>=(split==0?12u:6u)
                && result.illuminatedTrial[split].rmse<=options.maxRMSE
                && result.illuminatedBaseline[split].rmse-result.illuminatedTrial[split].rmse>=options.minImprovement;
        }
        result.status=result.accepted?(options.spot?"conditional_spot_proposal":"conditional_point_proposal"):
            selected.empty()?"no_supported_improvement":"validation_rejected";
        if(options.spot && !selected.empty() && result.illuminatedTrial[1].samples<6) result.status="insufficient_spot_support";
    }
};
Engine* Engine::active=nullptr;
}

PointFitResult fitPointLights(const PointFitOptions& options,std::vector<PointFitReceiver> receivers) {
    const float scale=options.spot?spotScale:pointScale;
    if(!std::isfinite(scale) || scale<=0 || !std::isfinite(extraDist) || extraDist<0
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
