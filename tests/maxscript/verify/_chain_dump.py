"""_chain_dump.py — diagnostic: read a .alo and print each bone's
encoded parent-local TM AND the world TM the engine would reconstruct
by composing the parent chain. Not a verifier (no pass/fail). Phase
14c investigation tool for #84 follow-up.

Usage:
    python3 _chain_dump.py <file.alo>
    python3 _chain_dump.py <file.alo> <bone_name_filter>     # only print bones whose name contains the filter

The composition uses row-vector convention matching alo_build.cpp's
encode_matrix3 (rows = local axes in world, row 3 = translation),
which matches Mike Lankamp's importer convention per format-notes.md.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def mat_mul(a, b):
    """Multiply two 4x3 row-vector matrices a * b (i.e. apply b after a).

    Both a and b are flat 12-float lists laid out on disk per
    encode_matrix3() in alo_build.cpp:838-849:
      matrix[0..3]   = (X.x, Y.x, Z.x, T.x)   <- first  written row
      matrix[4..7]   = (X.y, Y.y, Z.y, T.y)   <- second written row
      matrix[8..11]  = (X.z, Y.z, Z.z, T.z)   <- third  written row

    The bone's rotation-row basis vectors (rows of the Matrix3 in Max
    row-vector convention) are:
      local +X axis = (matrix[0], matrix[4], matrix[8])
      local +Y axis = (matrix[1], matrix[5], matrix[9])
      local +Z axis = (matrix[2], matrix[6], matrix[10])
      translation  = (matrix[3], matrix[7], matrix[11])

    This matches Mike Lankamp's [c[1],c[5],c[9]]...[c[4],c[8],c[12]]
    decoder. See docs/format-notes.md:107.

    For row-vector composition `child_world = child_local * parent_world`:
      transforming a direction d by parent_world means:
        d' = d.x * parent.row0 + d.y * parent.row1 + d.z * parent.row2
      transforming a point p (translation row) means:
        p' = d' + parent.translation

    where parent.row0 = (parent[0], parent[4], parent[8]),
          parent.row1 = (parent[1], parent[5], parent[9]),
          parent.row2 = (parent[2], parent[6], parent[10]),
          parent.translation = (parent[3], parent[7], parent[11]).
    """
    # a's rotation rows (as basis vectors in parent's local frame)
    a_x = (a[0], a[4], a[8])
    a_y = (a[1], a[5], a[9])
    a_z = (a[2], a[6], a[10])
    a_t = (a[3], a[7], a[11])   # a's translation (point in parent's frame)

    # b's rotation rows + translation
    b_x = (b[0], b[4], b[8])
    b_y = (b[1], b[5], b[9])
    b_z = (b[2], b[6], b[10])
    b_t = (b[3], b[7], b[11])

    def xform_dir(v):
        """Apply b's rotation to a direction v (no translation contribution)."""
        return (v[0]*b_x[0] + v[1]*b_y[0] + v[2]*b_z[0],
                v[0]*b_x[1] + v[1]*b_y[1] + v[2]*b_z[1],
                v[0]*b_x[2] + v[1]*b_y[2] + v[2]*b_z[2])

    out_x = xform_dir(a_x)
    out_y = xform_dir(a_y)
    out_z = xform_dir(a_z)
    out_t_rot = xform_dir(a_t)
    out_t = (out_t_rot[0] + b_t[0],
             out_t_rot[1] + b_t[1],
             out_t_rot[2] + b_t[2])

    return [
        out_x[0], out_y[0], out_z[0], out_t[0],
        out_x[1], out_y[1], out_z[1], out_t[1],
        out_x[2], out_y[2], out_z[2], out_t[2],
    ]


IDENTITY = [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0]


def compose_chain(bones, idx):
    """Walk parent chain from `idx` up to Root and compose all matrices.

    Returns the bone's world TM as a flat 12-element list.
    Composition order: world = self_local * parent_local * grandparent_local
                              * ... * top_local (row-vector convention).
    """
    chain = []
    cur = idx
    visited = set()
    while cur != 0xFFFFFFFF and cur not in visited and cur < len(bones):
        visited.add(cur)
        chain.append(cur)
        cur = bones[cur].parent_index
    # Compose from top-of-chain down to self. After this loop, acc
    # holds the world TM of self.
    acc = IDENTITY[:]
    for c in reversed(chain):
        acc = mat_mul(bones[c].matrix, acc)
    return acc


def fmt_vec3(v):
    return f"({v[0]:9.3f}, {v[1]:9.3f}, {v[2]:9.3f})"


def main():
    if len(sys.argv) < 2:
        print("usage: _chain_dump.py <file.alo> [bone_name_filter]")
        return 1
    path = sys.argv[1]
    filt = sys.argv[2] if len(sys.argv) >= 3 else None
    a = _alo.load(path)
    print(f"== {path} ({len(a.bones)} bones) ==")
    print(f"{'idx':>3}  {'name':<22}  {'parent':<22}  {'local_t':<32}  {'world_t':<32}")
    for i, b in enumerate(a.bones):
        if filt is not None and filt.lower() not in b.name.lower():
            continue
        parent_name = (a.bones[b.parent_index].name
                       if b.parent_index != 0xFFFFFFFF and b.parent_index < len(a.bones)
                       else "<root-sentinel>")
        local_t = b.translation
        world = compose_chain(a.bones, i)
        world_t = (world[3], world[7], world[11])
        print(f"[{i:3d}]  {b.name:<22}  [{b.parent_index:3d}] {parent_name:<17}  "
              f"{fmt_vec3(local_t):<32}  {fmt_vec3(world_t):<32}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
