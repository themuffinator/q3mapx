// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/atomic_file.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static std::string read( const std::filesystem::path& path ){
    std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}};
}
int main( int argc, char** argv ){
    try {
        if ( argc != 2 ) throw std::runtime_error("Expected test output directory");
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        const auto target = root / "original.txt";
        std::ofstream(target) << "original";
        std::filesystem::path discarded;
        { q3mapx::AtomicFile out(target); discarded = out.temporary(); std::ofstream(discarded) << "unfinished"; }
        if ( read(target) != "original" || std::filesystem::exists(discarded) ) throw std::runtime_error("Interrupted write changed original");
        { q3mapx::AtomicFile a(target), b(target);
          if ( a.temporary() == b.temporary() ) throw std::runtime_error("Duplicate temporary paths");
          std::ofstream(a.temporary()) << "complete"; a.commit(); }
        if ( read(target) != "complete" ) throw std::runtime_error("Commit failed");
        const auto directory = root / "directory";
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "preserved") << "keep";
        bool rejected = false;
        try { q3mapx::AtomicFile out(directory); std::ofstream(out.temporary()) << "data"; out.commit(); }
        catch ( const std::system_error& ) { rejected = true; }
        if ( !rejected || read(directory / "preserved") != "keep" ) throw std::runtime_error("Failed replacement lost data");
        const auto companion = root / "companion.txt";
        std::ofstream(companion) << "original companion";
        {
            q3mapx::OutputFiles files;
            std::fputs("discarded companion", files.open(companion));
            std::fputs("discarded main", files.open(target));
        }
        if ( read(companion) != "original companion" || read(target) != "complete" )
            throw std::runtime_error("Discarded stream group changed originals");
        {
            q3mapx::OutputFiles files;
            std::fputs("new companion", files.open(companion));
            std::fputs("new main", files.open(target));
            files.commit();
            rejected = false;
            try { files.commit(); } catch ( const std::logic_error& ) { rejected = true; }
            if ( !rejected ) throw std::runtime_error("Repeated stream publication accepted");
        }
        if ( read(companion) != "new companion" || read(target) != "new main" )
            throw std::runtime_error("Stream group publication failed");
        {
            q3mapx::OutputFiles files;
            files.open(target);
            rejected = false;
            try { files.open(root / "." / "original.txt"); }
            catch ( const std::invalid_argument& ) { rejected = true; }
            if ( !rejected ) throw std::runtime_error("Duplicate output accepted");
        }
        {
            q3mapx::OutputFiles files;
            std::fputs("must not be published", files.open(target));
            rejected = false;
            try { files.open(directory); } catch ( const std::runtime_error& ) { rejected = true; }
            if ( !rejected ) throw std::runtime_error("Directory output accepted");
        }
        if ( read(target) != "new main" || read(directory / "preserved") != "keep" )
            throw std::runtime_error("Companion open failure changed originals");
        const auto binary = root / "binary.dat";
        {
            q3mapx::OutputFiles files;
            FILE* stream = files.open(binary);
            // Locate beyond signed 32-bit offsets without allocating a huge file.
            q3mapx::seekOutput(stream, std::int64_t(1) << 33);
            if ( q3mapx::tellOutput(stream) != (std::int64_t(1) << 33) )
                throw std::runtime_error("Output position was truncated");
            q3mapx::seekOutput(stream, 0);
            q3mapx::writeOutput(stream, nullptr, 0);
            q3mapx::writeOutput(stream, "\0\1\2\3\4", 5);
            q3mapx::seekOutput(stream, 1);
            q3mapx::writeOutput(stream, "\xff\xfe", 2);
            if ( q3mapx::tellOutput(stream) != 3 ) throw std::runtime_error("Output position is incorrect");
            files.commit();
        }
        if ( read(binary) != std::string("\0\xff\xfe\3\4", 5) )
            throw std::runtime_error("Binary seek/write changed bytes or file length");
        for ( const bool invalidBuffer : {false, true} ) {
            rejected = false;
            try {
                q3mapx::OutputFiles files;
                FILE* stream = files.open(binary);
                q3mapx::writeOutput(stream, "unfinished", 10);
                if ( invalidBuffer ) q3mapx::writeOutput(stream, nullptr, 1);
                else q3mapx::seekOutput(stream, -1);
                files.commit();
            }
            catch ( const std::invalid_argument& ) { rejected = true; }
            if ( !rejected || read(binary) != std::string("\0\xff\xfe\3\4", 5) )
                throw std::runtime_error("Invalid binary operation changed existing output");
        }
        // A real stdio write failure must throw, including when errno is set by
        // the operating system. This borrowed stream has its own local owner.
        {
#ifdef _WIN32
            FILE* stream = _wfopen(binary.c_str(), L"rb");
#else
            FILE* stream = std::fopen(binary.c_str(), "rb");
#endif
            if ( !stream ) throw std::runtime_error("Cannot open read-only test stream");
            const auto close = [](FILE* file){ std::fclose(file); };
            std::unique_ptr<FILE, decltype(close)> owner(stream, close);
            std::setvbuf(stream, nullptr, _IONBF, 0);
            rejected = false;
            try { q3mapx::writeOutput(stream, "x", 1); }
            catch ( const std::system_error& ) { rejected = true; }
            if ( !rejected ) throw std::runtime_error("Read-only stream accepted a write");
        }
        for ( const auto& file : std::filesystem::directory_iterator(root) )
            if ( file.path().filename().string().find(".q3mapx-") != std::string::npos )
                throw std::runtime_error("Abandoned staged file or rollback copy retained");
        std::cout << "Atomic replacement, checked binary writes/seeks, stream groups, abandonment and destination guards passed\n";
    } catch ( const std::exception& error ) { std::cerr << error.what() << '\n'; return 1; }
}
