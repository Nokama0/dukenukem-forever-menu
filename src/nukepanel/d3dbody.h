#pragma once
// Whether d3dcap detours a body read from a COM vtable. Reset and Present must be d3d9.dll's own code: they carry the
// state block's lifetime. CreateDevice is hooked wherever it is (an overlay may replace the IDirect3D9 slot), and its
// detour reads the out-parameter only after the real call returns.
enum { D3DM_CREATEDEVICE = 0, D3DM_RESET = 1, D3DM_PRESENT = 2, D3DM_CLEAR = 3, D3DM_TRACE = 4 };
enum { D3DB_HOOK = 0, D3DB_HOOK_LOGGED = 1, D3DB_REFUSE = 2 };

// haveBody: the slot was readable and non-NULL; inD3d9: the body lies in d3d9.dll's image.
inline int d3dbody_verdict(int method, bool haveBody, bool inD3d9) {
  if (!haveBody) return D3DB_REFUSE;
  if (inD3d9) return D3DB_HOOK;
  return method == D3DM_CREATEDEVICE ? D3DB_HOOK_LOGGED : D3DB_REFUSE;
}
