// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "q3map2.h"

// Share the exact evaluation order between source preflight and emitted vertices.
inline Vector2 BrushTextureCoordinates( const side_t& side, const Vector3& position,
                                       const Vector3& texX, const Vector3& texY,
                                       const shaderInfo_t& shader ){
	Vector2 st;
	if ( g_brushType == EBrushType::Bp ) {
		const float x = vector3_dot( position, texX );
		const float y = vector3_dot( position, texY );
		st[0] = side.texMat[0][0] * x + side.texMat[0][1] * y + side.texMat[0][2];
		st[1] = side.texMat[1][0] * x + side.texMat[1][1] * y + side.texMat[1][2];
	}
	else {
		st[0] = side.vecs[0][3] + vector3_dot( side.vecs[0].vec3(), position );
		st[1] = side.vecs[1][3] + vector3_dot( side.vecs[1].vec3(), position );
		st[0] /= shader.shaderWidth;
		st[1] /= shader.shaderHeight;
	}
	return st;
}
