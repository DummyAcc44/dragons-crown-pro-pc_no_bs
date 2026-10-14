# Graphics correctness (`feature/graphics-correctness`)

## Symptoms and root cause

Observed on the first-boot build: flicker, rectangular stale patches in the opening movie and over the title smoke, and a
missing glyph in `Create save data?` (`Cre te`).

All of them came from one defect in the draw-preparation workers (`vk_draw_prep.cpp`, a GOW3-lineage optimisation, not
upstream shadPS4). A worker prepares each draw ahead of the GPU thread, including its vertex inputs. The V# of every vertex
stream is read from guest memory behind a user-data pointer (`Info::ReadUdReg`), which is **not** part of the flattened user
data the GPU thread compares before it trusts a prepared draw. When a worker read that table before the command stream had
written it, the draw used a stale vertex buffer base, so quads were drawn from the wrong vertices: ghost rectangles of old
content, a missing glyph quad, and flicker.

## Evidence

Window captures (game window only) and a luminance/frame-difference burst on the title screen:

| Run | Result |
|---|---|
| default | rectangles, frame diff avg 6.8 |
| `GOW3_PREP_WORKERS=0` | clean, 0.35 |
| `GOW3_VK_RECORD_THREAD=0`, `GOW3_DRAW_PIPE=0`, `GOW3_PRESENT_THREAD=0` (each alone) | unchanged (8.2, 8.3, 6.6) |
| fully serial | clean, 0.45 |

Presentation, recording thread and draw-pipe synchronisation were therefore ruled out. A temporary check comparing prepared
data with the GPU thread's own showed 0 pipeline-key mismatches in 55,000 draws and vertex-input mismatches within the first
few thousand draws, always in `V# 0` base address with identical size, stride and format.

## Fix

`Rasterizer::ResolveVertexBuffers` (`gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`) re-reads the V# of each
attribute on the GPU thread and uses the prepared inputs only when they are identical; otherwise it takes the regular path.
Workers stay enabled. After the fix: 0 mismatches over 80,000 sets, frame diff 0.29–0.45, and the save prompt, CRIWARE logo,
opening movie and title screen render cleanly.

The same code exists unchanged in gow3_pc, medievil_pc, tlg_pc, gr2_pc, iss_pc and bloodborne_pc_windows. Whether other
reported artifacts there are the same defect is unproven.

## Not pursued

Deferred image readback, full-sync A/B, image-cache and movie-path audits: the evidence above made them unnecessary. The
Vulkan startup warnings about partially supported formats are driver capabilities and were not linked to any artifact.
