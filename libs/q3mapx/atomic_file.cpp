// SPDX-License-Identifier: GPL-3.0-or-later
#include "atomic_file.h"
#include <atomic>
#include <cerrno>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
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
void writeOutput( FILE* stream, const void* data, size_t size ){
    if ( !stream || ( size && !data ) ) throw std::invalid_argument("Invalid output stream/buffer");
    errno = 0;
    if ( ( size && std::fwrite( data, 1, size, stream ) != size ) || std::ferror( stream ) )
        throw std::system_error( errno ? errno : EIO, std::generic_category(), "Cannot write output" );
}

std::int64_t tellOutput( FILE* stream ){
    if ( !stream ) throw std::invalid_argument("Invalid output stream");
    errno = 0;
#ifdef _WIN32
    const auto offset = _ftelli64( stream );
#else
    const auto offset = ftello( stream );
#endif
    if ( offset < 0 )
        throw std::system_error( errno ? errno : EIO, std::generic_category(), "Cannot locate output position" );
    return offset;
}

void seekOutput( FILE* stream, std::int64_t offset ){
    if ( !stream || offset < 0 ) throw std::invalid_argument("Invalid output stream/offset");
    errno = 0;
#ifdef _WIN32
    const int result = _fseeki64( stream, offset, SEEK_SET );
#else
    if ( offset > std::numeric_limits<off_t>::max() ) throw std::overflow_error("Output offset exceeds supported range");
    const int result = fseeko( stream, off_t(offset), SEEK_SET );
#endif
    // Seeking can flush a buffered write. Do not discard its failure.
    if ( result != 0 || std::ferror( stream ) )
        throw std::system_error( errno ? errno : EIO, std::generic_category(), "Cannot seek output" );
}

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
    if ( !committed_ && !preserved_ ) { std::error_code ignored; std::filesystem::remove( temporary_, ignored ); }
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

namespace {
bool regularDestination( const std::filesystem::path& path ){
    std::error_code error;
    const auto status = std::filesystem::symlink_status( path, error );
    if ( error && error != std::errc::no_such_file_or_directory )
        throw std::system_error( error, "Cannot inspect output " + path.string() );
    if ( status.type() == std::filesystem::file_type::not_found ) return false;
    if ( !std::filesystem::is_regular_file( status ) )
        throw std::runtime_error( "Output must be a regular file, not a directory or link: " + path.string() );
    return true;
}

bool sameDestination( const std::filesystem::path& a, const std::filesystem::path& b ){
#ifdef _WIN32
    if ( CompareStringOrdinal( a.c_str(), -1, b.c_str(), -1, TRUE ) == CSTR_EQUAL ) return true;
#else
    if ( a == b ) return true;
#endif
    std::error_code ignored;
    return std::filesystem::equivalent( a, b, ignored );
}
}

struct OutputFiles::Entry {
    std::filesystem::path destination;
    AtomicFile staged;
    std::unique_ptr<AtomicFile> backup;
    FILE* stream = nullptr;
    bool published = false;
    explicit Entry( std::filesystem::path path ) : destination( std::move(path) ), staged( destination ) {}
    ~Entry(){ if ( stream ) std::fclose( stream ); }

    void close(){
        FILE* file = stream;
        stream = nullptr;
        const bool failed = std::ferror( file ) != 0;
        const int writeError = errno;
        errno = 0;
        const int result = std::fclose( file );
        if ( failed || result != 0 )
            throw std::system_error( failed && writeError ? writeError : errno ? errno : EIO,
                std::generic_category(), "Cannot finish output " + destination.string() );
    }
};

OutputFiles::OutputFiles() = default;
OutputFiles::~OutputFiles() = default;

FILE* OutputFiles::open( std::filesystem::path destination ){
    if ( finished_ ) throw std::logic_error( "Output publication already attempted" );
    destination = std::filesystem::absolute( destination ).lexically_normal();
    regularDestination( destination );
    for ( const auto& entry : entries_ )
        if ( sameDestination( destination, entry->destination ) )
            throw std::invalid_argument( "Duplicate output destination: " + destination.string() );
    auto entry = std::make_unique<Entry>( std::move(destination) );
#ifdef _WIN32
    entry->stream = _wfopen( entry->staged.temporary().c_str(), L"wb" );
#else
    entry->stream = std::fopen( entry->staged.temporary().c_str(), "wb" );
#endif
    if ( !entry->stream )
        throw std::system_error( errno, std::generic_category(), "Cannot open staged output " + entry->destination.string() );
    // Text exporters emit many small records. Stdio owns this bounded buffer.
    std::setvbuf( entry->stream, nullptr, _IOFBF, 64 * 1024 );
    FILE* stream = entry->stream;
    entries_.push_back( std::move(entry) );
    return stream;
}

void OutputFiles::commit(){
    if ( finished_ ) throw std::logic_error( "Output publication already attempted" );
    finished_ = true;
    // No destination is touched until every buffered stream closes successfully.
    for ( auto& entry : entries_ ) entry->close();
    for ( size_t i = 0; i < entries_.size(); ++i ) {
        auto& entry = *entries_[i];
        // The final rename either succeeds or leaves its original intact, so
        // only earlier destinations need a rollback copy (none for one file).
        if ( regularDestination( entry.destination ) && i + 1 < entries_.size() ) {
            entry.backup = std::make_unique<AtomicFile>( entry.destination );
            std::filesystem::copy_file( entry.destination, entry.backup->temporary(), std::filesystem::copy_options::overwrite_existing );
        }
    }
    try {
        for ( auto& entry : entries_ ) {
            entry->staged.commit();
            entry->published = true;
        }
    }
    catch ( const std::exception& failure ) {
        std::string rollbackErrors;
        for ( auto it = entries_.rbegin(); it != entries_.rend(); ++it ) {
            auto& entry = **it;
            if ( !entry.published ) continue;
            try {
                if ( entry.backup ) entry.backup->commit();
                else if ( regularDestination( entry.destination ) ) std::filesystem::remove( entry.destination );
            }
            catch ( const std::exception& error ) {
                if ( entry.backup ) entry.backup->preserveTemporary();
                rollbackErrors += "\nCannot restore " + entry.destination.string() + ": " + error.what();
                if ( entry.backup ) rollbackErrors += "; original saved at " + entry.backup->temporary().string();
            }
        }
        if ( !rollbackErrors.empty() )
            throw std::runtime_error( std::string(failure.what()) + rollbackErrors );
        throw;
    }
}
}
