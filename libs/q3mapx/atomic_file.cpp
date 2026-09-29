// SPDX-License-Identifier: GPL-3.0-or-later
#include "atomic_file.h"
#include <atomic>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace q3mapx {
AtomicFile::AtomicFile( std::filesystem::path destination ) : destination_( std::move(destination) ){
    static std::atomic<unsigned long long> serial{0};
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    for ( unsigned attempt = 0; attempt < 100; ++attempt ) {
        temporary_ = destination_;
        temporary_ += ".q3mapx-" + std::to_string(process) + "-" + std::to_string(serial++) + ".tmp";
#ifdef _WIN32
        HANDLE file = CreateFileW( temporary_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( file != INVALID_HANDLE_VALUE ) { CloseHandle(file); return; }
        const auto error = GetLastError();
        if ( error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS )
            throw std::system_error( error, std::system_category(), "Cannot reserve output " + temporary_.string() );
#else
        const int file = open( temporary_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666 );
        if ( file >= 0 ) { close(file); return; }
        if ( errno != EEXIST ) throw std::system_error( errno, std::generic_category(), "Cannot reserve output " + temporary_.string() );
#endif
    }
    throw std::runtime_error( "Cannot reserve a unique output beside " + destination_.string() );
}

AtomicFile::~AtomicFile(){
    if ( !committed_ ) { std::error_code ignored; std::filesystem::remove( temporary_, ignored ); }
}

void AtomicFile::commit(){
    if ( committed_ ) throw std::logic_error("Output already committed");
#ifdef _WIN32
    if ( !MoveFileExW( temporary_.c_str(), destination_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
        throw std::system_error( GetLastError(), std::system_category(), "Cannot replace " + destination_.string() );
#else
    std::filesystem::rename( temporary_, destination_ );
#endif
    committed_ = true;
}
}
