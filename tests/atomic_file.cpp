// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/atomic_file.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static std::string read( const std::filesystem::path& path ){
    std::ifstream file(path); return {std::istreambuf_iterator<char>(file), {}};
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
        for ( const auto& file : std::filesystem::directory_iterator(root) )
            if ( file.path().filename().string().find(".q3mapx-") != std::string::npos )
                throw std::runtime_error("Abandoned staged file or rollback copy retained");
        std::cout << "Atomic replacement, checked stream groups, abandonment, unique names and destination guards passed\n";
    } catch ( const std::exception& error ) { std::cerr << error.what() << '\n'; return 1; }
}
