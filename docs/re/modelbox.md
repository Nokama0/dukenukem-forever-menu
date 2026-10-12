# Marker boxes from the model, not the collision cylinder (build 244)

An actor's collision cylinder (`CollisionRadius`, `CollisionHeight`, clamped 8..128 UU) is a poor marker box for
the item, crate, quest, breakable and ego markers: for pickups it is a flat disc far wider than the model that never
turns with it, and a wide disc near the camera at a screen edge stretches into a long strip. The engine's own render bounds
are the mesh's local `Primitive.BoundingBox` placed by `AActor::LocalToWorldCoords`; the panel projects those
8 corners (modelbox.h) and keeps the cylinder only when they do not resolve. Verified in game. Only DrawType 8
(static mesh) actors get a model box; DrawType 2 (`DukeMesh`) actors keep the cylinder.

Method: Engine.dll in Ghidra (plate comments saved), the class defaults, and StaticMeshes.dat read with Python.

## Cylinder against model (class defaults vs the mesh's BoundingBox in StaticMeshes.dat)

| class | cylinder r / h (UU) | model (DrawType, mesh, size X x Y x Z UU) |
|---|---|---|
| `ShotgunPickup` | 32 / 4 (a 64-wide disc) | 8 `Weapon_Pickups.ShotGun_Static`, 26.5 x 2.3 x 8.3 |
| `PipeBombPickup` | 15 / 6 | 8 `PipeBomb_Static`, 11.3 x 4.4 x 4.4 |
| `PistolAmmoPickup` | 15 / 1 | 8 `Pistol_Clip.Pistol_AmmoPickup`, 8.8 x 1.3 x 2.6 |
| `RPGPickup` | 22 / 6, `PrePivot` (10.5, 0, 6) | 8 `RPG_Static`, 37.2 x 19.7 x 8.9, off-centre (PrePivot recentres it) |
| `Containers_Crates_ItemCrate` | 16 / 12 | 8 `Crate.ammoCrate`, 24 x 32 x 24 |
| `Containers_Crates_Destructible` | 25 / 25 | 8 `Crate.Wooden_Crate_Breakable`, 48 cube |
| `Barrels_Generic` (explosive barrels) | 16 / 24 | 8 `Barrels.Barrel_Generic`, 31 x 31 x 47 (the cylinder fits) |
| `Electronics_Generic_Computer_Monitor_Black_A_Wall` | 14 / 15 | 8 `comp_monitor_wallmounted` 7.4 x 27.4 x 24.5, at `DrawScale` 2 in Map02 |
| `Containers_Crates_InfiniteAmmo`, urinals, `BatteryPickup` | 54 / 23, 14 / 24, 20 / 15 | 2 (`DukeMesh`); not boxed from the model: the cylinder stays |

StaticMeshes.dat entries (offsets from StaticMeshDir.dat) start `01 00 00 00 04 00 00 00` then the FBox (Min, Max,
IsValid). Read with Python, read only.

## How the engine places it (Ghidra MCP, Engine.dll, plate comments saved)

- `AActor::GetRenderBoundingBox` `0x101FDAF0`: DrawType 8 (`DT_StaticMesh`) -> `StaticMesh` (+0x2DC) vt+0x68;
  DrawType 2 (`DT_Mesh`) -> the MeshInstance (a dnSkinMeshInstance AABB, not reflected); DrawType 1 a sprite square.
- `UStaticMesh::GetRenderBoundingBox` `0x102E14D0`: the local box at UPrimitive+0x28 through the owner's vt+0x140
  (`AActor::LocalToWorldCoords` `0x101FE010`, slot checked in the AActor vtable beside GetMeshInstance at +0x198),
  8 corners -> a world AABB. `UMesh` (`DukeMesh`'s base) is a UPrimitive too (constructor `0x1030CD40`).
- `LocalToWorldCoords`: world = Location + PostPivot + R(Rotation) * (DrawScale * DrawScale3D * (local - PrePivot)).
  `bLowerByCollision`: with `bLowerAsPrePivot` PrePivot.Z += CollisionHeight / (DrawScale3D.Z * DrawScale), else
  PostPivot.Z -= CollisionHeight. Offsets in the code: Location 0x1EC (matches uprop's log), Rotation 0x1F8,
  PrePivot 0x28C, PostPivot 0x298, DrawScale 0x2A4, DrawScale3D 0x2A8; the panel resolves all of them by name.
- The panel keeps the box oriented (8 projected corners) rather than the engine's world AABB, so a turned model is
  not widened by its own diagonal. A model with any corner behind the camera gets no box, as a pawn does.

## Log lines

- `modelbox: <Class> DrawType .. local (..)..(..) ...; cylinder r .. h ..` once per class per level, and
  `modelbox: reads static=..; cylinder: no mesh .., bad box .., bad placement ..` every 600 frames.
- A static-mesh pickup with `bad box` would mean the BoundingBox is not filled at runtime; boxes offset from the
  models on screen would mean the placement rule is wrong. Neither is seen.
