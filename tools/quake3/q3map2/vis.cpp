/* -------------------------------------------------------------------------------

   Copyright (C) 1999-2007 id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

   ----------------------------------------------------------------------------------

   This code has been altered significantly from its original form, to support
   several games based on the Quake III Arena engine, in the form of "Q3Map2."

   ------------------------------------------------------------------------------- */



/* dependencies */
#include "q3map2.h"
#include "vis.h"
#include "visflow.h"
#include "q3mapx/portal_graph.h"

vportal_t          *sorted_portals[ MAX_MAP_PORTALS * 2 ];


static visPlane_t PlaneFromWinding( const fixedWinding_t *w ){
	// calc plane
	visPlane_t plane;
	if ( !PlaneFromPoints( plane, w->points ) ) Error("Degenerate portal winding");
	if (!std::isfinite(plane.dist()) || !std::isfinite(plane.normal().x()) || !std::isfinite(plane.normal().y()) || !std::isfinite(plane.normal().z()))
		Error("Non-finite portal plane");
	return plane;
}


/*
   NewFixedWinding()
   returns a new fixed winding
   ydnar: altered this a bit to reconcile multiply-defined winding_t
 */

static fixedWinding_t *NewFixedWinding( int numpoints ){
	if ( numpoints < 3 || numpoints > MAX_POINTS_ON_WINDING ) {
		Error( "NewWinding: %i points", numpoints );
	}
	return safe_calloc( offsetof_array( fixedWinding_t, points, numpoints ) );
}



static void print_leaf( const leaf_t *l ){
	for ( const vportal_t *p : Span( l->portals, l->numportals ) )
	{
		const visPlane_t pl = p->plane;
		Sys_Printf( "portal %4i to leaf %4i : %7.1f : (%4.1f, %4.1f, %4.1f)\n", (int)( p - portals ), p->leaf, pl.dist(), pl.normal()[0], pl.normal()[1], pl.normal()[2] );
	}
}


//=============================================================================

/*
   =============
   SortPortals

   Sorts the portals from the least complex, so the later ones can reuse
   the earlier information.
   =============
 */
static void SortPortals(){
	for ( int i = 0; i < numportals * 2; ++i )
		sorted_portals[i] = &portals[i];

	if ( !nosort ) {
		if (reproducibleVis) {
			std::ranges::sort(Span(sorted_portals, numportals * 2), [](const auto* a, const auto* b){
				return a->nummightsee == b->nummightsee ? a < b : a->nummightsee < b->nummightsee;
			});
		}
		else std::ranges::sort( Span( sorted_portals, numportals * 2 ), {}, &vportal_t::nummightsee );
	}
	for (int i = 0; i < numportals * 2; ++i) sorted_portals[i]->flowOrder = i;
}


/*
   ==============
   LeafVectorFromPortalVector
   ==============
 */
static int LeafVectorFromPortalVector( byte *portalbits, byte *leafbits ){
	for ( int i = 0; i < visPortalBits; ++i )
	{
		if ( bit_is_enabled( portalbits, i ) ) {
			const vportal_t& p = *activePortals[i];
			bit_enable( leafbits, p.leaf );
		}
	}

	for ( int i = 0; i < portalclusters; ++i )
	{
		int leafnum = i;
		while ( leafs[leafnum].merged >= 0 )
			leafnum = leafs[leafnum].merged;
		//if the merged leaf is visible then the original leaf is visible
		if ( bit_is_enabled( leafbits, leafnum ) ) {
			bit_enable( leafbits, i );
		}
	}
	return CountBits( leafbits, portalclusters ); //c_leafs
}


/*
   ===============
   ClusterMerge

   Merges the portal visibility for a leaf
   ===============
 */
static int clustersizehistogram[MAX_MAP_LEAFS] = {0};

