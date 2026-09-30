
#pragma once
#include <cstdio>

void                        LoadIBSPFile( const char *filename );
int                         WriteIBSPFile( FILE *file );
void                        LoadIBSPorRBSPFilePartially( const char *filename );

class MemBuffer;
struct bspHeader_t;
// Shared prefix records used by read-only native format adapters. Directory
// indices are canonical IBSP indices; offsets still address the original file.
void LoadIBSPGeometry(const bspHeader_t& header, const MemBuffer& file,
    unsigned shaderBytes = 72, unsigned surfaceBytes = 104,
    unsigned leafBytes = 48, unsigned sideBytes = 8);
