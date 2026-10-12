// Fullbright's probe (freecam.h fbp_*). While RendMap 6 holds, Actor.TraceEx from the camera (Duke ignored) names what
// the centre ray meets: the actor, its mesh and skin, the material and its class chain, the actor's lighting fields.
// Only a BumpShader (or a LayerMaterial over one) fills the G-buffer RendMap 6 leaves unlit. Game thread.
#include <windows.h>
#include <string.h>
#include <math.h>
#include "freecam.h"
#include "scall.h"
#include "scallparms.h"
#include "uobj.h"
#include "uprop.h"
#include "uname.h"
#include "uwalk.h"
#include "engine.h"
#include "world.h"
#include "aim.h"   // AIM_TF_*: STraceFlags in Engine.u's declaration order
#include "log.h"

// STraceHitResult in Engine.u's declaration order (Actor +0, Location +4, Normal +16, BSPSurfIndex +28, MeshBone +32,
// Texture +36, UV +40, Time +48; read with tools/ubc.py 2026-10-11). teleport.cpp reads +4/+16 the same way; the
// parameter's own size must cover Texture or nothing is read.
enum { HR_LOCATION = 4, HR_SURF = 28, HR_TEXTURE = 36, HR_NEED = 40 };
const float FBP_RAY_LEN = 65536.f;   // past any DNF level's far plane
const unsigned FBP_FLAGS = AIM_TF_ACTORS | AIM_TF_MESH | AIM_TF_TEXTURE;

static FbProbe s_p;
static unsigned s_samples, s_flight;
static int s_said;   // the trace's resolution, logged once a process

