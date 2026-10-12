#pragma once
// Properties by name (docs/re/engine-map.md): the class chain's Children/Next walked for a dnName, the
// result cached per (class, name) until the next level. Game thread only.

struct UProp { int ok; int offset; int type; int elemSize; unsigned bitmask; const void* field; };

// Classes the resolver needs: UProperty (fields that are properties) once found by name.
void uprop_set_property_class(const void* propertyClass);
// The property `nameIdx` on `cls` or one of its supers. Logs `uprop: <Class>.<Prop> +0x.. <type>` the first
// time each property resolves this level, and a miss once per (class, name).
UProp uprop_get(const void* cls, int nameIdx);
// A new level: forget the cache and the logged set.
void uprop_reset(void);
