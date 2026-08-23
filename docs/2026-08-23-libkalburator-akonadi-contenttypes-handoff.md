# Handoff to libkalburator: `AkonadiProvider` never populates `CollectionInfo::contentTypes` and types every collection `"calendar"` (RFC)

**Date:** 2026-08-23
**From:** WildPalms (`main` @ `7a4d564`, pin v1.01)
**To:** libkalburator maintainer (+ PlanStan as co-consumer for green-gate)
**Status:** open RFC. Root cause confirmed against the `v1.01` tree; requesting a
small fix in `AkonadiProvider::onCollectionsFetched`.

> **Process note.** This follows the standard WP-writes-RFC / lib-team-lands flow
> (`feedback_libkalburator_handoff_workflow`). PlanStan must stay green per
> `feedback_planstan_pretest_for_upstream` before tagging.

---

## 0. TL;DR

This is the Akonadi-side sibling of the DAV `contentTypes` gap closed in
v0.67 (`docs/2026-06-09-libkalburator-collectioninfo-contenttypes-handoff.md`).
That RFC fixed `CalDavProvider`/`MultiProtocolDavProvider`; **the Akonadi
provider still has the identical omission**, plus a second problem that makes it
worse:

1. `AkonadiProvider::onCollectionsFetched` never sets `info.contentTypes`
   (`src/sync/akonadiprovider.cpp:137-141`) — even though the mimeType list it
   needs (`col.contentMimeTypes()`) is *already in a local variable* two lines
   above (`:123`) and is what the type decision is made from.
2. Any collection whose mime types contain the event **or** todo mimetype is
   typed `"calendar"` unconditionally (`:125-127`). A tasks-only Akonadi task
   list arrives at consumers as `type == "calendar"`, `contentTypes == {}`.

Consumer-visible symptom in WildPalms (first-run shakedown finding F4,
High): the New Profile wizard's Bindings page matches collections per conduit
via `PimPlugin::matchesCollection`
(`src/plugins/pimplugin.cpp:8-29`):

```cpp
if (d == QLatin1String("calendar")) {
    // contentTypes are authoritative when reported ...
    if (!c.contentTypes.isEmpty())
        return c.contentTypes.contains(QStringLiteral("VEVENT"));
    return c.type == QLatin1String("calendar");     // <-- Akonadi lands here
}
if (d == QLatin1String("todo"))
    return c.type == QLatin1String("todos")
        || c.contentTypes.contains(QStringLiteral("VTODO"));  // <-- never true for Akonadi
```

Net effect with a real Akonadi account:

- **Tasks conduit dropdown is empty** — every Akonadi task list is
  `type == "calendar"` with no `contentTypes`, so neither todo predicate fires.
- **Calendar dropdown over-matches** — the empty-`contentTypes` fallback makes
  every Akonadi collection (including VTODO-only ones) a calendar candidate.
- Contacts is unaffected in practice: address books are typed `"contacts"` and
  matched on `type`.

Only the DAV provider populates `contentTypes` today
(`src/sync/multiprotocoldavprovider.cpp:241-242`), which is exactly why WP's
matcher works for DAV accounts and not for Akonadi.

---

## 1. Evidence

Observed at pin **`v1.01`** (`b847ab8` on main); the cited code is unchanged
since well before v0.77.

### 1.1 The producer drops data it already holds

`AkonadiProvider::onCollectionsFetched`,
`src/sync/akonadiprovider.cpp:122-142`:

```cpp
for (const auto &col : job->collections()) {
    const auto mimes = col.contentMimeTypes();      // :123 -- RIGHT THERE
    QString type;
    if (mimes.contains(KCalendarCore::Event::eventMimeType()) ||
        mimes.contains(KCalendarCore::Todo::todoMimeType())) {
        type = QStringLiteral("calendar");          // :127 -- VTODO-only ⇒ "calendar"
    } else if (mimes.contains(KContacts::Addressee::mimeType())) {
        type = QStringLiteral("contacts");
    } else {
        continue;
    }
    ...
    CollectionInfo info;
    info.id   = akonadiCollectionIdToString(col.id());
    info.name = col.displayName();
    info.type = type;                               // :140 -- info.contentTypes never set
    m_collections.append(info);
}
```

The fetch scope already requests exactly these mime types
(`connect()`, `:94-100`: event, todo, and — unless `calendarsOnly` — addressee),
so the data is complete and local. This mirrors the v0.67 DAV bug shape: the
capability exists at discovery time and is dropped by the `CollectionInfo`
projection only.

### 1.2 The consumer side (WildPalms)

`src/plugins/pimplugin.cpp:11-24` (quoted in §0): `contentTypes` are preferred
when present, bare `type == "calendar"` is the explicit fallback "for providers
that don't report components (Akonadi)" — written in anticipation of this exact
fix, per the v0.67 handoff's closing paragraph.

