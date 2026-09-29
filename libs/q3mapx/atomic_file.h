// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>

namespace q3mapx {
// A reserved sibling path; the original remains intact until a complete file is committed.
class AtomicFile {
public:
    explicit AtomicFile( std::filesystem::path destination );
    ~AtomicFile();
    AtomicFile( const AtomicFile& ) = delete;
    AtomicFile& operator=( const AtomicFile& ) = delete;
    const std::filesystem::path& temporary() const { return temporary_; }
    void commit();
private:
    std::filesystem::path destination_, temporary_;
    bool committed_ = false;
};
}
