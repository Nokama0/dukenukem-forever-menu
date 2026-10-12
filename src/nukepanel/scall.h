#pragma once
// Script calls: UObject::FindFunction + ProcessEvent through the object's vtable slot 1 (docs/re/engine-map.md
// section 3), the engine's own event path. Game thread only. What each option calls, and why it cannot reach
// SaveConfig: docs/re/player-options.md.

// fn: the UFunction; parmsSize: its ParmsSize (UFunction +0x92), the size of the block scall_call copies.
struct ScallFn { void* fn; unsigned short parmsSize; int ok; };

// The class's own function (FindFunction with bGlobal=1: a state override is not seen), cached per (class, class
// name, name) and re-found when the cached UFunction leaves the object table. ok=0 if absent, the name included
// (a miss is cached too, until the class address holds another class); logged once per (class, name).
ScallFn scall_find(void* obj, const wchar_t* name);
// The function a script call on obj reaches now: FindFunction with bGlobal=0, so the current state's override comes
// first (AIActor.ControlX.CanBeUsedBy before AIActor.CanBeUsedBy), as the engine's own event calls find it. Not
// cached (the state changes) and nothing logged: the caller refuses once.
ScallFn scall_find_state(void* obj, const wchar_t* name);
// ProcessEvent(f.fn, parms, null). 1 when called. Refused (0): object or function not in this frame's snapshot,
// function class not in the object's class chain, engine not scriptable, or f.parmsSize over cap, parms' size in bytes
// (sizeof parms; 0 with no parms). Filled with scallparms_put at scall_parm's offsets; a return value lands there.
int scall_call(void* obj, const ScallFn& f, void* parms, unsigned cap);

// One parameter of f by reflection (Children/Next): its offset and size in the parms block, when its property
// class is named propClass (L"FloatProperty", L"NameProperty", ...), it is not an array and it fits in ParmsSize.
struct ScallParm { unsigned offset, size; };
int scall_parm(const ScallFn& f, const wchar_t* name, const wchar_t* propClass, ScallParm* out);
// Every parameter of f whose property class is propClass, by offset (declaration order), into out[0..cap); returns
// how many there are (more than cap: unexpected), -1 when a read faulted. For overrides that rename parameters
// (Actor.CanBeUsedBy's User is AIActor's TestPawn). Locals sit past ParmsSize and are not counted.
int scall_parms_of(const ScallFn& f, const wchar_t* propClass, ScallParm* out, int cap);
// Once per process, the first time a player exists: finds the functions the player options use on the player and
// logs each one's ParmsSize, flags and parameters (name, offset, size). Calls nothing.
void scall_selfcheck(void* player);
