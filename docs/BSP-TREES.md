# BSP block subdivision and depth limits

Automatic block cuts now divide the ordered interior grid boundaries around their
middle. The previous algorithm always selected the first boundary, creating a
linear chain on long or finely subdivided maps. A generated 1,024-block corridor
compiled into a tree 1,028 nodes deep, which the BSP reader correctly rejected.
The balanced output has depth 15 and retains the same interior cells.

The existing `_blocksize` / `blocksize` / `chopsize` settings keep their meanings.
Nonpositive components disable automatic cuts on that axis. Axis priority remains
X, Y, Z; existing face-plane scoring, hint priorities and detail rules remain.
No block boundary is removed and this does not reduce portal counts or implement
automatic regional VIS optimization. Among an even number of boundaries, the
lower middle is selected deterministically. Unrepresentable float boundaries fail
with a diagnostic instead of repeating a non-progressing split.

Reordering cuts can change node/plane/leaf IDs and serialized output on maps that
cross several boundaries. It is not a byte-compatibility option for those maps.
The validation compares cell geometry, rendered triangle XYZ/ST/normals and mapped
visibility, as appropriate. Ordinary fixtures requiring only one boundary per
axis retain native lump and PRT bytes against the preceding compiler.

## Shared generation, loading and publication limit

The maximum path remains **1,024 internal BSP nodes**, excluding leaves. Face-tree
generation rejects another split before adding a deeper node. BSP publication
checks the node graph before opening an output stream or changing byte order.
Loading performs the same check; `-force` does not bypass it.

An iterative traversal checks all components, including unreachable nodes, and
rejects invalid positive child indices and cycles. Cached subtree heights account
for shared children: merely skipping an already visited child, as the preceding
reader did, could miss a longer path. Traversal scratch is capped at 1,024 frames;
state/height arrays scale with the loaded node count. Leaf reference validation
remains part of the existing full BSP validator.

This limit does not bound all compiler resource use or every other recursive
algorithm. A face-driven tree can still need too many levels; a 1,025-hint-plane
fixture now fails cleanly and preserves prior BSP/SRF bytes. Existing startup
cleanup of PRT/LIN/REG sidecars is unchanged, so this is not a transactional
guarantee covering every output from a failed compile.

## Verification

```sh
ctest --test-dir build/release -R '^(bsp_tree|bsp_depth|bsp_validation|compiler_pipeline|game_profiles|patch_source|recovery_outputs|vis_merge_qualification|vis_reproducible)$' --output-on-failure -j 2
python tests/bsp_depth.py --compiler build/release/bin/q3mapx --reference path/to/15dbeac/q3mapx --work-dir build/release/tests/bsp-depth
```

Use `.exe` on Windows. The independent core oracle enumerates complete boundary
sets and checks balanced height, then compares graph depths against independent
relaxation on shuffled DAGs. Boundary controls include the exact accepted limit,
over-limit forward/reversed/shared paths, cycles and invalid child references.

Q3/JA native fixtures contain 128, 1,024 and 4,096 blocks, including negative
coordinates, each long axis and one/four workers. Exact cell boxes are derived
from serialized split planes and checked against the source room. Merged VIS
must retain every analytically visible pair. All fixture orientations also pass
minimap generation; the 1,024-block positive-X cases pass LIGHT. A pillar fixture
checks the exact free-space volume, clear sightlines and visibility at matching
world points against the preceding compiler. These are generated-data checks,
not engine screenshots or a general proof for arbitrary maps.

See [recorded validation](validation/bsp-depth.json). The broader portal/renderer
qualification gates and material-preview limits remain separate work.
