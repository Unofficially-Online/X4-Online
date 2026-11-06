/* The mining group's distance core, one copy for both gates (lockstep_region_camera.h's rc_mining_near on Windows,
 * linux/gate_mining_camera.h's on Linux), so the association cannot drift between platforms. The region object
 * search (Windows 0x7884a0, Linux 0x1f47b20) returns nothing when a camera is within 50 km of the requested
 * position; the group asks the same of the latched view points. f is the frame's transform and v the point's, both
 * GetTransformRelativeTo the cluster at the same time (translation, then three rows); pos is the requested position
 * in the frame. The point's offset from the frame, in the frame's rows, minus pos, squared: */
#ifndef LOCKSTEP_MINING_NEAR_H
#define LOCKSTEP_MINING_NEAR_H
#define RC_MINING_NEAR_SQ 2.5e9f /* (50 km)^2, as the native test compares */
static float rc_mining_dist2(const float f[16],const float v[16],const float pos[3]) {
    const float dx=v[0]-f[0],dy=v[1]-f[1],dz=v[2]-f[2];
    const float lx=dx*f[4]+dy*f[5]+dz*f[6]-pos[0],ly=dx*f[8]+dy*f[9]+dz*f[10]-pos[1],lz=dx*f[12]+dy*f[13]+dz*f[14]-pos[2];
    return lx*lx+ly*ly+lz*lz;
}
/* best: the smallest squared distance of the candidates, negative without one. */
static int rc_mining_is_near(float best) {return best>=0 && best<RC_MINING_NEAR_SQ;}
#endif
