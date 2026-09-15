# The semantic model

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
Not as an influence: the internal model *is* ARIA, and the platform
accessibility APIs are things CJelly projects outward to.

The reasoning is the browser direction. A browser has to implement ARIA
because web content uses it, so an internal model in any other vocabulary
means translating ARIA into it and back out again to three platform APIs. The
other two directions lose nothing by using it - ARIA's role set was drawn from
desktop accessibility in the first place, and its terms map cleanly onto UIA
and AT-SPI2 because that is what they were designed from.

It also suits building this from scratch. ARIA is a published specification
with no runtime, no library, and nothing to link.

This is what Chromium settled on for the same reasons: an ARIA-derived role
enumeration internally, with the platform layers projecting from it.

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
Chrome DevTools Protocol has DOM, Layout and Overlay domains for debugging and
a distinct Accessibility domain that surfaces the accessibility tree *to* the
debugger.

**Node identity is shared between them.** Every element carries an id that is
stable across frames and across regeneration of the interface, and both trees
key on it. That is what makes it possible to ask "this accessible node - which
layout boxes is it?", and it is what lets a test harness choose its level:
semantic queries for durable tests ("invoke the button named Save"),
introspection queries for white-box assertions ("that layer was not
re-rasterised").

### The test harness is a consumer, not a third tree

A harness that drives CJelly reads these two trees and injects events through
the existing dispatch seam (`cj_window__dispatch_key_callback` and its
siblings in `window_internal.h`). It does not need a model of its own.

Worth knowing: on Windows the accessibility API *is* the automation API.
Microsoft named it UI Automation because it was built for screen readers and
test tooling as one thing, and commercial GUI test tools drive Qt through
`QAccessible` for the same reason. Building the semantic tree is building most
of the harness.

---

## 3. The semantic tree is computed, never adopted

It is not the widget tree with extra fields, and it is not the markup.

The browser world has the clearest demonstration of why. `visibility:hidden`
is in the DOM and absent from the accessibility tree. `aria-hidden` is in the
DOM and in layout, and absent from the accessibility tree. `role="presentation"`
erases an element's semantics while keeping its children. One `<select>`
becomes several accessible nodes. Generated content is laid out and never
announced.

The accessibility tree is neither a subset nor a superset of the document - it
is a projection, computed by rules. Building it as a decorated copy of
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
a debugger, or a test harness - and costs nothing otherwise. This is how UIA
expects to be used in any case: providers are queried on demand rather than
pushing state outward. Chromium likewise keeps accessibility behind a mode
flag, because the tree costs real memory and time.

The consequence for the API is that no caller may assume the tree exists.
Anything that walks it asks for it first and may be told no.

### Updates are change-driven

Property-changed, focus-changed and structure-changed notifications, raised
when the thing changes. Not a per-frame diff, and not hung off the render
loop: a static interface should generate no accessibility traffic at all.

---

## 5. Identity across regeneration: the Tang problem

Tang is a template language that emits HTML, and the intended path is Tang →
markup → interface. That makes the markup the authoring surface, which is a
considerable win: ARIA attributes are written where authors already are,
rather than being a separate annotation step everyone skips.

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

The answer is the one browsers and declarative UI frameworks arrived at
independently - **reconciliation**. Diff the newly generated tree against the
live one and carry identity across where nodes correspond, rather than
rebuilding.

Two requirements follow, and both are cheap now and structural later:

1. **The markup→interface step is a diff, not a rebuild.** It has to be
   written that way from the first version; a rebuild path that later grows
   diffing is a rewrite of the update mechanism.

2. **Tang needs a way to express a key.** Correspondence is inferable for
   stable structure, and ambiguous the moment a list reorders: without an
   author-supplied key there is no way to tell "the third item changed" from
   "an item was inserted second". This wants a syntax decision in Tang, taken
   with the reconciler in mind.

---

## 6. Text

The platform text interfaces - UIA's TextPattern, AT-SPI's Text, the macOS
text markers - all work in character-offset ranges, and all want run
attributes and a bounding rectangle for an arbitrary range.

That reaches back into the text subsystem, which is planned around ICU
shaping. **Shaping output has to retain the mapping from character offsets to
glyph positions and rectangles.** If that mapping is discarded once glyphs are
positioned, accessible text is not an accessibility feature to add later, it
is a rewrite of the text subsystem.

This is the single cheapest thing on this list to preserve now and the most
expensive to recover.

---

## 7. What the node carries

Sketched rather than settled, but the shape should not be surprising:

- **id** - stable across frames and across regeneration
- **role** - ARIA role
- **name**, **description** - computed per the ARIA name computation, which is
  itself specified and worth following rather than inventing
- **value** - where the role has one
- **state** - focused, focusable, disabled, checked, expanded, selected, busy,
  required, invalid
- **bounds** - in window coordinates, for hit testing and magnifier tracking
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
| Stable ids in both trees | a field and a scheme | every consumer's handles break |
| Markup→interface as a diff | design the reconciler | rewrite the update path |
| Text offset mapping retained | do not discard it | rewrite the text subsystem |
| Lazy, gated materialisation | a flag and a build step | callers assume the tree exists |

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

`cj_window_capture()` (`cjelly/cj_capture.h`) reads the frame a window is
displaying into an RGBA8 buffer, with `cj_capture_write_png()` alongside it.
That is the first piece of the harness described in section 2 - the part that
lets a test make statements about what was drawn. It is a shipped API rather
than a test hook, for the reason given there.

Nothing else in this document is built. The event-injection seam exists but is
internal (`cj_window__dispatch_*` in `window_internal.h`); neither tree exists,
because neither the widget layer nor the markup layer does yet.
