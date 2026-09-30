// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

namespace q3mapx {
// Borrowed streams: throw on failure so the OutputFiles owner can close and
// remove its staging files before the caller reports a fatal compiler error.
void writeOutput( FILE* stream, const void* data, size_t size );
std::int64_t tellOutput( FILE* stream );
void seekOutput( FILE* stream, std::int64_t offset ); // Absolute, nonnegative.

// A reserved sibling path; the original remains intact until a complete file is committed.
class AtomicFile {
public:
    explicit AtomicFile( std::filesystem::path destination );
    ~AtomicFile();
    AtomicFile( const AtomicFile& ) = delete;
    AtomicFile& operator=( const AtomicFile& ) = delete;
    const std::filesystem::path& temporary() const { return temporary_; }
    void commit();
    // Keep a recovery copy if restoring a previously published file fails.
    void preserveTemporary() noexcept { preserved_ = true; }
private:
    std::filesystem::path destination_, temporary_;
    bool committed_ = false, preserved_ = false;
};

// Checked stdio streams staged beside their destinations. Publication follows
// open order; a reported failure rolls earlier names back to their old contents.
// Each rename is atomic, but process/power loss between renames is not a group
// transaction. Callers must not concurrently modify these destination paths.
class OutputFiles {
public:
    OutputFiles();
    ~OutputFiles();
    OutputFiles( const OutputFiles& ) = delete;
    OutputFiles& operator=( const OutputFiles& ) = delete;
    // The returned stream is borrowed; only this owner may close it.
    FILE* open( std::filesystem::path destination );
    // Checks every stream, prepares rollback copies, then publishes the files.
    void commit();
private:
    struct Entry;
    std::vector<std::unique_ptr<Entry>> entries_;
    bool finished_ = false;
};
}
