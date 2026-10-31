# E3 heroine reconstruction delta

`e3-model.n50p` contains resource differences for the optional E3 1997 heroine
reconstruction. It contains no complete original BOX archive or reference
screenshots. The supported English, French, German and Italian releases use
the same source BOX (SHA-256
`164ed4127922940be5f41b663a41767f11b30ee71648587dceab33450908cb9f`).

The installer places the delta beside the game executable. Before the first
native archive open, the patch verifies the source and builds a resource map
and a small model-data overlay containing only changed/new bytes. The native
resource table is extended after retail initialization. Unchanged resources
and Original-model aliases read the installed BOX directly; changed resources
open the overlay through the game's own CRT. No second BOX is created.
The original archive remains read-only. A failed reconstruction leaves it intact.
The default model is Original; Setup → Native 50 can switch to E3 immediately.

`tools/pack_e3_model.py` generates the delta from a reference BOX and the tested
dual-model reconstruction. Repeated resource blocks are shared losslessly.
`tools/share_e3_chains.py` separates exactly matching torso/hip metalwork into
two shared native meshes reused by all 60 injury/team sets. It also merges
identical position/normal vertices and clears only native initialization caches.
Skin, wound polygons, UVs, normals and materials are preserved. Both chains
warm once before node rendering and follow the current bone's native transform,
including detached parts; Original never draws them.
`tools/model_pack_test.cc` verifies bounds, invalid inputs, cache reuse/recovery
and byte equality with every resource in the tested reconstruction.

The reconstruction follows surviving E3 references; it does not claim recovery
of the original model's unknown topology.
