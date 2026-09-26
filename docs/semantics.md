@page cjelly_semantics Semantic model


**Status:** Design. Nothing here is implemented yet.

CJelly draws every pixel itself and embeds no native controls, so nothing
about what the interface *means* is available to anyone outside the process
unless CJelly says it. A screen reader cannot ask Windows what that rectangle
is, because as far as Windows is concerned there is no control there - only a
swapchain.

This document settles how that meaning is modelled, before the widget work
starts, because the expensive parts to retrofit are decisions rather than
code.

It exists because of three directions CJelly is aimed at - a game engine, an
application framework, and a web browser - which pull on this in different
ways. The design below is the one that serves all three.

---

## 1. ARIA is the native vocabulary

Roles, states and properties follow [WAI-ARIA](https://www.w3.org/TR/wai-aria/).
Not as an influence: the internal vocabulary *is* ARIA, plus a small set of
internal roles that never project outward, and the platform accessibility
APIs are things CJelly projects to.

The reasoning is the browser direction. A browser has to implement ARIA
because web content uses it, so an internal model in any other vocabulary
means translating ARIA into it and back out again to three platform APIs. The
other two directions lose nothing by using it: ARIA's role set was drawn from
desktop accessibility in the first place, and W3C publishes the mapping from
it to every platform API as a specification of its own - the
[Core Accessibility API Mappings](https://www.w3.org/TR/core-aam/), with a
table per platform for UIA, ATK/AT-SPI2, IAccessible2 and the macOS AX API.
That is an implementable artifact, not a hope that the terms line up.

The internal roles are the ones every implementation ends up needing and ARIA
has no word for, because ARIA describes elements and a tree also has to
describe the pieces of them: a run of static text, an inline text box that a
character range resolves to, a generic container that exists only to hold
children. Chromium's role enumeration is exactly this shape - ARIA's roles
plus `kStaticText`, `kInlineTextBox`, `kGenericContainer` and a handful of
others - and it is what the platform layers there project from.

It also suits building this from scratch. ARIA is a published specification
with no runtime, no library, and nothing to link.

### Platform projections

| Platform | API | Transport | Notes |
| --- | --- | --- | --- |
| Windows | UI Automation | COM | COM from C is vtables; no framework needed |
| Linux | AT-SPI2 | D-Bus | wants a D-Bus client - a sibling library, not part of CJelly |
| macOS | NSAccessibility | Objective-C | needs an Objective-C translation unit |

None of these belong in the core library. They are optional modules, absent by
default, and the D-Bus one in particular should be its own repository on the
pattern already used for `compress`, `image` and `model` - it is useful well
beyond accessibility.

One trade-off to record on Windows: NVDA and JAWS still lean on the older
IAccessible2 interface for browsers, and Chromium exposes both. For a new
toolkit, UIA alone is the defensible choice - Narrator is UIA-only and NVDA
and JAWS both support it - but it is a choice, and if the browser direction
gets far enough for screen-reader users to notice the difference, IA2 is the
second projection to add.

**Threading.** UIA provider calls can arrive on threads other than the UI
thread; AT-SPI2 calls arrive on whichever thread dispatches D-Bus; macOS
expects the main thread. The tree is owned by the UI thread, and every bridge
marshals onto it. Chromium does the same. This is stated here so that no
bridge is ever written to read the tree from wherever it happens to be
called.

---

## 2. Two trees, one identity space

There are two consumers with genuinely different needs, and merging them
damages both.

**The semantic tree** is deliberately lossy. Decorative elements do not
appear. A composite of six drawn parts is one node. A virtualised list reports
ten thousand items with twenty realised. It is stable, user-facing, and a
contract with assistive technology: it answers *what does this mean?*

**The introspection tree** is exhaustive and unstable. Every layout box, every
cached layer, invalidation reasons, timings, draw-call counts. It is
developer-facing and free to change shape release to release: it answers *what
is the machine doing?*

Conflating them fails in both directions. Implementation detail leaks into
what a screen reader announces, and decorative nodes cannot be hidden because
the debugger needs to see them; meanwhile the debugger is constrained to be
semantic and stable, so it cannot show what is actually wanted.

Browsers ship both, separately, and that is the precedent to follow: the
Chrome DevTools Protocol has DOM, CSS, LayerTree, DOMSnapshot and Overlay
domains for debugging, and a distinct Accessibility domain that surfaces the
accessibility tree *to* the debugger.

**Node identity is shared between them.** Every element carries an id that is
stable across frames and across regeneration of the interface, and both trees
key on it. That is what makes it possible to ask "this accessible node - which
layout boxes is it?", and it is what lets a test harness choose its level:
semantic queries for durable tests ("invoke the button named Save"),
introspection queries for white-box assertions ("that layer was not
re-rasterised").

### The id scheme is the hard part

The id is an opaque 64-bit value, and the requirement on it is only that the
same element produces the same value across frames and regenerations. How it
is produced differs by direction, and neither scheme can be assumed by the
tree:

- **Immediate-mode** UI has no retained objects to attach an id to. Dear ImGui
  derives ids by hashing the widget's label into an id stack that the caller
  pushes and pops, so the same call site with the same label yields the same
  id every frame. That is the scheme the game direction wants.
- **Markup** has author-supplied keys (section 5), with structural position as
  the fallback where the author gave none.
- **Retained widgets** simply allocate one at construction.

The tree keys on the value and does not care where it came from. Getting this
wrong looks like an id that is unique but not stable, which is the common
failure and which breaks every consumer at once.

### The test harness is a consumer, not a third tree

A harness that drives CJelly reads these two trees and injects events through
the existing dispatch seam (`cj_window__dispatch_key_callback` and its
siblings in `window_internal.h`). It does not need a model of its own.

Worth knowing: on Windows the accessibility API *is* the automation API.
Microsoft named it UI Automation because it was built for screen readers and
test tooling as one thing. Qt's position is the same: generic GUI test tools
reach a Qt application through the platform accessibility bridge that
`QAccessible` feeds, which is why Qt's documentation frames accessibility as
what makes automated testing possible at all. Building the semantic tree is
building most of the harness.

---

## 3. The semantic tree is computed, never adopted

It is not the widget tree with extra fields, and it is not the markup.

The browser world has the clearest demonstration of why. `visibility:hidden`
is in the DOM and absent from the accessibility tree. `aria-hidden` is in the
DOM and in layout, and absent from the accessibility tree. `role="presentation"`
erases an element's semantics while keeping its children. A `<video>` is one
element and becomes a whole cluster of accessible controls. And CSS generated
content - `::before` and `::after` - is in no DOM at all, yet it *is* in the
accessibility tree and screen readers announce it; the `content: "★" / "star"`
alternative-text syntax exists precisely because it is announced.

So the accessibility tree is neither a subset nor a superset of the document -
it is a projection, computed by rules. Building it as a decorated copy of
whatever tree is convenient works until the first of these cases, and then it
is unpicked rather than fixed.

So: a computation step from the source tree (widgets, or parsed markup) to the
semantic tree, with the projection rules in one place.

---

## 4. Materialised lazily, and only when something is listening

The game direction wants per-frame immediate-mode UI, no retained allocation
in the hot path, and potentially thousands of elements. A mandatory semantic
tree taxes exactly that.

So the tree is built only when a client is attached - an assistive technology,
a debugger, or a test harness - and costs nothing otherwise. This is how the
platforms expect to be used in any case, and each gives a concrete signal to
gate on: Windows sends `WM_GETOBJECT` when a client first asks, and
`UiaClientsAreListening()` answers whether anyone still is; on Linux the
accessibility bus publishes `org.a11y.Status.IsEnabled`, which is what GTK and
Qt check before initialising their bridges. Chromium likewise keeps
accessibility behind a mode flag, because the tree costs real memory and
time.

The consequence for the API is that no caller may assume the tree exists.
Anything that walks it asks for it first and may be told no.

### Late attachment

A screen reader started *after* the application must still get a complete
tree. For retained widgets that is a walk of what exists. For immediate-mode
UI there is nothing to walk, which forces a decision on the shape of the
per-frame API: it has to **accept** semantic annotations - role, name, state -
on every call, from the first version, even though it discards them on every
frame where nobody is listening. Discarding is cheap; adding the parameters
later is a signature change across every widget call.

### Updates are change-driven at the platform boundary

What reaches a platform bridge is property-changed, focus-changed and
structure-changed notifications, raised when the thing changes. A static
interface generates no accessibility traffic at all.

How those notifications are produced differs by direction, and both are fine:

- Retained widgets raise them directly, from the place the state changed.
- Immediate-mode UI has no such place, so when a client is attached it
  submits the tree every frame and a diff against the previous frame produces
  the notifications. This is what Flutter does with its semantics updates and
  what AccessKit's tree-update model is built around. An unchanged tree
  diffs to nothing, so the static case still costs nothing beyond the
  comparison.

Only the retained path is "not a per-frame diff". The bridges never know
which path fed them.

---

## 5. Identity across regeneration: the Tang problem

Tang is a template language that emits text, and the text it is built to emit
is HTML. The intended path is Tang → markup → interface, which makes the
markup the authoring surface. That is a considerable win: ARIA attributes are
written where authors already are, rather than being a separate annotation
step everyone skips.

It also introduces the sharpest problem in this document. **Tang emits a
string, wholesale, on every render.** A string has no identity. Regenerate and
naively replace, and:

- keyboard focus is lost
- scroll positions reset
- a screen reader re-announces the entire view
- in-flight animations restart
- any handle a test harness is holding goes stale

Every one of those is the same defect: node identity did not survive
regeneration.

The answer is **reconciliation**: diff the newly generated tree against the
live one and carry identity across where nodes correspond, rather than
rebuilding. The closest precedent is not React but the server-rendered world,
which has exactly this shape - regenerate the entire HTML on the server, then
*morph* the live DOM to match. idiomorph and morphdom (used by htmx and
Phoenix LiveView) do this, and both match nodes on ids first and structure
second, which is the same conclusion as below.

Two requirements follow, and both are cheap now and structural later:

1. **The markup→interface step is a diff, not a rebuild.** It has to be
   written that way from the first version; a rebuild path that later grows
   diffing is a rewrite of the update mechanism.

2. **Tang needs a way to express a key.** Correspondence is inferable for
   stable structure, and ambiguous the moment a list reorders: without an
   author-supplied key there is no way to tell "the third item changed" from
   "an item was inserted second". This wants a syntax decision in Tang, taken
   with the reconciler in mind.

### Why a string at all

Tang is ours, so it could emit a tree directly and skip the parse. The string
is kept deliberately: the browser direction has to parse HTML into a tree
regardless, and one markup→tree path serving both Tang output and fetched
documents is one reconciler, one set of projection rules and one place for
bugs. If the parse ever shows up in a profile, a tree-emitting mode for Tang
is an optimisation that can be added behind the same interface; it is not a
design decision that has to be taken now.

---

## 6. Text

The platform text interfaces - UIA's TextPattern, AT-SPI's Text, the macOS
text markers - all work in character-offset ranges, and all want run
attributes and a bounding rectangle for an arbitrary range.

That reaches back into the text subsystem. The plan there is ICU for
normalisation, bidi and break iteration, with **our own shaper** on top.
**Shaping output has to retain the mapping from character offsets to glyph
positions and rectangles** - the cluster map, in shaping terms. If that
mapping is discarded once glyphs are positioned, accessible text is not an
accessibility feature to add later, it is a rewrite of the text subsystem.
Since the shaper is ours, whether the map survives is entirely our decision.

The map is not one-to-one. Offsets are in logical order and geometry is in
visual order, so once bidi is involved a single logical range resolves to
several rectangles, and the API for "bounds of this range" returns a list.

This is the single cheapest thing on this list to preserve now and the most
expensive to recover.

---

## 7. What the node carries

Sketched rather than settled, but the shape should not be surprising:

- **id** - stable across frames and across regeneration (section 2)
- **role** - ARIA role, or one of the internal roles
- **name**, **description** - computed per the
  [Accessible Name and Description Computation](https://www.w3.org/TR/accname/),
  which is a specification in its own right and worth following rather than
  inventing
- **value** - where the role has one
- **state** - focused, focusable, disabled, checked, expanded, selected, busy,
  required, invalid
- **bounds** - held in window coordinates, for hit testing and magnifier
  tracking. Every platform API reports *screen* coordinates, so the projection
  adds the window's position, which the window already tracks
- **actions** - what can be invoked, which is also the keyboard-operability
  contract: every action must be reachable without a mouse
- **relations** - parent, children, and ARIA's labelled-by and controls
- **text** - offsets, runs and range geometry, for roles that have text

---

## 8. Things adjacent to this that are cheap now

- **Keyboard operability** as an API constraint rather than a feature. If every
  widget exposes its actions, keyboard-only operation follows; if actions are
  implicit in mouse handlers, it does not.
- **Focus as a core concept**, with a traversal order, shared between input
  focus and accessible focus so they cannot drift apart.
- **OS settings**: high contrast, reduced motion (relevant to the planned
  animation system), and system text scaling as a separate axis from DPI
  scaling.

---

## 9. Why this is worth doing before v1 ships

The existing plan has "full accessibility tree in v1 (ship stubs; complete in
v2)", which is the right call. The risk is stubs of the wrong shape, and the
items above are the ones that are shape rather than substance:

| Decision | Cost now | Cost later |
| --- | --- | --- |
| ARIA as the internal vocabulary | naming | a translation layer in perpetuity |
| Opaque stable ids in both trees | a field, and a hash scheme per direction | every consumer's handles break |
| Immediate-mode calls accept semantics | parameters that are usually ignored | a signature change across every widget call |
| Markup→interface as a diff | design the reconciler | rewrite the update path |
| Text cluster map retained | do not discard it | rewrite the text subsystem |
| Lazy, gated materialisation | a flag and a build step | callers assume the tree exists |
| Tree owned by the UI thread | a rule | a bridge that races the frame loop |

None of those require the tree to be built, or a single platform bridge to be
written. They require the decisions to be made in the right order.

There is also a commercial argument, since CJelly is aimed at enterprise use.
Accessibility is often a procurement gate rather than a feature: Section 508 in
United States federal purchasing and EN 301 549 in European public sector both
reference WCAG, and an Accessibility Conformance Report is frequently requested
during evaluation. A toolkit that cannot support one can disqualify the product
built on it.

---

## Appendix: what exists so far

`cj_window_capture()` (`ghoti.io/cjelly/cj_capture.h`) reads the frame a window is
displaying into an RGBA8 buffer, with `cj_capture_write_png()` alongside it.
That is the first piece of the harness described in section 2 - the part that
lets a test make statements about what was drawn. It is a shipped API rather
than a test hook, for the reason given there.

Nothing else in this document is built. The event-injection seam exists but is
internal (`cj_window__dispatch_*` in `window_internal.h`); neither tree exists,
because neither the widget layer nor the markup layer does yet.
