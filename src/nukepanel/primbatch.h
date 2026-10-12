#pragma once
// Triangle queue bookkeeping for prim_d3d9.cpp. One draw call per run of triangles sharing a
// texture; a texture change or a full buffer means the queued run is drawn first.
enum PrimBatchResult { PB_QUEUED, PB_FLUSH_THEN_QUEUE };

struct PrimBatch {
  int cap;     // vertices
  int count;   // vertices queued
  void* tex;   // texture of the queued run
};

inline void primbatch_init(PrimBatch* b, int capVerts) {
  b->cap = capVerts - capVerts % 3;
  b->count = 0;
  b->tex = 0;
}

// Accounts for one triangle with `tex`. On PB_FLUSH_THEN_QUEUE the caller draws the run it had
// (its count and texture as they were before this call); the new triangle then sits at 0..2.
inline PrimBatchResult primbatch_add_tri(PrimBatch* b, void* tex) {
  if (b->count > 0 && (tex != b->tex || b->count + 3 > b->cap)) {
    b->count = 3;
    b->tex = tex;
    return PB_FLUSH_THEN_QUEUE;
  }
  b->count += 3;
  b->tex = tex;
  return PB_QUEUED;
}

// Empties the queue (after the caller drew it).
inline void primbatch_reset(PrimBatch* b) { b->count = 0; }
