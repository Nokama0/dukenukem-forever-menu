// Model-box glue (modelbox.h): an actor's placement and its mesh's BoundingBox read by reflection, inside the
// caller's access-violation guard. Read only, game thread. Offsets are cached per class until the next level, so a
// marker costs a few plain reads per frame on top of the Location read it already had.
#include <windows.h>
#include <string.h>
#include "modelbox.h"
#include "world.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "progress.h"   // pg_field_in
#include "log.h"

enum { P_LOCATION, P_ROTATION, P_DRAWSCALE, P_DRAWSCALE3D, P_PREPIVOT, P_POSTPIVOT, P_DRAWTYPE, P_STATICMESH,
       P_LOWERBY, P_LOWERAS, P_HEIGHT, P_RADIUS, P_COUNT };
// Actor (Engine.u); BoundingBox is Primitive's, which StaticMesh and DukeMesh (UMesh) inherit (Engine.dll reads it
// at +0x28 in UStaticMesh::GetRenderBoundingBox 0x102E14D0 and the UMesh constructor 0x1030CD40).
static const wchar_t* const kProps[P_COUNT] = { L"Location", L"Rotation", L"DrawScale", L"DrawScale3D", L"PrePivot",
                                                L"PostPivot", L"DrawType", L"StaticMesh",
                                                L"bLowerByCollision", L"bLowerAsPrePivot", L"CollisionHeight",
                                                L"CollisionRadius" };
static int s_n[P_COUNT], s_nBox;
enum { DT_STATICMESH = 8 };   // Engine.u Actor.EDrawType; AActor::GetRenderBoundingBox 0x101FDAF0

struct ClassSlot { const void* cls; int nameIdx; UProp p[P_COUNT]; };   // nameIdx: a reused address is another class
static ClassSlot s_cls[128];
struct MeshSlot { const void* cls; UProp box; };
static MeshSlot s_mesh[16];
static const void* s_logged[32];   // classes whose first box was logged this level
static int s_loggedN, s_frameLogs;   // 4 per modelbox_frame call: five callers a frame, uname_dlog holds 64 lines
static unsigned s_serial;
static unsigned s_static, s_noMesh, s_badBox, s_badPlace;

void modelbox_frame(void) {
  for (int k = 0; k < P_COUNT; ++k)
    if (!s_n[k]) s_n[k] = uname_find(kProps[k]);
  if (!s_nBox) s_nBox = uname_find(L"BoundingBox");
  const unsigned serial = world_level_serial();
  if (serial != s_serial) {   // class objects can be reused by the next level
    s_serial = serial;
    memset(s_cls, 0, sizeof s_cls);
    memset(s_mesh, 0, sizeof s_mesh);
    s_loggedN = 0;
  }
  s_frameLogs = 0;
}

static const ClassSlot& class_slot(const void* cls) {
  ClassSlot& s = s_cls[((uintptr_t)cls >> 4) % 128];
  const int nameIdx = uobj_name(cls);
  if (s.cls != cls || s.nameIdx != nameIdx) {
    s.cls = 0;   // a fault mid-fill leaves no half-filled slot behind
    for (int k = 0; k < P_COUNT; ++k) s.p[k] = uprop_get(cls, s_n[k]);
    s.nameIdx = nameIdx;
    s.cls = cls;
  }
  return s;
}

static const UProp& box_prop(const void* meshCls) {
  MeshSlot& s = s_mesh[((uintptr_t)meshCls >> 4) % 16];
  if (s.cls != meshCls) {
    s.cls = 0;
    s.box = uprop_get(meshCls, s_nBox);
    s.cls = meshCls;
  }
  return s.box;
}

static bool rd_vec(const void* o, const UProp& p, void* out) {
  if (!p.ok || p.type != UW_STRUCT || p.elemSize < 12 || !pg_field_in(p.offset, 12)) return false;
  memcpy(out, (const char*)o + p.offset, 12);
  return true;
}
static bool rd_float(const void* o, const UProp& p, float* out) {
  if (!p.ok || p.type != UW_FLOAT || !pg_field_in(p.offset, 4)) return false;
  memcpy(out, (const char*)o + p.offset, 4);
  return true;
}
static int rd_bool(const void* o, const UProp& p) {
  return p.ok && p.type == UW_BOOL && pg_field_in(p.offset, 4) && (uw_int(o, p.offset) & (int)p.bitmask) != 0;
}
static const void* rd_obj(const void* o, const UProp& p) {
  if (!p.ok || p.type != UW_OBJECT || !pg_field_in(p.offset, 4)) return 0;
  return uw_ptr(o, p.offset);
}