static void ClusterMerge( int leafnum ){
	alignas(VisWord) byte portalvector[MAX_PORTALS / 8];
	byte uncompressed[MAX_MAP_LEAFS / 8];
	int numvis, mergedleafnum;

	// OR together all the portalvis bits

	mergedleafnum = leafnum;
	while ( leafs[mergedleafnum].merged >= 0 )
		mergedleafnum = leafs[mergedleafnum].merged;

	memset( portalvector, 0, portalbytes );

	for ( const vportal_t *p : Span( leafs[mergedleafnum].portals, leafs[mergedleafnum].numportals ) )
	{
		if ( p->removed ) {
			continue;
		}

		if ( p->getStatus() != EVStatus::Done ) {
			Error( "portal not done" );
		}
		for ( int j = 0; j < portalwords; ++j )
			( (VisWord *)portalvector )[j] |= ( (VisWord *)p->portalvis )[j];
		bit_enable( portalvector, p->visIndex );
	}

	memset( uncompressed, 0, leafbytes );

	bit_enable( uncompressed, mergedleafnum );
	// convert portal bits to leaf bits
	numvis = LeafVectorFromPortalVector( portalvector, uncompressed );

//	if ( uncompressed[leafnum >> 3] & ( 1 << ( leafnum & 7 ) ) )
//		Sys_Warning( "Leaf portals saw into leaf\n" );

//	uncompressed[leafnum >> 3] |= ( 1 << ( leafnum & 7 ) );

	// LeafVectorFromPortalVector already counts the self bit and merged members.

	//Sys_FPrintf( SYS_VRB, "cluster %4i : %4i visible\n", leafnum, numvis );
	++clustersizehistogram[numvis];

	memcpy( bspVisBytes.data() + VIS_HEADER_SIZE + leafnum * leafbytes, uncompressed, leafbytes );
}

/*
   ==================
   CalcPortalVis
   ==================
 */
static void (*batchFlow)(int);
static int batchBegin;
static void RunPortalBatchItem(int index){ batchFlow(batchBegin + index); }

static void RunPortalFlow(void (*flow)(int), const char* name, bool progress){
	const int total = numportals * 2;
	if (!reproducibleVis) {
		RunThreadsOnIndividual(total, progress, flow, name);
		return;
	}
	// A fixed batch size and stable sort make the set of reusable portal results
	// independent of worker count and completion order. Results become visible to
	// pruning only after every job in the preceding batch has joined.
	constexpr int batchSize = 64;
	batchFlow = flow;
	for (batchBegin = 0; batchBegin < total; batchBegin += batchSize) {
		publishedPortalCount = batchBegin;
		RunThreadsOnIndividual(std::min(batchSize, total - batchBegin), false, RunPortalBatchItem, name);
		if (progress && batchBegin % (batchSize * 16) == 0)
			Sys_Printf("  reproducible VIS: %d/%d portals\n", std::min(batchBegin + batchSize, total), total);
	}
	publishedPortalCount = total;
}

static void CalcPortalVis(){
#ifdef MREDEBUG
	Sys_Printf( "%6d portals out of %d", 0, numportals * 2 );
	//get rid of the counter
	RunPortalFlow(PortalFlow, "PortalFlow", false);
#else
	RunPortalFlow(PortalFlow, "PortalFlow", true);
#endif
}

/*
   ==================
   CalcPassageVis
   ==================
 */
static void CalcPassageVis(){
	PassageMemory();

#ifdef MREDEBUG
	_printf( "%6d portals out of %d", 0, numportals * 2 );
	RunThreadsOnIndividual( numportals * 2, false, CreatePassages, "CreatePassages" );
	_printf( "\n" );
	_printf( "%6d portals out of %d", 0, numportals * 2 );
	RunPortalFlow(PassageFlow, "PassageFlow", false);
	_printf( "\n" );
#else
	Sys_Printf( "\n--- CreatePassages (%d) ---\n", numportals * 2 );
	RunThreadsOnIndividual( numportals * 2, true, CreatePassages, "CreatePassages" );

	Sys_Printf( "\n--- PassageFlow (%d) ---\n", numportals * 2 );
	RunPortalFlow(PassageFlow, "PassageFlow", true);
#endif
}

/*
   ==================
   CalcPassagePortalVis
   ==================
 */
