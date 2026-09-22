# Fuzzing the mesh builder

Two libFuzzer harnesses, both over `cjelly_model_mesh_from_obj()` — the code
that turns a parsed OBJ document into vertex and index buffers. The parse
itself belongs to Ghoti.io Model and is fuzzed there; what these cover is
everything CJelly does afterwards: range checking the face indices, fan
triangulation, generating normals for a file that has none, the V flip, and
the bounding box.

    make fuzz                      # both, 60s each
    make fuzz-run-mesh FUZZ_TIME=3600
    make fuzz-run-mesh-struct FUZZ_TIME=3600

## The two harnesses, and why there are two

`fuzz_mesh` feeds bytes to the OBJ parser and passes whatever comes out to the
mesh builder. Everything it tests is reachable from a file, so a finding here
needs no argument about whether it matters.

`fuzz_mesh_struct` builds the `GMDL_Obj` itself and skips the parser. It gives
up that guarantee deliberately. `cjelly_model_mesh_from_obj()` is public API,
and a consumer of a generic framework can arrive with a document built from
glTF, a procedural generator, or a network message — the OBJ parser is not a
gate this function sits behind. Model's own header says the same thing about
its output: *a file may still name an element that does not exist ... a
consumer that indexes the arrays directly must range check first.* CJelly is
that consumer.

So the struct harness reaches states the parser will not produce: a face whose
`count` claims corners it never stored, an index of `INT32_MIN`, a position of
`inf`. What it will **not** do is lie about its own arrays — every count
matches an allocation of exactly that many elements. A `GMDL_Obj` whose
`vertex_count` exceeds its `vertices` array is a broken caller, and a crash
from one would be the harness's bug rather than the library's.

## What it found

Both defects were in the bounding box, and both were reachable from an
ordinary OBJ file, which the struct harness only made quicker to reach.

**The centre of a finite box could be outside it.** `(min + max) * 0.5f`
overflows when the bounds are large and share a sign: three vertices at
`-3.4e38` — legal OBJ text — gave `bounds_min == bounds_max == -3.4e38` and a
centre of `-inf`, with the radius following it to infinity. A caller framing a
camera on the bounding sphere got no usable numbers from a file that parsed
perfectly.

**The first fix was wrong, and the struct harness said so in ninety seconds.**
Halving each bound before adding them — `0.5f*min + 0.5f*max` — cannot
overflow, but it underflows: half of the smallest denormal is zero, so a box
sitting entirely just above zero got a centre just below it. The committed
form picks by sign, because neither spelling is safe at both ends.

That second finding is the argument for the harness better than the first one
is. The first defect could have been reasoned about. The second was in a fix
that had just been written, tested, and believed.

## Seeds

`corpus/` holds hand-written seeds only, and each is a case worth keeping: a
cube, a mesh with no normals, mixed `v/vt/vn` reference forms, out-of-range
and negative indices, non-finite positions, and the two numeric extremes
above. libFuzzer writes what it discovers to `build/<build>-fuzz/corpus/`
instead — a campaign adds thousands of machine-named files, and they are
reproducible from these seeds in the time it takes to read them.

Crashes land in `build/<build>-fuzz/` rather than the working directory.