### 1.3 Why the todo branch can't rescue itself

WP's todo predicate also accepts `c.type == "todos"`. The Akonadi provider
never emits that type — its vocabulary is only `"calendar"` / `"contacts"`
(`:127,129`). So both todo predicates miss simultaneously.

---

## 2. Requested fix

**Direction 1 (preferred) — populate `contentTypes` from the mimeType list,
inside the loop at `src/sync/akonadiprovider.cpp:137-141`:**

```cpp
if (mimes.contains(KCalendarCore::Event::eventMimeType()))
    info.contentTypes << QStringLiteral("VEVENT");
if (mimes.contains(KCalendarCore::Todo::todoMimeType()))
    info.contentTypes << QStringLiteral("VTODO");
if (mimes.contains(KContacts::Addressee::mimeType()))
    info.contentTypes << QStringLiteral("VCARD");
```

Three-to-five lines, same shape as the accepted v0.67 DAV fix
(`multiprotocoldavprovider.cpp:241-242`).

**Backward-compat consideration:** keep `info.type` semantics as-is
(`"calendar"` for any event-or-todo collection, `"contacts"` otherwise).
Consumers written before this change rely on `type == "calendar"` matching all
Akonadi PIM collections; once `contentTypes` is populated, WP's matcher
automatically switches to the authoritative branch and the bare-type fallback
stays as dead-code safety for any other consumer that predates component
reporting. No `type` vocabulary change ("todos") is requested in this RFC.

**Optional follow-up (separate, not blocking):** `createBackendForCollection`
(`:167-196`) only has `"calendar"` and `"contacts"` branches. A VTODO-only
collection bound via the wizard would currently instantiate `AkonadiBackend`.
We believe that is fine — `AkonadiBackend` already discriminates
`CalendarType::Todo` from mime types (`src/calendar/akonadibackend.cpp:215-220`)
and handles todos throughout (`:601-602,663`) — but please confirm, or route
tasks-only bindings explicitly, if you'd rather not have that implicit. Not a
requested change in this RFC; flagging so it doesn't surprise anyone.

### 2.1 Non-goals / things we are NOT asking for

- No change to `type` values or vocabulary.
- No new per-domain Akonadi backends (see the optional follow-up above).
- No change to the shared id scheme (`akonadicollectionid.h`) — that surface was
  settled by the v0.77 contacts-prefix fix and works on device now.

### 2.2 Suggested regression test (lib-side)

The existing provider tests (`tests/sync/tst_akonadiprovider.cpp`) are all
gated behind `KALBURATOR_AKONADI_LIVE_TEST`, which can't assert a tasks-only
collection deterministically. Sketch, either shape acceptable:

- **Preferred:** extract the body of the `onCollectionsFetched` loop into a
  testable pure helper, e.g.
  `static QList<CollectionInfo> collectionsFromFetched(const QList<Akonadi::Collection> &, bool calendarsOnly)`
  — then a unit test feeds it one VEVENT-only, one VTODO-only, one mixed, and
  one address-book collection and asserts each row's `contentTypes` and `type`.
- **Minimal:** extend the live-gated test with a case that finds a
  `contentTypes.contains("VTODO")` collection (skip if none exists) and asserts
  `createBackends()` yields a backend for it — proving end-to-end bindability.

---

## 3. WP-side context (why now)

Found by the first-run simulated shakedown (2026-08-22, finding F4):
with a real Akonadi account the wizard cannot offer any Tasks binding, and the
Calendar dropdown lists every Akonadi collection including tasks-only ones.
Once this lands and WP pins past it, the WP matcher (`pimplugin.cpp`) needs
**no changes** — it was written against the documented `CollectionInfo`
contract (`contentTypes` documented as the `"VEVENT", "VTODO", "VCARD"` subset,
`src/types/collectioninfo.h:21`) and starts working immediately.

Consumer-side impact notes:

- Wizard Bindings page (accounts-first flow): todo dropdown gains Akonadi
  VTODO-capable entries; datebook dropdown shrinks correctly to VEVENT-capable
  ones via the authoritative branch.
- Persisted routes are unaffected: rows store `<backend-id>:<collection-id>`
  targets; only candidate *filtering* changes.
- No migration; no behavior change for DAV accounts (already populate
  `contentTypes` since v0.67).

---

## Acceptance criteria

1. An `AkonadiProvider` connected to an Akonadi server reports `contentTypes`
   for every exposed collection (VEVENT/VTODO from the collection's content
   mime types; VCARD for address books).
2. A VTODO-only collection is offered to WP's todo-conduit matcher
   (`contentTypes.contains("VTODO")`) and NOT to the calendar matcher.
3. A regression test pins the projection (pure-helper unit test preferred over
   live-gated only).
4. lib suite green; PlanStan ctest baseline green before tagging.