void modelbox_read(const void* o, const void* cls, MbCorners* out) {
  out->ok = 0;
  const ClassSlot& s = class_slot(cls);
  const UProp& dtp = s.p[P_DRAWTYPE];
  const int dt =
      dtp.ok && dtp.type == UW_BYTE && pg_field_in(dtp.offset, 1) ? *((const unsigned char*)o + dtp.offset) : -1;
  // Static meshes only: an animated mesh's render bounds come from its MeshInstance (AActor::GetRenderBoundingBox
  // 0x101FDAF0), not the mesh's BoundingBox through this placement, so those keep the cylinder.
  const void* mesh = dt == DT_STATICMESH ? rd_obj(o, s.p[P_STATICMESH]) : 0;
  if (!mesh || !uobj_valid(mesh)) { s_noMesh++; return; }
  const UProp& bp = box_prop(uobj_class(mesh));
  if (!bp.ok || bp.type != UW_STRUCT || bp.elemSize < 25 || !pg_field_in(bp.offset, 25)) { s_badBox++; return; }
  MbLocal b;
  memcpy(b.min, (const char*)mesh + bp.offset, 12);
  memcpy(b.max, (const char*)mesh + bp.offset + 12, 12);
  b.valid = *((const unsigned char*)mesh + bp.offset + 24);
  if (!mb_local_ok(b)) { s_badBox++; return; }
  MbPlace t = {};
  t.drawScale = 1.f;
  t.scale3d[0] = t.scale3d[1] = t.scale3d[2] = 1.f;
  if (!rd_vec(o, s.p[P_LOCATION], t.loc) || !rd_vec(o, s.p[P_ROTATION], t.rot) ||
      !rd_float(o, s.p[P_DRAWSCALE], &t.drawScale) || !rd_vec(o, s.p[P_DRAWSCALE3D], t.scale3d) ||
      !rd_vec(o, s.p[P_PREPIVOT], t.prePivot)) {
    s_badPlace++;
    return;
  }
  rd_vec(o, s.p[P_POSTPIVOT], t.postPivot);   // unresolved: none
  t.lowerByCollision = rd_bool(o, s.p[P_LOWERBY]);
  t.lowerAsPrePivot = rd_bool(o, s.p[P_LOWERAS]);
  rd_float(o, s.p[P_HEIGHT], &t.collisionHeight);
  *out = mb_corners(t, b);
  if (!out->ok) { s_badPlace++; return; }
  s_static++;
  bool seen = false;
  for (int i = 0; i < s_loggedN && !seen; ++i) seen = s_logged[i] == cls;
  if (!seen && s_loggedN < 32 && s_frameLogs < 4) {   // the gate's evidence: the model against its cylinder
    s_logged[s_loggedN++] = cls;
    s_frameLogs++;
    float r = 0.f;
    rd_float(o, s.p[P_RADIUS], &r);
    uname_dlog("modelbox: %s DrawType %d %s local (%.1f %.1f %.1f)..(%.1f %.1f %.1f) DrawScale %.2f x (%.2f %.2f %.2f)"
               " PrePivot (%.1f %.1f %.1f) lower %d/%d; cylinder r %.1f h %.1f", uname_tok(uobj_name(cls)), dt,
               uname_tok(uobj_name(mesh)), b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2], t.drawScale,
               t.scale3d[0], t.scale3d[1], t.scale3d[2], t.prePivot[0], t.prePivot[1], t.prePivot[2],
               t.lowerByCollision, t.lowerAsPrePivot, r, t.collisionHeight);
  }
}

void modelbox_log(void) {
  if (s_static || s_noMesh || s_badBox || s_badPlace)
    np_log("modelbox: reads static=%u; cylinder: no mesh %u, bad box %u, bad placement %u", s_static,
           s_noMesh, s_badBox, s_badPlace);
  s_static = s_noMesh = s_badBox = s_badPlace = 0;
}
