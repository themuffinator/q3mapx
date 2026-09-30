"""Matched first-party VIS workloads. SPDX-License-Identifier: GPL-3.0-or-later."""
from fixtures import box, create_fixture


def create_vis_fixture(root, *, grid=5, detail=False):
    source = create_fixture(root, dense=True, patch=False, grid=grid)
    if detail:
        text = source.read_text()
        limit = grid // 2 * 128
        for y in range(-limit, limit+1, 128):
            for x in range(-limit, limit+1, 128):
                brush = box((x-20, y-20, 0), (x+20, y+20, 80+(x+y) % 112))
                assert text.count(brush) == 1
                text = text.replace(brush, brush.replace(' 0 0 0\n', ' 134217728 0 0\n'))
        source.write_text(text)
    return source