static void CalcPassagePortalVis(){
	PassageMemory();

#ifdef MREDEBUG
	Sys_Printf( "%6d portals out of %d", 0, numportals * 2 );
	RunThreadsOnIndividual( numportals * 2, false, CreatePassages, "CreatePassages" );
	Sys_Printf( "\n" );
	Sys_Printf( "%6d portals out of %d", 0, numportals * 2 );
	RunPortalFlow(PassagePortalFlow, "PassagePortalFlow", false);
	Sys_Printf( "\n" );
#else
	Sys_Printf( "\n--- CreatePassages (%d) ---\n", numportals * 2 );
	RunThreadsOnIndividual( numportals * 2, true, CreatePassages, "CreatePassages" );

	Sys_Printf( "\n--- PassagePortalFlow (%d) ---\n", numportals * 2 );
	RunPortalFlow(PassagePortalFlow, "PassagePortalFlow", true);
#endif
}

/*
   ==================
   CalcFastVis
   ==================
 */
static void CalcFastVis(){
	// fastvis just uses mightsee for a very loose bound
	for ( vportal_t *p : activePortals )
	{
		free( p->portalvis );
		p->portalvis = p->portalflood;
		p->setStatus( EVStatus::Done );
	}
}

/*
   ==================
   CalcVis
   ==================
 */
static void CalcVis(){
	int i, minvis, maxvis;
	double mu, sigma, totalvis, totalvis2;


	/* ydnar: rr2do2's farplane code */
	const char *value;
	if( entities[ 0 ].read_keyvalue( value, "_farplanedist",         /* proper '_' prefixed key */
	                                        "fogclip",               /* wolf compatibility */
	                                        "distancecull" ) ){      /* sof2 compatibility */
		farPlaneDist = atof( value );
		farPlaneDistMode = value[strlen( value ) - 1 ];
		if ( farPlaneDist != 0 ) {
			Sys_Printf( "farplane distance = %.1f\n", farPlaneDist );
			if ( farPlaneDistMode == 'o' )
				Sys_Printf( "farplane Origin2Origin mode on\n" );
			else if ( farPlaneDistMode == 'r' )
				Sys_Printf( "farplane Radius+Radius mode on\n" );
			else if ( farPlaneDistMode == 'e' )
				Sys_Printf( "farplane Exact distance mode on\n" );
		}
	}

	Sys_Printf( "\n--- BasePortalVis (%d) ---\n", numportals * 2 );
	RunThreadsOnIndividual( numportals * 2, true, BasePortalVis, "BasePortalVis" );

//	RunThreadsOnIndividual( numportals * 2, true, BetterPortalVis, "BetterPortalVis" );

	SortPortals();

	if ( fastvis ) {
		CalcFastVis();
	}
	else if ( noPassageVis ) {
		CalcPortalVis();
	}
	else if ( passageVisOnly ) {
		CalcPassageVis();
	}
	else {
		CalcPassagePortalVis();
	}
	//
	// assemble the leaf vis lists by oring and compressing the portal lists
	//
	Sys_Printf( "creating leaf vis...\n" );
	for ( i = 0; i < portalclusters; ++i )
		ClusterMerge( i );

	totalvis = 0;
	totalvis2 = 0;
	minvis = -1;
	maxvis = -1;
	for ( i = 0; i < MAX_MAP_LEAFS; ++i )
		if ( clustersizehistogram[i] ) {
			if ( debugCluster ) {
				Sys_FPrintf( SYS_VRB, "%4i clusters have exactly %4i visible clusters\n", clustersizehistogram[i], i );
			}
			/* cast is to prevent integer overflow */
			totalvis  += (double) i     * clustersizehistogram[i];
			totalvis2 += (double) i * i * clustersizehistogram[i];

			if ( minvis < 0 ) {
				minvis = i;
			}
			maxvis = i;
		}

	mu = totalvis / portalclusters;
	sigma = sqrt( totalvis2 / portalclusters - mu * mu );

	Sys_Printf( "Total clusters: %i\n", portalclusters );
	Sys_Printf( "Total visible clusters: %.0f\n", totalvis );
	Sys_Printf( "Average clusters visible: %.2f (%.3f%%/total)\n", mu, mu / portalclusters * 100.0 );
	Sys_Printf( "  Standard deviation: %.2f (%.3f%%/total, %.3f%%/avg)\n", sigma, sigma / portalclusters * 100.0, sigma / mu * 100.0 );
	Sys_Printf( "  Minimum: %i (%.3f%%/total, %.3f%%/avg)\n", minvis, minvis / (double) portalclusters * 100.0, minvis / mu * 100.0 );
	Sys_Printf( "  Maximum: %i (%.3f%%/total, %.3f%%/avg)\n", maxvis, maxvis / (double) portalclusters * 100.0, maxvis / mu * 100.0 );
}

