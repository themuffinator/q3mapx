// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery_groups.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>

static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }

int main() try {
    std::mt19937 random(17031999);
    size_t exported=0,conflicts=0;
    for(size_t trial=0;trial<6000;++trial) {
        const size_t count=2+random()%19, opaque=random()%(count+1);
        std::vector<q3mapx::GroupBrushEvidence> brushes(count);
        for(size_t i=0;i<count;++i) {
            brushes[i].index=int(i); brushes[i].opaque=i<opaque;
            brushes[i].surfaces={int(random()%6)};
            if(random()%13==0) brushes[i].exclusion="protected";
        }
        uint64_t work=0;
        const auto plan=q3mapx::planRecoveryGroups(brushes,work,1'000'000);
        std::vector<bool> assigned(count);
        for(const auto& group:plan.groups) {
            if(std::string_view(group.status)=="conflicting_opacity_order") ++conflicts;
            if(!group.exported) continue;
            ++exported;
            require(group.members.size()>=2,"Singleton group");
            for(int member:group.members) {
                require(!assigned[member],"Duplicate group member"); assigned[member]=true;
                require(!brushes[member].exclusion,"Grouped protected brush");
            }
        }
        // Independently simulate FinishBrush and MoveBrushesToWorld, including
        // both reversals for opaque group members and ordinary world insertion.
        std::vector<int> world;
        for(size_t i=opaque;i-->0;) if(!assigned[i]) world.insert(world.begin(),int(i));
        for(size_t i=opaque;i<count;++i) if(!assigned[i]) world.push_back(int(i));
        for(size_t index:plan.emissionOrder) {
            const auto& group=plan.groups[index];
            require(group.exported,"Emission of rejected group");
            std::vector<int> loaded;
            for(int member:group.members) {
                if(brushes[member].opaque) loaded.insert(loaded.begin(),member);
                else loaded.push_back(member);
            }
            for(int member:loaded) {
                if(brushes[member].opaque) world.insert(world.begin(),member);
                else world.push_back(member);
            }
        }
        require(world.size()==count,"Brush count changed");
        for(size_t i=0;i<count;++i) require(world[i]==int(i),"Compiled brush ordering changed");
        uint64_t exact=0;
        const auto repeated=q3mapx::planRecoveryGroups(brushes,exact,work);
        require(exact==work && repeated.emissionOrder==plan.emissionOrder,"Unstable work/order");
        uint64_t insufficient=0; bool failed=false;
        try { q3mapx::planRecoveryGroups(brushes,insufficient,work-1); }
        catch(const std::runtime_error&) { failed=true; }
        require(failed,"Work limit ignored");
    }
    require(exported>100,"Insufficient positive ordering coverage");
    require(conflicts>0,"Missing conflicting-opacity coverage");
    std::cout << "6000 deterministic group/opacity/protection layouts; " << exported
              << " exported groups, " << conflicts << " conflicting-order decisions verified\n";
}
catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
