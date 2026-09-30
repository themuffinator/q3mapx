// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <cstddef>
#include "math/vector.h"
class Args;
namespace q3mapx {
void checkProbeSourceBudget();
class LightProbes {
public:
    static std::unique_ptr<LightProbes> parse(Args& args,const char* source);
    ~LightProbes();
    void prepare();
    void prepareSurfaces();
    void checkLights() const;
    void run(const Vector3& ambient,size_t generated);
private:
    struct Data;
    explicit LightProbes(std::unique_ptr<Data> data);
    std::unique_ptr<Data> data_;
};
}
