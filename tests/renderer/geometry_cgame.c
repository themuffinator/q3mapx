/* SPDX-License-Identifier: GPL-3.0-or-later
 * Original camera/light fixture for the Quake III cgame ABI. Compile against
 * the reference engine's GPL-2.0-or-later headers; no gameplay/input automation.
 */
#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "renderercommon/tr_types.h"
#include "cgame/cg_public.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static intptr_t (QDECL *engine)(intptr_t,...);
static glconfig_t config;
void QDECL dllEntry(intptr_t (QDECL *syscall)(intptr_t,...)) { engine=syscall; }
static int float_bits(float value) { int bits; memcpy(&bits,&value,4); return bits; }
static void draw(void) {
    char buffer[256];
    float x=0,y=-220,z=208,pitch=35,yaw=90,fov=90;
    float lx=0,ly=0,lz=180,radius=0,red=1,green=0.4f,blue=0.2f;
    refdef_t view;
    engine(CG_CVAR_VARIABLESTRINGBUFFER,"q3mapx_camera",buffer,sizeof(buffer));
    sscanf(buffer,"%f %f %f %f %f %f",&x,&y,&z,&pitch,&yaw,&fov);
    engine(CG_CVAR_VARIABLESTRINGBUFFER,"q3mapx_light",buffer,sizeof(buffer));
    sscanf(buffer,"%f %f %f %f %f %f %f",&lx,&ly,&lz,&radius,&red,&green,&blue);
    memset(&view,0,sizeof(view));
    view.width=config.vidWidth; view.height=config.vidHeight;
    view.fov_x=fov;
    view.fov_y=2*atan(tan(fov*3.141592653589793/360)*view.height/view.width)*180/3.141592653589793;
    view.vieworg[0]=x; view.vieworg[1]=y; view.vieworg[2]=z;
    pitch*=3.141592653589793/180; yaw*=3.141592653589793/180;
    view.viewaxis[0][0]=cos(pitch)*cos(yaw); view.viewaxis[0][1]=cos(pitch)*sin(yaw); view.viewaxis[0][2]=-sin(pitch);
    view.viewaxis[1][0]=-sin(yaw); view.viewaxis[1][1]=cos(yaw);
    view.viewaxis[2][0]=sin(pitch)*cos(yaw); view.viewaxis[2][1]=sin(pitch)*sin(yaw); view.viewaxis[2][2]=cos(pitch);
    view.time=1000;
    engine(CG_R_CLEARSCENE);
    if(radius>0) {
        vec3_t origin={lx,ly,lz};
        engine(CG_R_ADDLIGHTTOSCENE,origin,float_bits(radius),float_bits(red),float_bits(green),float_bits(blue));
    }
    engine(CG_R_RENDERSCENE,&view);
}
intptr_t QDECL vmMain(int command,...) {
    switch(command) {
    case CG_INIT:
        engine(CG_PRINT,"q3mapx deterministic renderer fixture active\n");
        engine(CG_GETGLCONFIG,&config);
        engine(CG_R_LOADWORLDMAP,"maps/fixture.bsp");
        return 0;
    case CG_DRAW_ACTIVE_FRAME: draw(); return 0;
    case CG_CROSSHAIR_PLAYER: case CG_LAST_ATTACKER: return -1;
    default: return 0;
    }
}