/*
   ==================
   SetPortalSphere
   ==================
 */
static void SetPortalSphere( vportal_t& p ){
	DoubleVector3 origin( 0 );

	for ( const Vector3& point : Span( p.winding->points, p.winding->numpoints ) )
	{
		origin += point;
	}

	origin /= p.winding->numpoints;

	double bestr = 0;
	for ( const Vector3& point : Span( p.winding->points, p.winding->numpoints ) )
	{
		value_maximize( bestr, vector3_length( point - origin ) );
	}
	p.origin = origin;
	p.radius = bestr;
}

/*
   =============
   Winding_PlanesConcave
   =============
 */
#define WCONVEX_EPSILON     0.2

static bool Winding_PlanesConcave( const fixedWinding_t *w1, const fixedWinding_t *w2,
                                   const Plane3f& plane1, const Plane3f& plane2 ){
	if ( !w1 || !w2 ) {
		return false;
	}

	// check if one of the points of winding 1 is at the front of the plane of winding 2
	for ( const Vector3& point : Span( w1->points, w1->numpoints ) )
	{
		if ( plane3_distance_to_point( plane2, point ) > WCONVEX_EPSILON ) {
			return true;
		}
	}
	// check if one of the points of winding 2 is at the front of the plane of winding 1
	for ( const Vector3& point : Span( w2->points, w2->numpoints ) )
	{
		if ( plane3_distance_to_point( plane1, point ) > WCONVEX_EPSILON ) {
			return true;
		}
	}

	return false;
}

/*
   ============
   TryMergeLeaves
   ============
 */
static bool TryMergeLeaves( int l1num, int l2num ){
	vportal_t *portals[MAX_PORTALS_ON_LEAF];
	if ( l1num == l2num || leafs[l1num].merged >= 0 || leafs[l2num].merged >= 0 ) {
		return false;
	}
	// Check both lists before changing either. A valid input leaf can have up to
	// MAX_PORTALS_ON_LEAF faces/portals; their union need not fit the same storage.
	for ( const leaf_t *lfs : { faceleafs, leafs } ) {
		int count = 0;
		for ( const int source : { l1num, l2num } ) {
			const int other = source == l1num ? l2num : l1num;
			for ( const vportal_t *p : Span( lfs[source].portals, lfs[source].numportals ) ) {
				if ( p->removed ) continue;
				if ( p->leaf == other ) {
					// Another opening between these same leaves may be a hint.
					if ( p->hint ) return false;
				}
				else if ( ++count > MAX_PORTALS_ON_LEAF ) return false;
			}
		}
	}

	for ( const leaf_t *l1 : { &faceleafs[l1num], &leafs[l1num] } )
	{
		for ( const vportal_t *p1 : Span( l1->portals, l1->numportals ) )
		{
			if ( p1->leaf == l2num ) {
				continue;
			}
			for ( const leaf_t *l2 : { &faceleafs[l2num], &leafs[l2num] } )
			{
				for ( const vportal_t *p2 : Span( l2->portals, l2->numportals ) )
				{
					if ( p2->leaf == l1num ) {
						continue;
					}
					//
					if ( Winding_PlanesConcave( p1->winding, p2->winding, p1->plane, p2->plane ) ) {
						return false;
					}
				}
			}
		}
	}
	for ( leaf_t *lfs : { faceleafs, leafs } )
	{
		leaf_t& l1 = lfs[l1num];
		leaf_t& l2 = lfs[l2num];
		int numportals = 0;
		//the leaves can be merged now
		for ( vportal_t *p1 : Span( l1.portals, l1.numportals ) )
		{
			if ( p1->leaf == l2num ) {
				p1->removed = true;
				continue;
			}
			portals[numportals++] = p1;
		}
		for ( vportal_t *p2 : Span( l2.portals, l2.numportals ) )
		{
			if ( p2->leaf == l1num ) {
				p2->removed = true;
				continue;
			}
			portals[numportals++] = p2;
		}
		std::copy_n( portals, numportals, l2.portals );
		l2.numportals = numportals;
		l1.merged = l2num;
	}
	return true;
}

