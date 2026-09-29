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
        std::cout << "Interrupted/successful/failed replacement and unique names passed\n";
    } catch ( const std::exception& error ) { std::cerr << error.what() << '\n'; return 1; }
}
