// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <memory>
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace q3mapx {
class LightRecovery {
public:
    static std::shared_ptr<LightRecovery> load(const char* bsp,const char* report);
    ~LightRecovery();
    void prepare();
    void protectOutputs(const char* map,const char* report) const;
    void verifyInputs() const;
    size_t entityCount() const;
    void writeEntities(FILE* file,size_t first) const;
    void writeReport(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer,size_t first) const;
private:
    struct Data;
    explicit LightRecovery(std::unique_ptr<Data> data);
    std::unique_ptr<Data> data_;
};
}
