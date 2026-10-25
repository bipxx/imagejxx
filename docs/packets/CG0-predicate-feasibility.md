# CG0 G-A: exact predicates and output feasibility

**2026-10-25 amendment:** this review evaluates the proposed exact-native
profile, not the minimum universal editor contract. The
[efficiency review](EXECUTION_EFFICIENCY_REVIEW.md#required-product-behavior-versus-optional-guarantees)
requires comparing a documented reference-compatible continuous profile and
eligible existing engines before choosing bespoke exact arithmetic. Its
blockers do not block independent settings, XYZ, filtering or workflow work.

Prepared 2026-10-24 by Agent A. **Source review and proposed decision, not an
algorithm qualification or dependency approval.** The only change made by this
task is this document. No library was cloned, installed, compiled or executed;
no native numerical result was produced. CFG1 inputs remain held for C.

This supplements [CG0's proposed contracts](CG0-continuous-geometry-decisions.md)
and [the three-agent delivery specifications](THREE_AGENT_DELIVERY_SPECS.md).
It does not freeze their public API, default limits, error allowance or later
creation/workflow versions.

**Recommended next decision:** keep G-A open for product implementation. Select
the pinned CGAL Bezier traits below for a **source-only exact-reference
feasibility packet**. Retain an independently authored, budgeted exact
polynomial kernel as the native candidate. First resolve the pair-certificate,
coincidence and binary64 reconstruction obligations in sections 3–6. Neither
interval subdivision alone nor a float32 library meets the proposed profile.
An exact library does not by itself solve the output or resource contract.

## 1. Corrections needed before freezing G-A

| Issue | Concrete decision proposed |
| --- | --- |
| Exact input topology versus rounded output | Require two certificates: the exact selected boundary arrangement, and the topology of the actual emitted binary64 path. A displacement bound alone is insufficient. Refuse an uncertifiable reconstruction; never publish a path whose small error opens a contact or removes a narrow face. |
| Degree elevation/subdivision | Exact coincidence applies to the supplied binary64 words interpreted as dyadics. A construction that rounded is not automatically coincident with its parent. Keep exact and rounded-nearby test rows separate. |
| Zero resultant | It identifies a common algebraic component in an elimination problem, not a proved shared real span inside both parameter intervals. Require component/parameter-range analysis, including disjoint portions of the same supporting curve. |
| Tangencies and cusps | Root count plus multiplicity is not the incident-branch order. Require a certified ordering on both sides of an event, including zero first derivatives and coincident branches. |
| Generic exact library | Its caches, algebraic arithmetic, allocation failures and cancellation must meet our per-operation contract before product incorporation. A wrapper that only checks limits before/after a library call does not establish that contract. |
| Meaning of success | CG1 may refuse when its declared finite limits or binary64 representation certificate cannot be met. It may not quietly use a tolerance merge, curve fitting, polygon flattening or another precision profile. Mandatory easy/exact cases must have named success expectations; universal refusal is not acceptance. |

Local contracts remain unchanged: `ImageJCompositePath` owns M/L/Q/C/Z doubles;
the core publicly requires C++17 (`CMakeLists.txt:81`); existing grid Boolean
commands and NativePathV4 remain unchanged. No public exact/algebraic curve
carrier, dependency type or subprocess appears in the proposed SDK.

## 2. Candidate comparison, with exact source identities

### CGAL: exact-reference candidate; product admission unresolved

The official immutable release **6.2.1**, tag `v6.2.1`, resolves to commit
`28811b671a12b5caa9e3688569dadbc6b3728fe6`. This is a browsed source identity,
not a locally verified archive hash. C must record actual acquired-file hashes
before a future executable reference run. [Release and commit](https://github.com/CGAL/cgal/releases/tag/v6.2.1)

`Arr_Bezier_curve_traits_2` accepts rational control points and uses algebraic
events, with filtering followed by exact computation. Thus exact dyadic input
does not require binary32 conversion. It supports original Bezier curves of
arbitrary degree; CG1 would still admit only L/Q/C. Its documented number-traits
instantiation is `CORE_algebraic_number_traits` with rational/algebraic Cartesian
kernels. [Traits contract](https://doc.cgal.org/latest/Arrangement_on_surface_2/classCGAL_1_1Arr__Bezier__curve__traits__2.html)

The Boolean package's Bezier example establishes a concrete composition with
`Gps_traits_2` and general polygons. Its operations are regularized, which agrees
with excluding isolated line/point output. However, its polygon input contract
requires relatively simple boundaries; it does not directly accept our raw
self-crossing even-odd path soup. An arrangement with per-operand parity is still
needed before using that polygon interface. [Boolean package and Bezier example](https://doc.cgal.org/latest/Boolean_set_operations_2/index.html)

The pinned source has actual exact-event machinery, not only a general accuracy
claim. `Bezier_cache.h` stores paired algebraic parameters and coordinates;
`_intersection_params` uses resultants, and the self-intersection branch removes
the diagonal factor. This supplies a useful review target but does not establish
all our degenerate cases without execution. [Pinned cache](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/Arrangement_on_surface_2/include/CGAL/Arr_geometry_traits/Bezier_cache.h)

`Bezier_x_monotone_2.h::_intersect` has an explicit overlapping-subcurve result
and endpoint-range cases. Its `supporting_curve()` preserves provenance, but
`parameter_range()` returns approximate doubles. That accessor must not be used
as an exact split certificate. [Pinned subcurve implementation](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/Arrangement_on_surface_2/include/CGAL/Arr_geometry_traits/Bezier_x_monotone_2.h)

The following source seams need a bounded adapter review:

| Requirement | Available seam / remaining work |
| --- | --- |
| Exact event and source parameters | Pinned `Point_2::make_exact(Bezier_cache&)`, `get_originator(curve,xid)`, `has_parameter()` and `parameter()` provide a possible source-level route. Check every originating curve after exactification; do not assume all lazy originators have parameters. These extra methods are not the complete documented stable interface. A pinned reference adapter may use them; an SDK may not expose them. [Point implementation](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/Arrangement_on_surface_2/include/CGAL/Arr_geometry_traits/Bezier_point_2.h) |
| Ordinary API access | Documented `x()`/`y()` require an exactly computed point; `approximate()` is insufficient evidence. [Point API](https://doc.cgal.org/latest/Arrangement_on_surface_2/classCGAL_1_1Arr__Bezier__curve__traits__2_1_1Point__2.html) |
| Work/cancellation | Trait functors have no operation-budget/cancellation argument. Traits allocate caches with `new`; collection and algebra operations occur within calls. An outer cancellation poll cannot interrupt those inner operations. [Pinned trait owner](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/Arrangement_on_surface_2/include/CGAL/Arr_Bezier_curve_traits_2.h) |
| Allocation accounting | CORE's `MemoryPool` uses global `operator new` and thread-local/static pools. Per-operation retained/scratch accounting cannot be inferred from a facade allocator. [Pinned pool](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/CGAL_Core/include/CGAL/CORE/MemoryPool.h) |
| Exact arithmetic backend | Pinned `BigInt.h` has Boost `mpz_int` and `cpp_int` branches selected by configuration. Record the actual branch and all transitive versions; neither the old source comment nor a top-level package name proves the chosen backend. [Pinned integer backend](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/CGAL_Core/include/CGAL/CORE/BigInt.h) |
| Recoverable bounded allocation | GMP's documented custom allocators cannot return failure; throwing a C++ exception or `longjmp` has undefined results. They are also shared allocation hooks. Do not use them as the core's recoverable per-job cap. A different backend still needs a complete audit. [GMP allocation contract](https://gmplib.org/manual/Custom-Allocation) |

CGAL 6.2.1 documents C++17, CMake >=3.22 and Boost >=1.74; its tested Linux GCC
floor is 13.3, and Windows support includes MSVC 17.14. Existing host compiler
compatibility is **not executed here**; C must compare the active caches rather
than silently upgrade them. Qt is an optional viewer dependency and is not part
of this proposed reference. [Toolchain and dependencies](https://doc.cgal.org/latest/Manual/thirdparty.html)

The exact selected packages have redistribution obligations: arrangements and
Boolean sets are GPLv3-or-later/commercial-license packages; CGAL_Core is
LGPLv3-or-later. The licenses are not noncommercial restrictions, but they cannot
be presented as an MIT-only dependency. Record package/backend licenses, notices,
source/linking obligations and the intended distribution before incorporation.
No purchase, license acceptance or incorporation is authorized by this review.
[Arrangement license](https://raw.githubusercontent.com/CGAL/cgal/v6.2.1/Arrangement_on_surface_2/package_info/Arrangement_on_surface_2/license.txt),
[Boolean package license](https://raw.githubusercontent.com/CGAL/cgal/v6.2.1/Boolean_set_operations_2/package_info/Boolean_set_operations_2/license.txt),
[CORE license](https://raw.githubusercontent.com/CGAL/cgal/28811b671a12b5caa9e3688569dadbc6b3728fe6/CGAL_Core/package_info/CGAL_Core/license.txt),
[CGAL licensing policy](https://www.cgal.org/license.html)

### lib2geom: binary64 storage, but not the required certificate

The official GitHub repository says it moved to GitLab; its archival branch is
not the active source. Use GitLab release **1.4**, commit
`23316118755def8156c268f767894c145130e07d`, as the reviewed comparison identity.
The tag's commit link was observed; raw files at that full hash were readable.
No archive was acquired. [Mirror notice](https://github.com/inkscape/lib2geom),
[official tags](https://gitlab.com/inkscape/lib2geom/-/tags)

The pinned `Coord` is `double`, with `EPSILON=1e-6`; the Boolean graph accepts a
precision parameter. It implements even-odd path operations, so its public
shape model is relevant. These facts do not prove exact dyadic predicates.
[Coordinate type](https://gitlab.com/inkscape/lib2geom/-/raw/23316118755def8156c268f767894c145130e07d/include/2geom/coord.h),
[graph API](https://gitlab.com/inkscape/lib2geom/-/raw/23316118755def8156c268f767894c145130e07d/include/2geom/intersection-graph.h)

More decisively, the pinned graph obtains finite-precision intersection lists,
classifies edges using sampled winding points, and marks some overlapping
intersection patterns defective. It also allocates internal graph nodes with
ordinary `new`. These are not our exact coincidence/branch-order or budget
certificates. Changing the supplied precision to zero does not add those
proofs. [Pinned implementation](https://gitlab.com/inkscape/lib2geom/-/raw/23316118755def8156c268f767894c145130e07d/src/2geom/intersection-graph.cpp)

Release 1.4 already requires C++20, so this is not merely a concern about current
master. Its build enables optional GPL code by default and requires GSL/GLib/
double-conversion; the library target publicly links those dependencies. The
dual LGPL2.1/MPL1.1 file notices therefore do not describe the complete built
closure. GSL itself has GPL distribution terms. Do not assume disabling one
CMake switch removes every GPL dependency. [Pinned build](https://gitlab.com/inkscape/lib2geom/-/raw/1.4/CMakeLists.txt),
[link closure](https://gitlab.com/inkscape/lib2geom/-/raw/23316118755def8156c268f767894c145130e07d/src/2geom/CMakeLists.txt),
[GSL policy](https://www.gnu.org/software/gsl/)

**Disposition:** useful design/reference comparison, not the selected exact
dependency or a silent replacement for G-A. The current-master README adds an
explicit degeneracy caveat; that is separate from the above pinned-source
finding. [Current README](https://gitlab.com/inkscape/lib2geom/-/raw/master/README.md)

### Skia PathOps: incompatible public precision for this profile

Skia's public `SkScalar` is `float`. `SkPath` uses that scalar for its L/Q/C
geometry, so transferring arbitrary native doubles loses information before
Boolean processing. Internal higher-precision arithmetic cannot restore those
input words. [Scalar definition](https://api.skia.org/SkScalar_8h.html),
[path API](https://api.skia.org/classSkPath.html)

`Op` also permits reducing cubic/quadratic degree and has no budget/cancellation
parameter. Its useful Boolean functionality is not a substitute for D06's
original-degree/provenance contract. [PathOps contract](https://api.skia.org/SkPathOps_8h.html)

**Disposition:** reject as the implementation of `native_binary64_evenodd_v1`.
No build/version pin or transitive closure was selected because the API already
fails the precision requirement. The top-level license permits use/redistribution
subject to notices; that does not qualify a dependency closure or authorize a
different float32 profile. [License](https://raw.githubusercontent.com/google/skia/main/LICENSE)

## 3. Concrete independently authored exact route

This is a proposed mathematical construction, not copied implementation and not
a claim that its missing modules are already proven. The small degree bounds
make it a credible bounded research packet; they do not reduce it to a few
orientation tests.

1. Decode each finite double word into its exact signed integer and power-of-two
   scale. Construct integer/rational power-basis coefficients of each L/Q/C.
   Remove a leading term only when it is exactly zero. Keep original degree and
   original words separately for output. Constant and collinear parameterizations
   get explicit branches; do not pass a zero polynomial to a generic root solver.
2. For a pair form `F(s,t)=xA(s)-xB(t)` and `G(s,t)=yA(s)-yB(t)` over rationals.
   A bounded interval filter may reject boxes or certify simple isolated roots.
   Every unresolved box goes to exact algebra, not an epsilon threshold.
3. For isolated events, compute exact elimination polynomials, square-free
   factors and isolating rational intervals. Certify parameter pairing by the
   original two equations using subresultants/real-algebraic sign evaluation;
   matching overlapping coordinate boxes is insufficient. Preserve multiplicity
   information separately from square-free root identity.
4. For a self-pair remove the exact `s-t` diagonal before searching distinct
   parameters, then enforce `0<=s<t<=1`. A shared endpoint does not authorize
   dropping another event. Detect cusps/critical parameters separately.
5. For common components, compute an exact common-factor/component relation,
   restrict its real branches to `[0,1]^2`, and return parameter correspondences
   for maximal overlap spans. Constant/collinear curves may traverse a segment
   repeatedly; split into monotone parameter branches and retain multiplicity
   for per-operand parity. A fast affine reparameterization identity check is
   sufficient when proved, but cannot be the fallback for every zero resultant.
6. Sort events and incident branches with exact algebraic comparisons and local
   sidedness certificates. Ordinary tangent determinants vanish at tangencies;
   derivative sign probing needs the first nonzero term and a certified local
   interval, or an exact critical-fiber analysis. Order coincident groups before
   propagating per-operand even-odd face labels.

These are recognized exact-algebra operations: the CGAL algebraic-kernel
documentation distinguishes univariate root isolation/sign-at-root from
bivariate solving, critical fibers and common-factor decomposition. It is a
primary specification reference, **not proof of an independent implementation**.
[Algebraic-kernel contracts](https://doc.cgal.org/latest/Algebraic_kernel_d/index.html)

The minimum missing native modules are explicit:

| Module | Required result / proof obligation | Current feasibility status |
| --- | --- | --- |
| Charged integer/rational backend | Exact add/multiply/divide/GCD, pre-admitted growth, work counted by operand limb sizes, cancellation inside long loops. | No backend selected or implemented. Plain double expansions alone do not supply general division/root algebra. |
| Root identity and pairing | Exact root intervals plus a proved parameter-pair identity; repeated roots, endpoint roots and distinct parameters at one point retained. | Concrete resultant/Sturm/subresultant route identified; implementation and all degeneracy rules missing. |
| Positive-dimensional overlap | Real common components and parameter intervals, including reversal, degree elevation and repeated collinear traversal. | Bivariate component analysis or a proved complete degree<=3 specialization required. Identical-control shortcuts alone are incomplete. |
| Incident branch order | Exact cyclic order, including cusps and tangencies, sufficient for face traversal. | Missing. A scalar multiplicity field does not replace it. |
| Rounded output validation | Certificate described below for the actual doubles and selected topology. | Missing; no inspected dependency supplies this whole contract. |

A safe private solver result distinguishes `disjoint`, `isolated_events` and
`overlap_spans`, with exact parameter provenance and budget statistics. Resource,
cancellation and unsupported-certificate failures must be separate refusal
statuses/exceptions, never an empty event list. These are private design
requirements, not new public API declarations.

## 4. Binary64 output is a separate gate

Compute exact restricted Bezier controls at the certified parameters, then
enclose and round them with a specified tie policy. An exact parameter must not
first be rounded to double: two distinct parameters can round to the same
double even when their physical points are distinguishable. Reuse one emitted
vertex word pair for all incident output fragments, retaining the map to the
exact event and every source interval.

The proposed `64*u(S)` bound in the earlier card can be checked using enclosures
of the entire restricted polynomial and its emitted controls. It remains an
**upper error allowance**, not a sufficient topology test or a promise of
successful reconstruction for every input. Exact unchanged fragments retain
their input words and zero error.

Before publication, require:

- distinct necessary vertices remain distinct, with the required parameter and
  cyclic order; shared events use exactly shared binary64 endpoints;
- the emitted boundary has the selected arrangement's incidence, boundary
  traversal and face adjacency, allowing only explicitly recorded extra degree-2
  subdivision vertices;
- nonincident emitted fragments have no new crossings or contacts; incident
  fragments preserve the intended touching/crossing relation;
- every emitted fragment has a source-interval provenance and a certified
  displacement bound, and all closed traversals and empty results are valid.

One concrete certifier is to run the same exact predicates on the emitted
dyadic curves and compare the resulting boundary graph against the selected
exact graph/provenance map. That is additional charged work and storage, not a
free validation pass. It avoids assuming that metric closeness preserves
topology. A future cheaper certificate needs a proof covering the same cases.

If rounding fails this check, deterministic further subdivision may be tried
within the frozen limits while preserving source degree. It cannot be assumed
to cure every exact tangency/cusp. Return a visible representation/certificate
refusal when it does not. Do not preserve a hidden exact graph while publishing
a double path that contradicts it, or introduce an algebraic path carrier under
the existing type name.

## 5. Exact adversarial rows for the feasibility packet

These are analytic fixture specifications, **not observed results**. They enter
G-A before the larger CG-01..10 area-operation matrix. `L`, `Q`, `C` below include
the start point; all listed decimal coordinates are exact dyadics.

| ID | Literal input and independent requirement |
| --- | --- |
| GA-01 | `L[(0,0),(6,6)]` with `L[(0,6),(6,0)]`: one event `(3,3)`, parameters `(1/2,1/2)`, transverse branch order. |
| GA-02 | `Q[(-2,4),(0,-4),(2,4)]` with `L[(-2,0),(2,0)]`: one event `(0,0)` at both halves. The quadratic satisfies `y=x^2`; this is a double-root tangency, not two approximate crossings. |
| GA-03 | `C[(3,-1),(-1,1),(-1,-1),(3,1)]`: with `u=2t-1`, `x=3u^2,y=u^3`. The cusp is at `t=1/2`; line `y=0` contact requires the zero-derivative branch case. |
| GA-04 | `Q[(0,0),(3,3),(6,0)]` and `C[(0,0),(2,2),(4,2),(6,0)]`: exactly equal degree-elevated curves; repeat reversed and with the half restriction `Q[(0,0),(1.5,1.5),(3,1.5)]`. The overlap map is exact, not proximity. |
| GA-05 | The GA-04 quadratic with `Q[(12,-12),(15,-21),(18,-36)]`: the second is the same polynomial support at parameter `s=t+2`, but the admitted arcs are disjoint. A zero resultant must not become a full overlap. |
| GA-06 | `Q[(0,0),(4,0),(0,0)]` traverses `[0,2]` twice, with a turn at `t=1/2`. Pair with a collinear subsegment and then use the closed curve as an area operand: parameter multiplicity must survive, while its area is empty. |
| GA-07 | `C[(3,-2.25),(-1,3.25),(-1,-3.25),(3,2.25)]`: `x=3u^2,y=3(u^3-u/4)`. Distinct self-parameters `1/4,3/4` meet at `(3/4,0)`. Do not discard both through the diagonal or merge their branch identities. |
| GA-08 | GA-02 quadratic with horizontal line `y=2^-120`, endpoints `x=-2,2`: two events `x=+-2^-60`, with quadratic parameters `1/2+-2^-62`. Both parameters round to `1/2` in binary64; exact restriction must not collapse the events. Repeat line `y=-2^-120`: no real events. |
| GA-09 | Rectangle `[1,1+2^-40] x [0,1]`: positive-width native area whose two x values collapse under float32 conversion. Test identity/containment and a real cut through it; no precision fallback. |
| GA-10 | A dyadic box with width/height `2^-1074` and an ordinary disjoint box: positive exact area although its floating product underflows. Preserve it or give the specifically frozen representation/resource refusal; never report empty from double area arithmetic. The unchanged dyadic identity subcase must succeed. |
| GA-11 | Elevate `Q[(0,0),(1,1),(2,0)]` using rounded binary64 `2/3` and `4/3` controls. It is not the same exact polynomial as the supplied Q. Contrast with GA-04; test classification, not a tolerance equality assertion. |
| GA-12 | Construct a proposed rounded-output contact and perturb one incident endpoint by one ULP. Even if both candidates satisfy the metric bound, the graph validator must reject the one with a new gap/crossing. C supplies exact topology expectations independently. |

Add per-module one-under work/storage bounds and cancellation during arithmetic,
root refinement and output validation. Refusal must leave all input words and
owners unchanged and invoke no user callback. These controls are not replaced
by a watchdog killing a reference child process.

## 6. Bounded implementation and reference dispatch

The native candidate can satisfy C++17 and deterministic accounting only if its
integer/polynomial loops and allocations are controlled. Charge result growth
before arithmetic or allocation, including temporaries, old-plus-new buffers,
root/certificate storage, input descriptors and output. One multiplication of
two huge integers is not one work unit. Poll cancellation within bounded limb
blocks and root-refinement steps. Do not change global rounding or allocation
hooks. The existing quiet inherited-scope policy still applies.

The inspected CGAL/CORE code does not provide those controls as an off-the-shelf
product integration. A complete allocator/cancellation adaptation, or a
different exact backend, is a separate engineering and eligibility decision.
Switching to Boost multiprecision may address one backend issue but does not
automatically remove trait caches or CORE pools. Boost's license is a separate
dependency obligation, not proof of bounded behavior. [Boost license](https://www.boost.org/LICENSE_1_0.txt)

Proposed next packet **GA-R0**, with exactly A/B/C:

| Owner | Bounded deliverable and exit |
| --- | --- |
| A | Finish the degree<=3 pair-certificate specification: exact number representation, complete common-component branch or explicit mathematical blocker, sidedness certificate, rounded-output graph equivalence and counted arithmetic cost model. Do not start document/UI Boolean consumers. |
| C | Freeze the CGAL commit above as a source/reference candidate, review the exact backend/license closure and source-level exactification seam. If separately dispatched, own one new reference observer, proposed `tests/reference/ObserveContinuousBezierPredicates.cpp`, and independently authored GA-01..12 inputs/reader. Use no product expected-value helper. |
| B | Review refusal and empty-result UX contracts read-only, preserving NativePathV4 Replace-only behavior. No alternate geometry engine or new UI implementation while G-A is open. |

For a later approved observer, send at most two primitives and their binary64
words per request. Convert those words directly to exact rationals. Emit actual
event kind, source IDs, exact/algebraic parameter representations, isolating
rational intervals, overlap provenance and explicit exactification status.
Do not label `approximate()` or `parameter_range()` output an exact certificate.
If the selected source seam cannot expose a necessary parameter, report that
specific limitation instead of fabricating it from a double.

The reference can use an isolated child process with explicit time/output/RSS
limits so a pathological library call cannot consume unbounded observation
resources. This is reference containment only, not evidence of the native
core's per-operation cap/cancellation. C owns acquisition, exact file hashes,
toolchain comparison, the single active cache/runtime and the shared 8GiB
storage ledger. No whole repository/dependency archive is required as evidence.

GA-R0 exits only with the exact-reference seam and analytic rows reviewed, plus
a written decision on each missing native module. A source pin or passing
reference rows alone does not close G-A. The current concrete blockers are
complete coincidence/branch ordering, recoverable bounded exact arithmetic,
and the emitted-binary64 topology certificate. A narrower line-only packet may
be proposed separately, but cannot be reported as complete CG1 curve algebra.

No public version is reserved by this review. The proposed CG1 allowlist and
CG2 creation/workflow decisions remain gated; historical masks, codecs,
fingerprints and unavailable evidence remain unchanged.