static int av_only(unsigned long code) {
  return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

void fbprobe_flight(void) {
  fbp_flight(&s_p);
  ++s_flight;
}

struct Hit { const void* actor; int surf; const void* mat; float at[3]; };

// 1 traced (h filled; actor 0: nothing met), 0 refused, -1 unresolved.
static int trace(void* player, const float start[3], const float end[3], Hit* h) {
  memset(h, 0, sizeof *h);
  h->surf = -1;
  const ScallFn f = scall_find(player, L"TraceEx");
  static const void* s_fn;
  static ScallParm s_start, s_end, s_flags, s_hr, s_ign, s_rv;
  if (!f.ok) return -1;
  if (f.fn != s_fn) {
    ScallParm a, b, fl, hr, ig, rv;
    if (!scall_parm(f, L"TraceStart", L"StructProperty", &a) || a.size != 12 ||
        !scall_parm(f, L"TraceEnd", L"StructProperty", &b) || b.size != 12 ||
        !scall_parm(f, L"TraceFlags", L"StructProperty", &fl) || fl.size != 4 ||
        !scall_parm(f, L"TraceHitResult", L"StructProperty", &hr) || hr.size < HR_NEED ||
        !scall_parm(f, L"IgnoreActor", L"ObjectProperty", &ig) || ig.size != 4 ||
        !scall_parm(f, L"ReturnValue", L"ObjectProperty", &rv) || rv.size != 4) {
      if (!s_said++) np_log("freecam: Fullbright probe: TraceEx's parameters did not resolve; no probe");
      return -1;
    }
    s_start = a; s_end = b; s_flags = fl; s_hr = hr; s_ign = ig; s_rv = rv;
    s_fn = f.fn;
    if (!s_said++)
      np_log("freecam: Fullbright probe: TraceEx ParmsSize %u, TraceHitResult +%u size %u (Texture at +%d), flags 0x%x",
             (unsigned)f.parmsSize, hr.offset, hr.size, HR_TEXTURE, FBP_FLAGS);
  }
  unsigned char parms[256];
  memset(parms, 0, sizeof parms);   // Extent 0: a line
  if (f.parmsSize > sizeof parms || !scallparms_put(parms, f.parmsSize, s_start.offset, start, 12) ||
      !scallparms_put(parms, f.parmsSize, s_end.offset, end, 12) ||
      !scallparms_put(parms, f.parmsSize, s_flags.offset, &FBP_FLAGS, 4) ||
      !scallparms_put(parms, f.parmsSize, s_ign.offset, &player, 4))
    return -1;
  if (!scall_call(player, f, parms, sizeof parms)) return 0;
  memcpy(&h->actor, parms + s_rv.offset, 4);   // read after the call that filled it
  if (!h->actor) return 1;
  memcpy(h->at, parms + s_hr.offset + HR_LOCATION, 12);
  memcpy(&h->surf, parms + s_hr.offset + HR_SURF, 4);
  memcpy(&h->mat, parms + s_hr.offset + HR_TEXTURE, 4);
  return 1;
}

// Name indices only inside the guard: uname_str takes the engine's names lock.
struct Seen {
  int actorCls, actorName, sm, mesh, skin, skinCls, mat, matCls[3], path;
  int unlit, noAmbient, lightTag, style, drawType, matUnlit;
};

enum { N_STATICMESH, N_MESH, N_SKINS, N_UNLIT, N_NOAMBIENT, N_LIGHTTAG, N_STYLE, N_DRAWTYPE, N_COUNT };
static int s_n[N_COUNT];
static const wchar_t* const kNames[N_COUNT] = { L"StaticMesh", L"Mesh", L"Skins", L"bUnlit", L"bNoAmbientLight",
                                                L"bUseLightTag", L"Style", L"DrawType" };
static const void* s_deferredCls[2];   // BumpShader, LayerMaterial: the materials D3DDrv9 draws into the G-buffer
static unsigned s_clsLevel;
static int s_noRender;                 // the name NoRenderGFX, 0 unresolved
static unsigned s_skipped;             // invisible hits passed, this process

static int name_of(const void* o) { return o && uobj_valid(o) ? uobj_name(o) : 0; }

static UProp prop(const void* cls, int k) {
  UProp p = { 0, 0, UW_OTHER, 0, 0, 0 };
  if (cls && s_n[k]) p = uprop_get(cls, s_n[k]);
  return p;
}
static int read_bool(const void* o, const void* cls, int k) {
  const UProp p = prop(cls, k);
  return p.ok && p.type == UW_BOOL && p.bitmask ? (uw_int(o, p.offset) & (int)p.bitmask) != 0 : -1;
}
static int read_byte(const void* o, const void* cls, int k) {
  const UProp p = prop(cls, k);
  return p.ok && p.type == UW_BYTE ? *((const unsigned char*)o + p.offset) : -1;
}
static const void* read_obj(const void* o, const void* cls, int k) {
  const UProp p = prop(cls, k);
  return p.ok && p.type == UW_OBJECT ? uw_ptr(o, p.offset) : 0;
}
// Skins[0]: a TArray {data, count}; the data span is checked before the read.
static const void* first_skin(const void* o, const void* cls) {
  const UProp p = prop(cls, N_SKINS);
  if (!p.ok || p.type != UW_ARRAY) return 0;
  const int n = uw_int(o, p.offset + 4);
  const void* const* data = (const void* const*)uw_ptr(o, p.offset);
  if (n <= 0 || n > 64 || !data || !engine_readable(data, 4)) return 0;
  return data[0];
}

static bool look(const Hit& h, Seen* s) {
  memset(s, 0, sizeof *s);
  s->path = FBP_NO_MATERIAL;
  s->unlit = s->noAmbient = s->lightTag = s->style = s->drawType = s->matUnlit = -1;
  __try {
    const void* cls = uobj_valid(h.actor) ? uobj_class(h.actor) : 0;
    if (!cls) return false;
    s->actorCls = uobj_name(cls);
    s->actorName = uobj_name(h.actor);
    s->sm = name_of(read_obj(h.actor, cls, N_STATICMESH));
    s->mesh = name_of(read_obj(h.actor, cls, N_MESH));
    const void* skin = first_skin(h.actor, cls);
    if (skin && uobj_valid(skin)) {
      s->skin = uobj_name(skin);
      s->skinCls = uobj_name(uobj_class(skin));
    }
    s->unlit = read_bool(h.actor, cls, N_UNLIT);
    s->noAmbient = read_bool(h.actor, cls, N_NOAMBIENT);
    s->lightTag = read_bool(h.actor, cls, N_LIGHTTAG);
    s->style = read_byte(h.actor, cls, N_STYLE);
    s->drawType = read_byte(h.actor, cls, N_DRAWTYPE);
    const void* m = h.mat && uobj_valid(h.mat) ? h.mat : skin && uobj_valid(skin) ? skin : 0;
    if (m) {
      s->mat = uobj_name(m);
      const void* mc = uobj_class(m);
      s->matUnlit = read_bool(m, mc, N_UNLIT);   // Texture.bUnlit; -1 on other material classes
      s->path = fbp_path(1, s_deferredCls[0] != 0, s_deferredCls[0] && uobj_class_isa(mc, s_deferredCls[0]),
                             s_deferredCls[1] != 0, s_deferredCls[1] && uobj_class_isa(mc, s_deferredCls[1]));
      for (int i = 0; i < 3 && mc; ++i, mc = uw_ptr(mc, US_SUPER)) s->matCls[i] = uobj_name(mc);
    }
    return true;
  } __except (av_only(GetExceptionCode())) {
    return false;
  }
}

static const wchar_t* nm(int idx) { return idx ? uname_str(idx) : L"-"; }

// The hit material's name index, 0 without one.
static int mat_name(const void* mat) {
  __try {
    return name_of(mat);
  } __except (av_only(GetExceptionCode())) {
    return 0;
  }
}

static const char* path_text(int path) {
  switch (path) {
    case FBP_DEFERRED: return "deferred (G-buffer, lit only by the passes RendMap 6 skips)";
    case FBP_FORWARD: return "forward (drawn straight to the frame with its own shading; RendMap 6 does not reach it)";
    case FBP_UNKNOWN: return "unknown (BumpShader or LayerMaterial did not resolve)";
    default: return "no material";
  }
}

void fbprobe_frame(void* player, int active, const FreecamPose& pose) {
  if (!player || !fbp_due(&s_p, active, GetTickCount())) return;
  const unsigned level = world_level_serial();
  if (level != s_clsLevel) {
    s_clsLevel = level;
    for (int k = 0; k < N_COUNT; ++k) s_n[k] = uname_find(kNames[k]);
    static const wchar_t* const kCls[2] = { L"BumpShader", L"LayerMaterial" };
    s_deferredCls[0] = s_deferredCls[1] = 0;
    uobj_find_classes(kCls, s_deferredCls, 2);
    s_noRender = uname_find(L"NoRenderGFX");
  }
  ++s_samples;
  float f[3], r[3], u[3], end[3];
  freecam_axes(pose, f, r, u);
  for (int k = 0; k < 3; ++k) end[k] = pose.eye[k] + f[k] * FBP_RAY_LEN;
  Hit h;
  float start[3] = { pose.eye[0], pose.eye[1], pose.eye[2] };
  int skips = 0;
  for (;;) {
    if (trace(player, start, end, &h) <= 0) return;
    if (!h.actor || !fbp_skip(mat_name(h.mat), s_noRender, skips)) break;
    fbp_past(h.at, f, start);
    ++skips;
    ++s_skipped;
  }
  if (!fbp_take(&s_p, h.actor, h.surf, h.mat)) return;
  if (!h.actor) {
    np_log("freecam: Fullbright probe %d (flight %u, sample %u): nothing within %.0f uu, past %d invisible collision "
           "hit(s) (%u this session)", s_p.lines, s_flight, s_samples, FBP_RAY_LEN, skips, s_skipped);
    return;
  }
  Seen s;
  if (!look(h, &s)) {
    np_log("freecam: Fullbright probe %d (flight %u, sample %u): the hit actor %p did not read", s_p.lines, s_flight,
           s_samples, h.actor);
    return;
  }
  float d = 0.f;
  for (int k = 0; k < 3; ++k) d += (h.at[k] - pose.eye[k]) * (h.at[k] - pose.eye[k]);
  np_log("freecam: Fullbright probe %d (flight %u, sample %u): %ls %ls at %.0f uu, BSP surface %d; StaticMesh %ls, "
         "Mesh %ls, Skins[0] %ls (%ls); material %ls (%ls < %ls < %ls): %s; actor bUnlit %d bNoAmbientLight %d "
         "bUseLightTag %d Style %d DrawType %d; material bUnlit %d; past %d invisible collision hit(s)", s_p.lines,
         s_flight, s_samples, nm(s.actorCls), nm(s.actorName), d > 0.f ? sqrtf(d) : 0.f, h.surf, nm(s.sm), nm(s.mesh),
         nm(s.skin), nm(s.skinCls), nm(s.mat), nm(s.matCls[0]), nm(s.matCls[1]), nm(s.matCls[2]), path_text(s.path),
         s.unlit, s.noAmbient, s.lightTag, s.style, s.drawType, s.matUnlit, skips);
}