/*
   ============
   UpdatePortals
   ============
 */
static void UpdatePortals(){
	for ( vportal_t& p : Span( portals, numportals * 2 ) )
		if ( !p.removed )
			while ( leafs[p.leaf].merged >= 0 )
				p.leaf = leafs[p.leaf].merged;
}

/*
   ============
   MergeLeaves

   try to merge leaves but don't merge through hint splitters
   ============
 */
static void MergeLeaves(){
	int nummerges, totalnummerges = 0;

	do
	{
		nummerges = 0;
		for ( int i = 0; i < portalclusters; ++i )
		{
			const leaf_t& leaf = leafs[i];
			//if this leaf is merged already

			/* ydnar: vmods: merge all non-hint portals */
			if ( leaf.merged >= 0 ) {
				continue;
			}


			for ( const vportal_t *p : Span( leaf.portals, leaf.numportals ) )
			{
				//never merge through hint portals
				if ( !p->removed && !p->hint ) {
					if ( TryMergeLeaves( i, p->leaf ) ) {
						UpdatePortals();
						nummerges++;
						break;
					}
				}
			}
		}
		totalnummerges += nummerges;
	} while ( nummerges );
	Sys_Printf( "%6d leaves merged\n", totalnummerges );
}

/*
   ============
   TryMergeWinding
   ============
 */
#define CONTINUOUS_EPSILON  0.005

static fixedWinding_t *TryMergeWinding( fixedWinding_t *f1, fixedWinding_t *f2, const Vector3& planenormal ){
	const Vector3       *p1, *p2, *p3, *p4, *back;
	fixedWinding_t  *newf;
	int i, j, k, l;
	Vector3 normal;
	float dot;
	bool keep1, keep2;


	//
	// find a common edge
	//
	p1 = p2 = nullptr; // stop compiler warning
	j = 0;          //

	for ( i = 0; i < f1->numpoints; ++i )
	{
		p1 = &f1->points[i];
		p2 = &f1->points[( i + 1 ) % f1->numpoints];
		for ( j = 0; j < f2->numpoints; ++j )
		{
			p3 = &f2->points[j];
			p4 = &f2->points[( j + 1 ) % f2->numpoints];
			for ( k = 0; k < 3; ++k )
			{
				if ( std::fabs( ( *p1 )[k] - ( *p4 )[k] ) > 0.1f ) { //EQUAL_EPSILON) //ME
					break;
				}
				if ( std::fabs( ( *p2 )[k] - ( *p3 )[k] ) > 0.1f ) { //EQUAL_EPSILON) //ME
					break;
				}
			}
			if ( k == 3 ) {
				break;
			}
		}
		if ( j < f2->numpoints ) {
			break;
		}
	}

	if ( i == f1->numpoints ) {
		return nullptr;            // no matching edges
	}
	//
	// check slope of connected lines
	// if the slopes are colinear, the point can be removed
	//
	back = &f1->points[( i + f1->numpoints - 1 ) % f1->numpoints];
	normal = VectorNormalized( vector3_cross( planenormal, *p1 - *back ) );

	back = &f2->points[( j + 2 ) % f2->numpoints];
	dot = vector3_dot( *back - *p1, normal );
	if ( dot > CONTINUOUS_EPSILON ) {
		return nullptr;            // not a convex polygon
	}
	keep1 = ( dot < -CONTINUOUS_EPSILON );

	back = &f1->points[( i + 2 ) % f1->numpoints];
	normal = VectorNormalized( vector3_cross( planenormal, *back - *p2 ) );

	back = &f2->points[( j + f2->numpoints - 1 ) % f2->numpoints];
	dot = vector3_dot( *back - *p2, normal );
	if ( dot > CONTINUOUS_EPSILON ) {
		return nullptr;            // not a convex polygon
	}
	keep2 = ( dot < -CONTINUOUS_EPSILON );

	//
	// build the new polygon
	//
	const int count = f1->numpoints + f2->numpoints - 2 - !keep1 - !keep2;
	if ( count < 3 || count > MAX_POINTS_ON_WINDING ) return nullptr;
	newf = NewFixedWinding( count );

	// copy first polygon
	for ( k = ( i + 1 ) % f1->numpoints; k != i; k = ( k + 1 ) % f1->numpoints )
	{
		if ( k == ( i + 1 ) % f1->numpoints && !keep2 ) {
			continue;
		}

		newf->points[newf->numpoints] = f1->points[k];
		newf->numpoints++;
	}

	// copy second polygon
	for ( l = ( j + 1 ) % f2->numpoints; l != j; l = ( l + 1 ) % f2->numpoints )
	{
		if ( l == ( j + 1 ) % f2->numpoints && !keep1 ) {
			continue;
		}
		newf->points[newf->numpoints] = f2->points[l];
		newf->numpoints++;
	}

	return newf;
}

