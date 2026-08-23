"""Extract the component tree from an ASCII STEP file (AP214/AP242).
Usage: python step_tree.py <file.step>
Prints: product list with occurrence counts, and the parent->child hierarchy.
"""
import sys, re, collections

path = sys.argv[1]
txt = open(path, encoding="utf-8", errors="replace").read()

# STEP entities can span lines; normalize
data = re.sub(r"\s*\n\s*", " ", txt)

# products: #id = PRODUCT('name','desc',...)
products = {}
for m in re.finditer(r"#(\d+)\s*=\s*PRODUCT\s*\(\s*'([^']*)'", data):
    products[m.group(1)] = m.group(2)

# product definitions -> product (via PRODUCT_DEFINITION_FORMATION -> PRODUCT)
pdf2prod = {}
for m in re.finditer(r"#(\d+)\s*=\s*PRODUCT_DEFINITION_FORMATION[^(]*\(\s*'[^']*'\s*,\s*(?:'[^']*'|\$)\s*,\s*#(\d+)", data):
    pdf2prod[m.group(1)] = m.group(2)
pd2prod = {}
for m in re.finditer(r"#(\d+)\s*=\s*PRODUCT_DEFINITION\s*\(\s*'[^']*'\s*,\s*(?:'[^']*'|\$)\s*,\s*#(\d+)", data):
    pd = m.group(1); form = m.group(2)
    if form in pdf2prod:
        pd2prod[pd] = pdf2prod[form]

def pname(pd):
    return products.get(pd2prod.get(pd, ""), f"pd#{pd}")

# assembly occurrences: NEXT_ASSEMBLY_USAGE_OCCURRENCE('id','name','desc',#parentPD,#childPD,$)
children = collections.defaultdict(list)
occ_names = collections.Counter()
for m in re.finditer(r"NEXT_ASSEMBLY_USAGE_OCCURRENCE\s*\(\s*'[^']*'\s*,\s*'([^']*)'\s*,\s*(?:'[^']*'|\$)\s*,\s*#(\d+)\s*,\s*#(\d+)", data):
    occ, parent, child = m.group(1), m.group(2), m.group(3)
    children[parent].append(child)
    occ_names[pname(child)] += 1

print(f"file: {path}")
print(f"products: {len(products)}   assembly occurrences: {sum(occ_names.values())}")
print()
if occ_names:
    print("=== part counts (by product name) ===")
    for name, n in occ_names.most_common():
        print(f"{n:3d} x {name}")
    print()
    # hierarchy from roots (parents that are never children)
    childset = {c for cs in children.values() for c in cs}
    roots = [p for p in children if p not in childset]
    print("=== hierarchy ===")
    def walk(pd, depth, seen):
        cnt = collections.Counter(children.get(pd, []))
        for ch, n in cnt.items():
            label = pname(ch)
            mult = f" x{n}" if n > 1 else ""
            print("  " * depth + f"- {label}{mult}")
            if ch not in seen and depth < 8:
                walk(ch, depth + 1, seen | {ch})
    for r in roots:
        print(f"{pname(r)}")
        walk(r, 1, {r})
else:
    print("(single part — products:)")
    for i, name in list(products.items())[:20]:
        print(f"  {name}")
