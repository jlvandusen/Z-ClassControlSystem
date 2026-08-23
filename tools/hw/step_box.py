#!/usr/bin/env python3
"""step_box.py — write a minimal STEP (AP214) rectangular solid, x0..x1 / y0..y1 / z0..z1 in mm.
   python step_box.py out.step NAME x0 y0 z0 x1 y1 z1
Used for collision envelopes (plug-in modules, mating plugs, fuse, cable housings) in the Fusion fit-check model."""
import sys

def box(name, x0, y0, z0, x1, y1, z1):
    E = []
    def add(s):
        E.append(s); return len(E)
    # product skeleton
    ctx = add("APPLICATION_CONTEXT('automotive design')")
    add(f"APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#{ctx})")
    pc = add(f"PRODUCT_CONTEXT('',#{ctx},'mechanical')")
    prod = add(f"PRODUCT('{name}','{name}','',(#{pc}))")
    pdf = add(f"PRODUCT_DEFINITION_FORMATION('','',#{prod})")
    pdc = add(f"PRODUCT_DEFINITION_CONTEXT('part definition',#{ctx},'design')")
    pd = add(f"PRODUCT_DEFINITION('design','',#{pdf},#{pdc})")
    pds = add(f"PRODUCT_DEFINITION_SHAPE('','',#{pd})")
    # geometry helpers
    def pt(x, y, z): return add(f"CARTESIAN_POINT('',({x:.4f},{y:.4f},{z:.4f}))")
    def dr(x, y, z): return add(f"DIRECTION('',({x:.1f},{y:.1f},{z:.1f}))")
    def vtx(p): return add(f"VERTEX_POINT('',#{p})")
    corners = {}
    for i in range(8):
        x = x1 if i & 1 else x0; y = y1 if i & 2 else y0; z = z1 if i & 4 else z0
        corners[i] = (x, y, z)
    V = {i: vtx(pt(*corners[i])) for i in range(8)}
    edges = {}
    def edge(a, b):
        key = (min(a, b), max(a, b))
        if key in edges: return edges[key], a == key[0]
        pa, pb = corners[key[0]], corners[key[1]]
        d = tuple(pb[k] - pa[k] for k in range(3)); L = sum(c * c for c in d) ** 0.5
        p = pt(*pa); v = add(f"VECTOR('',#{dr(*[c / L for c in d])},1.)")
        ln = add(f"LINE('',#{p},#{v})")
        ec = add(f"EDGE_CURVE('',#{V[key[0]]},#{V[key[1]]},#{ln},.T.)")
        edges[key] = ec; return ec, a == key[0]
    faces = []
    # (loop of 4 corner ids, outward normal) — loops CCW seen from outside
    F = [((0, 2, 3, 1), (0, 0, -1)), ((4, 5, 7, 6), (0, 0, 1)),
         ((0, 1, 5, 4), (0, -1, 0)), ((2, 6, 7, 3), (0, 1, 0)),
         ((0, 4, 6, 2), (-1, 0, 0)), ((1, 3, 7, 5), (1, 0, 0))]
    for loop, n in F:
        oes = []
        for k in range(4):
            a, b = loop[k], loop[(k + 1) % 4]
            ec, fwd = edge(a, b)
            oes.append(add(f"ORIENTED_EDGE('',*,*,#{ec},{'.T.' if fwd else '.F.'})"))
        el = add(f"EDGE_LOOP('',({','.join('#%d' % o for o in oes)}))")
        fb = add(f"FACE_OUTER_BOUND('',#{el},.T.)")
        origin = pt(*corners[loop[0]])
        ref = (1, 0, 0) if n[0] == 0 else (0, 1, 0)
        ax = add(f"AXIS2_PLACEMENT_3D('',#{origin},#{dr(*n)},#{dr(*ref)})")
        pl = add(f"PLANE('',#{ax})")
        faces.append(add(f"ADVANCED_FACE('',(#{fb}),#{pl},.T.)"))
    shell = add(f"CLOSED_SHELL('',({','.join('#%d' % f for f in faces)}))")
    solid = add(f"MANIFOLD_SOLID_BREP('{name}',#{shell})")
    o = pt(0, 0, 0); z = dr(0, 0, 1); x = dr(1, 0, 0)
    place = add(f"AXIS2_PLACEMENT_3D('',#{o},#{z},#{x})")
    lu = add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    au = add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    su = add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    unc = add(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-06),#{lu},'distance_accuracy_value','')")
    gc = add(f"( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{unc})) GLOBAL_UNIT_ASSIGNED_CONTEXT((#{lu},#{au},#{su})) REPRESENTATION_CONTEXT('','') )")
    rep = add(f"ADVANCED_BREP_SHAPE_REPRESENTATION('{name}',(#{place},#{solid}),#{gc})")
    add(f"SHAPE_DEFINITION_REPRESENTATION(#{pds},#{rep})")
    body = "\n".join(f"#{i + 1}={s};" for i, s in enumerate(E))
    return ("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('envelope box'),'2;1');\n"
            f"FILE_NAME('{name}.step','2026-08-23T00:00:00',('Z-Class'),(''),'step_box.py','','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n" + body + "\nENDSEC;\nEND-ISO-10303-21;\n")

if __name__ == "__main__":
    out, name = sys.argv[1], sys.argv[2]; x0, y0, z0, x1, y1, z1 = map(float, sys.argv[3:9])
    open(out, "w", encoding="ascii").write(box(name, x0, y0, z0, x1, y1, z1)); print("wrote", out)