/*
   ============
   MergeLeafPortals
   ============
 */
static void MergeLeafPortals(){
	int i, j, k, nummerges, hintsmerged;
	leaf_t *leaf;
	vportal_t *p1, *p2;
	fixedWinding_t *w;

	nummerges = 0;
	hintsmerged = 0;
	for ( i = 0; i < portalclusters; ++i )
	{
		leaf = &leafs[i];
		if ( leaf->merged >= 0 ) {
			continue;
		}
		for ( j = 0; j < leaf->numportals; ++j )
		{
			p1 = leaf->portals[j];
			if ( p1->removed ) {
				continue;
			}
			for ( k = j + 1; k < leaf->numportals; ++k )
			{
				p2 = leaf->portals[k];
				if ( p2->removed ) {
					continue;
				}
				// A shared edge alone does not establish coplanarity. Preserve hint
				// and sky semantics instead of combining differently flagged openings.
				if ( p1->leaf == p2->leaf && p1->hint == p2->hint && p1->sky == p2->sky
				  && vector3_length_squared( p1->plane.normal() - p2->plane.normal() ) < 1e-10f
				  && std::fabs( p1->plane.dist() - p2->plane.dist() ) < 0.005f ) {
					// The convexity test expects the winding plane; VIS stores its
					// opposite, pointing into the neighboring leaf.
					w = TryMergeWinding( p1->winding, p2->winding, -p1->plane.normal() );
					if ( w ) {
						free( p1->winding );    //% FreeWinding( p1->winding );
						p1->winding = w;
						if ( p1->hint && p2->hint ) {
							hintsmerged++;
						}
						p1->hint |= p2->hint;
						SetPortalSphere( *p1 );
						p2->removed = true;
						nummerges++;
						i--;
						break;
					}
				}
			}
			if ( k < leaf->numportals ) {
				break;
			}
		}
	}
	Sys_Printf( "%6d portals merged\n", nummerges );
	Sys_Printf( "%6d hint portals merged\n", hintsmerged );
}


/*
   ============
   WritePortals
   ============
 */
static int CountActivePortals(){
	int num = 0, hints = 0;

	for ( const vportal_t& p : Span( portals, numportals * 2 ) )
	{
		if ( !p.removed ) {
			num++;
			if ( p.hint )
				hints++;
		}
	}
	Sys_Printf( "%6d active portals\n", num );
	Sys_Printf( "%6d hint portals\n", hints );
	return num;
}

// Relabel only live visibility bits. Keep portal objects, sort keys, and job
// slots unchanged: removing jobs would shift reproducible publication batches
// and could change which completed results a portal is allowed to use for pruning.
static void IndexActivePortals(){
	activePortals.clear();
	activePortals.reserve( numportals * 2 );
	for ( vportal_t& p : Span( portals, numportals * 2 ) ) {
		p.visIndex = -1;
		if ( !p.removed ) {
			p.visIndex = static_cast<int>( activePortals.size() );
			activePortals.push_back( &p );
		}
	}
	visPortalBits = static_cast<int>( activePortals.size() );
	const int oldBytes = portalbytes;
	portalbytes = ( ( visPortalBits + 63 ) & ~63 ) >> 3;
	portalwords = portalbytes / sizeof( VisWord );
	Sys_Printf( "VIS portal bitsets: %d live / %d input directions; %d -> %d bytes each\n",
	            visPortalBits, numportals * 2, oldBytes, portalbytes );
}

/*
   ============
   LoadPortals
   ============
 */
static void LoadPortals( const char *name ) try {
	static_assert(q3mapx::PortalLimits{}.clusters == MAX_MAP_VISCLUSTERS);
	static_assert(q3mapx::PortalLimits{}.portals == MAX_PORTALS / 2);
	static_assert(q3mapx::PortalLimits{}.faces == MAX_MAP_PORTALS * 2);
	static_assert(q3mapx::PortalLimits{}.pointsPerWinding == MAX_POINTS_ON_WINDING);
	static_assert(q3mapx::PortalLimits{}.windingsPerCluster == MAX_PORTALS_ON_LEAF);
	const auto graph = q3mapx::readPortalGraph(name);
	portalclusters = graph.clusters;
	for (const auto& leaf : bspLeafs)
		if (leaf.cluster >= portalclusters) Error("Portal clusters do not cover BSP leaf clusters");
	numportals = int(graph.portals.size());
	numfaces = int(graph.faces.size());
	Sys_Printf("%6i portalclusters\n%6i numportals\n%6i numfaces\n", portalclusters, numportals, numfaces);
	leafbytes = ((portalclusters + 63) & ~63) >> 3;
	portalbytes = ((numportals * 2 + 63) & ~63) >> 3;
	portalwords = portalbytes / sizeof(VisWord);
	portals = safe_calloc(2 * numportals * sizeof(vportal_t));
	leafs = safe_calloc(portalclusters * sizeof(leaf_t));
	for (leaf_t& leaf : Span(leafs,portalclusters)) leaf.merged = -1;
	bspVisBytes.resize(VIS_HEADER_SIZE + size_t(portalclusters) * leafbytes);
	if (bspVisBytes.size() > MAX_MAP_VISIBILITY) Error("MAX_MAP_VISIBILITY exceeded");
	((int*)bspVisBytes.data())[0] = portalclusters;
	((int*)bspVisBytes.data())[1] = leafbytes;
	const auto winding = [](const auto& polygon, bool reverse) {
		auto* w = NewFixedWinding(int(polygon.points.size()));
		w->numpoints = int(polygon.points.size());
		for (size_t j=0; j<polygon.points.size(); ++j) {
			const auto& point = polygon.points[reverse ? polygon.points.size()-1-j : j];
			w->points[j] = Vector3(point[0],point[1],point[2]);
		}
		return w;
	};
	for (int i=0; i<numportals; ++i) {
		const auto& polygon = graph.portals[i];
		auto* forward = winding(polygon,false);
		const auto plane = PlaneFromWinding(forward);
		for (int direction=0; direction<2; ++direction) {
			auto& p = portals[i*2+direction];
			p.num = i+1;
			p.hint = (polygon.flags & 1) != 0;
			p.sky = (polygon.flags & 2) != 0;
			p.winding = direction ? winding(polygon,true) : forward;
			p.plane = direction ? plane : plane3_flipped(plane);
			p.leaf = direction ? polygon.front : polygon.back;
			SetPortalSphere(p);
			auto& leaf = leafs[direction ? polygon.back : polygon.front];
			leaf.portals[leaf.numportals++] = &p;
		}
	}
	faces = safe_calloc(numfaces * sizeof(vportal_t));
	faceleafs = safe_calloc(portalclusters * sizeof(leaf_t));
	for (leaf_t& leaf : Span(faceleafs,portalclusters)) leaf.merged = -1;
	for (int i=0; i<numfaces; ++i) {
		const auto& polygon = graph.faces[i];
		auto& p = faces[i];
		p.num = i+1;
		p.winding = winding(polygon,false);
		p.plane = plane3_flipped(PlaneFromWinding(p.winding));
		p.leaf = -1;
		SetPortalSphere(p);
		auto& leaf = faceleafs[polygon.front];
		leaf.portals[leaf.numportals++] = &p;
	}
}
catch(const std::exception& error) { Error("LoadPortals: %s",error.what()); }


/*
   ===========
   VisMain
   ===========
 */
int VisMain( Args& args ){
	char portalfile[1024];


	/* note it */
	Sys_Printf( "--- Vis ---\n" );

	/* process arguments */
	if ( args.empty() ) {
		Error( "usage: vis [-threads #] [-fast] [-v] bspfile" );
	}
	const char *fileName = args.takeBack();
	const auto argsToInject = args.getVector();
	{
		while ( args.takeArg( "-fast" ) ) {
			Sys_Printf( "fastvis = true\n" );
			fastvis = true;
		}
		while (args.takeArg("-reproducible")) {
			Sys_Printf("Reproducible visibility: fixed publication batches\n");
			reproducibleVis = true;
		}
		while ( args.takeArg( "-merge" ) ) {
			Sys_Printf( "merge = true\n" );
			mergevis = true;
		}
		while ( args.takeArg( "-mergeportals" ) ) {
			Sys_Printf( "mergeportals = true\n" );
			mergevisportals = true;
		}
		while ( args.takeArg( "-nopassage" ) ) {
			Sys_Printf( "nopassage = true\n" );
			noPassageVis = true;
		}
		while ( args.takeArg( "-passageOnly" ) ) {
			Sys_Printf( "passageOnly = true\n" );
			passageVisOnly = true;
		}
		while ( args.takeArg( "-nosort" ) ) {
			Sys_Printf( "nosort = true\n" );
			nosort = true;
		}
		while ( args.takeArg( "-saveprt" ) ) {
			Sys_Printf( "saveprt = true\n" );
			saveprt = true;
		}
		while ( args.takeArg( "-v" ) ) {
			debugCluster = true;
			Sys_Printf( "Extra verbose mode enabled\n" );
		}
		/* ydnar: -hint to merge all but hint portals */
		while ( args.takeArg( "-hint" ) ) {
			Sys_Printf( "hint = true\n" );
			mergevis = true;
		}

		while( !args.empty() )
		{
			Sys_Warning( "Unknown option \"%s\"\n", args.takeFront() );
		}
	}


	/* load the bsp */
	strcpy( source, ExpandArg( fileName ) );
	path_set_extension( source, ".bsp" );
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );

	/* load the portal file */
	strcpy( portalfile, ExpandArg( fileName ) );
	path_set_extension( portalfile, ".prt" );
	Sys_Printf( "Loading %s\n", portalfile );
	LoadPortals( portalfile );

	/* ydnar: exit if no portals, hence no vis */
	if ( numportals == 0 ) {
		Sys_Printf( "No portals means no vis, exiting.\n" );
		return 0;
	}

	/* ydnar: for getting far plane */
	ParseEntities();

	/* inject command line parameters */
	InjectCommandLine( "-vis", argsToInject );
	UnparseEntities();

	if ( mergevis ) {
		MergeLeaves();
	}

	if ( mergevis || mergevisportals ) {
		MergeLeafPortals();
	}

	CountActivePortals();
	IndexActivePortals();

	Sys_Printf( "visdatasize:%zu\n", bspVisBytes.size() );

	CalcVis();

	/* write the bsp file */
	WriteBSPFile( source );

	// Preserve the retry input if BSP publication fails.
	if ( !saveprt ) {
		remove( portalfile );
	}

	return 0;
}
