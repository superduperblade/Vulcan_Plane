// Unit tests for the world model (contract: src/world/world.h, decision
// 0003: docs/decisions/0003-world-model.md).
//
// Tested invariants (decision 0003, "Quality Considerations"):
//   - ensureCell is idempotent and creates the full ancestor chain: the
//     resident cells form a sparse trie, a resident non-root cell always
//     has a resident parent, and residentChildCount() is exact;
//   - find() is an exact resident lookup; removeCell() removes resident
//     leaves only and never auto-prunes ancestors;
//   - content attach/detach/replace lifecycle with ownership transfer and
//     destruction exactly once (removeCell, replace, World teardown);
//   - one content slot per ContentKind, independent across kinds, with the
//     documented lower-case kind names;
//   - WorldStats mirrors the resident trie and attached content exactly;
//   - residentCellAt() returns the deepest resident cell containing a
//     position at or above a level (level-range guard, untouched regions,
//     dateline/pole/equator-boundary conventions pinned against vp::geo,
//     decision 0002);
//   - iteration visits exactly the resident set, in no particular order;
//   - provenance round-trips and compares field-wise.
//
// The suite is quiet (no printing; the harness reports failures to
// stderr). A failed VP_CHECK does not abort, so lookups whose result is
// dereferenced below are additionally guarded with an early return; that
// keeps a null dereference impossible instead of merely unlikely.
// main() lives in geo_test.cpp: all test files link into the
// vp_unit_tests binary and register their cases through test.h.
//
// All addresses are pinned or derived deterministically (vp::geo oracle
// functions); no RNG is needed.

#include "world/world.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "geospatial/cells.h"
#include "geospatial/geo.h"
#include "test.h"

using namespace vp::geo;
using namespace vp::world;

namespace {

// Two distinct provenances for identity and ownership checks.
const ContentProvenance kProvA{1, 2, 0x123456789ABCDEF0ULL, 100};
const ContentProvenance kProvB{3, 4, 0x0FEDCBA987654321ULL, 200};

// Test payload: carries a caller-chosen sizeBytes() and an optional
// destruction flag (set in the destructor) for lifecycle assertions.
class ProbeContent final : public CellContent {
 public:
  ProbeContent(ContentKind kind, std::uint64_t size,
               const ContentProvenance& provenance,
               const std::shared_ptr<bool>& destroyed = {})
      : CellContent(kind, provenance), size_(size), destroyed_(destroyed) {}

  ~ProbeContent() override {
    if (destroyed_ != nullptr) {
      *destroyed_ = true;
    }
  }

  // Payloads are owned exclusively through unique_ptr (the base class
  // already forbids copying); spell the rule of five out anyway.
  ProbeContent(const ProbeContent&) = delete;
  ProbeContent& operator=(const ProbeContent&) = delete;
  ProbeContent(ProbeContent&&) = delete;
  ProbeContent& operator=(ProbeContent&&) = delete;

  [[nodiscard]] std::uint64_t sizeBytes() const override { return size_; }

 private:
  std::uint64_t size_;
  std::shared_ptr<bool> destroyed_;
};

std::unique_ptr<ProbeContent> makeProbe(
    ContentKind kind, std::uint64_t size, const ContentProvenance& provenance,
    const std::shared_ptr<bool>& destroyed = {}) {
  return std::make_unique<ProbeContent>(kind, size, provenance, destroyed);
}

// The resident set a World must hold after ensuring the given cells: each
// cell plus every ancestor (the sparse-trie invariant). Deriving
// expectations from the DISTINCT union keeps tests correct even where
// chains share ancestors (a 3x3 neighborhood is not 9 disjoint chains).
std::set<CellId> expectedResidents(const std::vector<CellId>& ensured) {
  std::set<CellId> out;
  for (const CellId& id : ensured) {
    for (CellId cur = id; cur.valid(); cur = parent(cur)) {
      out.insert(cur);
    }
  }
  return out;
}

// Bounds-checked element access for the WorldStats histograms (world.h
// fixes their raw-array type; the pro-bounds rules forbid operator[] with
// non-constant indices). Safe at every call site: it asserts the range and
// is fed only in-range indices (levels [0, kMaxCellLevel], kinds
// [0, kContentKindCount)).
template <typename T, std::size_t N>
T& at(T (&arr)[N], int idx) {
  // Runtime backstop, not a compile-time fact (the static-assert heuristic
  // misfires here, as on world.cpp's identical at() helper).
  // NOLINTNEXTLINE(misc-static-assert)
  assert(idx >= 0 && idx < static_cast<int>(N));
  return *(arr + idx);
}

// Per-level resident counts of an expected resident set.
std::array<std::uint64_t, kMaxCellLevel + 1> countByLevel(
    const std::set<CellId>& residents) {
  std::array<std::uint64_t, kMaxCellLevel + 1> counts{};
  for (const CellId& id : residents) {
    ++counts.at(static_cast<std::size_t>(id.level()));
  }
  return counts;
}

}  // namespace

VP_TEST(world_ensure_idempotent) {
  World w;
  const CellId c = CellId::make(6, 13, 9);
  WorldCell& first = w.ensureCell(c);
  VP_CHECK(first.id() == c);
  VP_CHECK(first.residentChildCount() == 0U);
  const std::size_t afterFirst = w.residentCount();
  VP_CHECK(afterFirst ==
           static_cast<std::size_t>(c.level() + 1));  // full chain

  // Ensuring again returns the SAME resident cell object.
  WorldCell& second = w.ensureCell(c);
  VP_CHECK(&first == &second);
  VP_CHECK(w.residentCount() == afterFirst);

  // Content storage is shared too: attach through one handle, observe
  // through the other.
  first.attachContent(makeProbe(ContentKind::Water, 32, kProvA));
  VP_CHECK(second.content(ContentKind::Water) ==
           first.content(ContentKind::Water));
  VP_CHECK(second.hasContent(ContentKind::Water));

  // Idempotent at the root as well (already resident as c's ancestor).
  const CellId rootId = ancestorAtLevel(c, 0);
  WorldCell& root = w.ensureCell(rootId);
  VP_CHECK(&root == &w.ensureCell(rootId));
  VP_CHECK(w.residentCount() == afterFirst);
}

VP_TEST(world_ensure_creates_ancestor_chain) {
  World w;
  const int level = 12;
  const CellId deep = CellId::make(level, 1234, 567);
  const WorldCell& leaf = w.ensureCell(deep);
  VP_CHECK(leaf.id() == deep);
  VP_CHECK(leaf.isResidentLeaf());
  VP_CHECK(w.residentCount() == static_cast<std::size_t>(level + 1));

  // Exactly one resident cell per level 0..level, and it is the deep
  // cell's ancestor at that level. Every chain cell except the leaf has
  // exactly one resident child (the next cell down); the leaf has none.
  for (int lv = 0; lv <= level; ++lv) {
    const CellId anc = ancestorAtLevel(deep, lv);
    const WorldCell* cell = w.find(anc);
    VP_CHECK(cell != nullptr);
    // VP_CHECK above records the failure; non-null is a precondition.
    if (cell == nullptr) {
      return;
    }
    VP_CHECK(cell->id() == anc);
    VP_CHECK(cell->residentChildCount() == (lv < level ? 1U : 0U));
  }

  // Walking parents from the leaf visits the same chain: every non-root
  // resident cell's parent is resident (sparse-trie invariant).
  CellId cur = deep;
  while (cur.level() > 0) {
    const CellId p = parent(cur);
    const WorldCell* pc = w.find(p);
    VP_CHECK(pc != nullptr);
    // VP_CHECK above records the failure; non-null is a precondition.
    if (pc == nullptr) {
      return;
    }
    VP_CHECK(pc->residentChildCount() == 1U);
    cur = p;
  }
  VP_CHECK(w.find(ancestorAtLevel(deep, 0)) != nullptr);  // root resident
}

VP_TEST(world_resident_child_counts) {
  World w;
  const CellId p = CellId::make(4, 2, 1);
  const std::array<CellId, 4> kids = children(p);

  // Three children of one cell.
  w.ensureCell(kids.at(0));
  w.ensureCell(kids.at(1));
  w.ensureCell(kids.at(2));
  const WorldCell* pc = w.find(p);
  VP_CHECK(pc != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (pc == nullptr) {
    return;
  }
  VP_CHECK(pc->residentChildCount() == 3U);
  VP_CHECK(!pc->isResidentLeaf());
  for (int k = 0; k < 3; ++k) {
    const WorldCell* kc = w.find(kids.at(k));
    VP_CHECK(kc != nullptr);
    // VP_CHECK above records the failure; non-null is a precondition.
    if (kc == nullptr) {
      return;
    }
    VP_CHECK(kc->residentChildCount() == 0U);
    VP_CHECK(kc->isResidentLeaf());
  }
  VP_CHECK(w.find(kids.at(3)) == nullptr);  // fourth child was never ensured

  // Shaped population: a grandchild under kids[0] and a child of p's
  // sibling q (same grandparent) leave every count exact.
  const std::array<CellId, 4> kid0Kids = children(kids.at(0));
  w.ensureCell(kid0Kids.at(3));
  VP_CHECK(w.find(kids.at(0))->residentChildCount() == 1U);
  VP_CHECK(pc->residentChildCount() == 3U);

  const CellId q = CellId::make(4, 3, 1);  // sibling of p
  VP_CHECK(parent(q) == parent(p));
  const std::array<CellId, 4> qKids = children(q);
  w.ensureCell(qKids.at(2));
  VP_CHECK(w.find(q)->residentChildCount() == 1U);
  VP_CHECK(w.find(parent(p))->residentChildCount() == 2U);  // p and q
  VP_CHECK(pc->residentChildCount() == 3U);

  // The whole world matches the trie invariant for what was ensured.
  const std::vector<CellId> ensured = {kids.at(0), kids.at(1), kids.at(2),
                                       kid0Kids.at(3), qKids.at(2)};
  VP_CHECK(w.residentCount() == expectedResidents(ensured).size());
}

VP_TEST(world_find_lookup) {
  World w;
  const CellId c = CellId::make(7, 40, 30);
  WorldCell& cell = w.ensureCell(c);
  VP_CHECK(w.find(c) == &cell);
  VP_CHECK(w.find(c)->id() == c);

  // const find() works.
  const World& cw = w;
  const WorldCell* cfound = cw.find(c);
  VP_CHECK(cfound != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (cfound == nullptr) {
    return;
  }
  VP_CHECK(cfound->id() == c);
  VP_CHECK(cfound == &cell);

  // Ancestors are resident (trie invariant) and findable.
  VP_CHECK(w.find(parent(c)) != nullptr);

  // Non-resident VALID ids yield nullptr: a sibling sharing the resident
  // parent, and a valid id in a completely different subtree.
  VP_CHECK(w.find(CellId::make(7, 41, 30)) == nullptr);
  VP_CHECK(w.find(CellId::make(3, 0, 0)) == nullptr);
  VP_CHECK(cw.find(CellId::make(7, 41, 30)) == nullptr);

  // invalid() is never resident.
  VP_CHECK(w.find(CellId::invalid()) == nullptr);
  VP_CHECK(cw.find(CellId::invalid()) == nullptr);
}

VP_TEST(world_remove_leaf_only) {
  World w;
  const CellId p = CellId::make(3, 5, 2);
  const std::array<CellId, 4> kids = children(p);
  w.ensureCell(kids.at(0));
  w.ensureCell(kids.at(1));
  const WorldCell* pc = w.find(p);
  VP_CHECK(pc != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (pc == nullptr) {
    return;
  }
  VP_CHECK(pc->residentChildCount() == 2U);
  const std::size_t before = w.residentCount();

  // Never-resident (or invalid) ids: false, world unchanged.
  VP_CHECK(!w.removeCell(kids.at(2)));
  VP_CHECK(!w.removeCell(CellId::make(7, 3, 3)));
  VP_CHECK(!w.removeCell(CellId::invalid()));
  VP_CHECK(w.residentCount() == before);

  // Not a leaf: p still has two resident children.
  VP_CHECK(!w.removeCell(p));
  VP_CHECK(w.find(p) != nullptr);
  VP_CHECK(w.residentCount() == before);

  // Leaf removal: gone, resident count drops, parent bookkeeping updates.
  VP_CHECK(w.removeCell(kids.at(0)));
  VP_CHECK(w.find(kids.at(0)) == nullptr);
  VP_CHECK(w.residentCount() == before - 1);
  VP_CHECK(pc->residentChildCount() == 1U);

  // Double removal: no longer resident.
  VP_CHECK(!w.removeCell(kids.at(0)));

  VP_CHECK(w.removeCell(kids.at(1)));
  VP_CHECK(pc->residentChildCount() == 0U);
  VP_CHECK(pc->isResidentLeaf());

  // p itself is now a removable leaf.
  VP_CHECK(w.removeCell(p));
  VP_CHECK(w.find(p) == nullptr);

  // No auto-pruning (decision 0003): exactly p's ancestors remain.
  std::set<CellId> remaining = expectedResidents({p});
  remaining.erase(p);
  std::set<CellId> got;
  w.forEachCell([&got](const WorldCell& c) { got.insert(c.id()); });
  VP_CHECK(got == remaining);
  VP_CHECK(w.residentCount() == remaining.size());
}

VP_TEST(world_content_lifecycle) {
  World w;
  WorldCell& cell = w.ensureCell(CellId::make(6, 10, 5));

  // Empty slots initially.
  VP_CHECK(cell.content(ContentKind::Terrain) == nullptr);
  VP_CHECK(!cell.hasContent(ContentKind::Terrain));

  // Attach: the payload's kind() selects the slot; the slot was empty.
  auto first = makeProbe(ContentKind::Terrain, 128, kProvA);
  ProbeContent* firstRaw = first.get();
  VP_CHECK(cell.attachContent(std::move(first)) == nullptr);
  VP_CHECK(cell.content(ContentKind::Terrain) == firstRaw);
  VP_CHECK(cell.hasContent(ContentKind::Terrain));
  VP_CHECK(cell.content(ContentKind::Terrain)->sizeBytes() == 128U);
  const WorldCell& cview = cell;  // const overload sees the same payload
  VP_CHECK(cview.content(ContentKind::Terrain) == firstRaw);

  // Replace: returns the PREVIOUS payload; ownership passes to the caller.
  auto second = makeProbe(ContentKind::Terrain, 64, kProvB);
  ProbeContent* secondRaw = second.get();
  std::unique_ptr<CellContent> previous = cell.attachContent(std::move(second));
  VP_CHECK(previous != nullptr);
  VP_CHECK(previous.get() == firstRaw);
  VP_CHECK(cell.content(ContentKind::Terrain) == secondRaw);
  previous.reset();  // the caller drops the handed-back payload

  // Detach: ownership moves out, the slot is left null.
  std::unique_ptr<CellContent> detached =
      cell.detachContent(ContentKind::Terrain);
  VP_CHECK(detached != nullptr);
  VP_CHECK(detached.get() == secondRaw);
  VP_CHECK(cell.content(ContentKind::Terrain) == nullptr);
  VP_CHECK(!cell.hasContent(ContentKind::Terrain));
  detached.reset();

  // Detaching an empty slot yields null.
  VP_CHECK(cell.detachContent(ContentKind::Terrain) == nullptr);

  // The cell is still resident; the full cycle can repeat.
  cell.attachContent(makeProbe(ContentKind::Terrain, 16, kProvA));
  VP_CHECK(cell.hasContent(ContentKind::Terrain));
}

VP_TEST(world_content_destruction) {
  // removeCell destroys the removed cell's content; content elsewhere
  // survives, and a leaf without content removes fine.
  auto removedFlag = std::make_shared<bool>(false);
  auto survivorFlag = std::make_shared<bool>(false);
  World w;
  const CellId leaf = CellId::make(5, 9, 4);
  const CellId other = CellId::make(5, 10, 4);  // shares ancestors, not content
  w.ensureCell(leaf).attachContent(
      makeProbe(ContentKind::Terrain, 64, kProvA, removedFlag));
  w.ensureCell(other).attachContent(
      makeProbe(ContentKind::Biomes, 32, kProvB, survivorFlag));
  VP_CHECK(!*removedFlag);
  VP_CHECK(!*survivorFlag);
  VP_CHECK(w.removeCell(leaf));
  VP_CHECK(w.find(leaf) == nullptr);
  VP_CHECK(*removedFlag);    // destroyed exactly once with its cell
  VP_CHECK(!*survivorFlag);  // untouched payload still alive
  VP_CHECK(w.find(other)->content(ContentKind::Biomes) != nullptr);
  const CellId bare = CellId::make(5, 11, 4);
  w.ensureCell(bare);
  VP_CHECK(w.removeCell(bare));  // no content: no destruction needed

  // attachContent hands the previous payload back; destruction happens
  // when the CALLER drops it (attach never destroys behind the caller's
  // back unless the handle is dropped).
  auto oldFlag = std::make_shared<bool>(false);
  auto keptFlag = std::make_shared<bool>(false);
  World w2;
  WorldCell& cell = w2.ensureCell(CellId::make(4, 2, 1));
  cell.attachContent(makeProbe(ContentKind::Water, 16, kProvA, oldFlag));
  VP_CHECK(!*oldFlag);
  auto replacement = makeProbe(ContentKind::Water, 32, kProvB, keptFlag);
  CellContent* replacementRaw = replacement.get();
  std::unique_ptr<CellContent> kept =
      cell.attachContent(std::move(replacement));
  VP_CHECK(kept != nullptr);
  VP_CHECK(!*oldFlag);  // handed back alive, not destroyed
  kept.reset();         // the caller's drop destroys it exactly once
  VP_CHECK(*oldFlag);
  VP_CHECK(!*keptFlag);
  VP_CHECK(cell.content(ContentKind::Water) == replacementRaw);

  // World destruction destroys remaining content exactly once.
  auto teardownFlag = std::make_shared<bool>(false);
  {
    World w3;
    w3.ensureCell(CellId::make(2, 1, 1))
        .attachContent(
            makeProbe(ContentKind::Structures, 8, kProvA, teardownFlag));
    VP_CHECK(!*teardownFlag);
  }
  VP_CHECK(*teardownFlag);

  // detachContent passes ownership out; destruction happens on the
  // caller's drop, not at detach time and not again at World teardown.
  auto detachedFlag = std::make_shared<bool>(false);
  {
    World w4;
    WorldCell& c4 = w4.ensureCell(CellId::make(3, 6, 2));
    c4.attachContent(
        makeProbe(ContentKind::Vegetation, 24, kProvA, detachedFlag));
    std::unique_ptr<CellContent> owned =
        c4.detachContent(ContentKind::Vegetation);
    VP_CHECK(owned != nullptr);
    VP_CHECK(!*detachedFlag);  // alive in the caller's hands
    owned.reset();
    VP_CHECK(*detachedFlag);
  }  // w4 tears down an empty slot: no second destruction
}

VP_TEST(world_content_kinds_independent) {
  VP_CHECK(kContentKindCount == 5);
  VP_CHECK(static_cast<int>(ContentKind::kCount) == kContentKindCount);

  World w;
  WorldCell& cell = w.ensureCell(CellId::make(4, 8, 3));
  const std::array<ContentKind, kContentKindCount> kinds = {
      ContentKind::Terrain, ContentKind::Biomes, ContentKind::Water,
      ContentKind::Vegetation, ContentKind::Structures};
  std::array<CellContent*, kContentKindCount> raws = {};

  // One cell holds all five kinds simultaneously; each payload lands in
  // its own slot (the payload's kind() selects it).
  for (int k = 0; k < kContentKindCount; ++k) {
    ContentProvenance prov = kProvA;
    prov.seed = static_cast<std::uint64_t>(k);  // per-kind marker
    auto payload =
        makeProbe(kinds.at(k), 100 + static_cast<std::uint64_t>(k), prov);
    raws.at(k) = payload.get();
    VP_CHECK(cell.attachContent(std::move(payload)) == nullptr);
  }
  for (int k = 0; k < kContentKindCount; ++k) {
    const CellContent* stored = cell.content(kinds.at(k));
    VP_CHECK(stored == raws.at(k));
    VP_CHECK(stored->kind() == kinds.at(k));
    VP_CHECK(stored->sizeBytes() == 100U + static_cast<std::uint64_t>(k));
    VP_CHECK(stored->provenance().seed == static_cast<std::uint64_t>(k));
    VP_CHECK(cell.hasContent(kinds.at(k)));
  }

  // Detaching one kind leaves the other slots untouched.
  std::unique_ptr<CellContent> water = cell.detachContent(ContentKind::Water);
  VP_CHECK(water != nullptr);
  VP_CHECK(water->kind() == ContentKind::Water);
  VP_CHECK(cell.content(ContentKind::Water) == nullptr);
  VP_CHECK(!cell.hasContent(ContentKind::Water));
  for (int k = 0; k < kContentKindCount; ++k) {
    if (kinds.at(k) == ContentKind::Water) {
      continue;
    }
    VP_CHECK(cell.content(kinds.at(k)) == raws.at(k));
  }
  water.reset();

  // The freed slot accepts new content without disturbing the others.
  auto replacement = makeProbe(ContentKind::Water, 999, kProvB);
  CellContent* replacementRaw = replacement.get();
  VP_CHECK(cell.attachContent(std::move(replacement)) == nullptr);
  VP_CHECK(cell.content(ContentKind::Water) == replacementRaw);
  VP_CHECK(cell.content(ContentKind::Terrain) == raws.at(0));

  // Documented lower-case names; out-of-range kinds name "unknown".
  VP_CHECK(std::string(contentKindName(ContentKind::Terrain)) == "terrain");
  VP_CHECK(std::string(contentKindName(ContentKind::Biomes)) == "biomes");
  VP_CHECK(std::string(contentKindName(ContentKind::Water)) == "water");
  VP_CHECK(std::string(contentKindName(ContentKind::Vegetation)) ==
           "vegetation");
  VP_CHECK(std::string(contentKindName(ContentKind::Structures)) ==
           "structures");
  VP_CHECK(std::string(contentKindName(static_cast<ContentKind>(200))) ==
           "unknown");
}

VP_TEST(world_stats_correct) {
  World w;
  // An empty world is all zeros.
  const WorldStats empty = w.stats();
  VP_CHECK(empty.residentCells == 0U);
  VP_CHECK(empty.contentBytes == 0U);
  for (const std::uint64_t levelCount : empty.cellsByLevel) {
    VP_CHECK(levelCount == 0U);
  }
  for (const std::uint64_t kindCount : empty.contentCountByKind) {
    VP_CHECK(kindCount == 0U);
  }

  // Shaped population: two subtrees diverging at level 3, cells at levels
  // 10, 11 and 12.
  std::vector<CellId> ensured;
  const CellId a = CellId::make(10, 100, 50);
  const CellId b = children(a).at(1);           // NE child of a, level 11
  const CellId c = children(b).at(2);           // SW child of b, level 12
  const CellId d = CellId::make(12, 999, 400);  // second subtree, level 12
  for (const CellId& id : {a, b, c, d}) {
    w.ensureCell(id);
    ensured.push_back(id);
  }

  // Content of four kinds with known sizes, spread over three cells.
  w.ensureCell(a).attachContent(makeProbe(ContentKind::Biomes, 4096, kProvA));
  w.ensureCell(c).attachContent(makeProbe(ContentKind::Terrain, 1000, kProvB));
  w.ensureCell(c).attachContent(makeProbe(ContentKind::Water, 256, kProvA));
  w.ensureCell(d).attachContent(makeProbe(ContentKind::Vegetation, 77, kProvB));

  const std::set<CellId> want = expectedResidents(ensured);
  const std::array<std::uint64_t, kMaxCellLevel + 1> wantByLevel =
      countByLevel(want);
  const WorldStats s = w.stats();
  VP_CHECK(s.residentCells == want.size());
  for (int l = 0; l <= kMaxCellLevel; ++l) {
    VP_CHECK(at(s.cellsByLevel, l) == wantByLevel.at(l));
  }
  // Shaped-ness pins: both subtrees contribute at levels 10..12.
  VP_CHECK(s.cellsByLevel[10] == 2U);
  VP_CHECK(s.cellsByLevel[11] == 2U);
  VP_CHECK(s.cellsByLevel[12] == 2U);
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Terrain)] == 1U);
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Biomes)] == 1U);
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Water)] == 1U);
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Vegetation)] ==
           1U);
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Structures)] ==
           0U);
  VP_CHECK(s.contentBytes == 4096U + 1000U + 256U + 77U);

  // Removing leaf c (its two payloads die with it) is reflected exactly;
  // its ancestors stay resident and a's and d's content is untouched.
  VP_CHECK(w.removeCell(c));
  const WorldStats after = w.stats();
  VP_CHECK(after.residentCells == s.residentCells - 1U);
  VP_CHECK(after.cellsByLevel[12] == s.cellsByLevel[12] - 1U);
  VP_CHECK(after.cellsByLevel[10] == s.cellsByLevel[10]);
  VP_CHECK(after.cellsByLevel[11] == s.cellsByLevel[11]);
  VP_CHECK(after.contentCountByKind[static_cast<int>(ContentKind::Terrain)] ==
           0U);
  VP_CHECK(after.contentCountByKind[static_cast<int>(ContentKind::Water)] ==
           0U);
  VP_CHECK(after.contentCountByKind[static_cast<int>(ContentKind::Biomes)] ==
           1U);
  VP_CHECK(
      after.contentCountByKind[static_cast<int>(ContentKind::Vegetation)] ==
      1U);
  VP_CHECK(after.contentBytes == 4096U + 77U);
}

VP_TEST(world_resident_cell_at) {
  // Part 1: hit, deeper fallback, coarser fallback, untouched regions,
  // empty world, out-of-range levels.
  World w;
  const LLA zurich{47.3769, 8.5417, 540.0};
  const ECEF zurichEcef = llaToEcef(zurich);
  const int level = 8;
  const CellId c = cellOf(zurichEcef, level);
  VP_CHECK(c.valid());
  VP_CHECK(c == cellOf(zurich, level));  // LLA and ECEF addressing agree
  w.ensureCell(c);

  // The populated cell answers at its own level.
  const WorldCell* hit = w.residentCellAt(zurichEcef, level);
  VP_CHECK(hit != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (hit == nullptr) {
    return;
  }
  VP_CHECK(hit->id() == c);
  VP_CHECK(hit == w.find(c));
  VP_CHECK(contains(c, zurichEcef));

  // A deeper query inside the same region falls back up the trie to the
  // populated ancestor (nothing deeper is resident).
  VP_CHECK(w.residentCellAt(zurichEcef, 20) == hit);
  VP_CHECK(w.residentCellAt(zurichEcef, kMaxCellLevel) == hit);

  // Coarser queries return the resident ancestors (ensureCell created the
  // whole chain).
  const WorldCell* coarse = w.residentCellAt(zurichEcef, 4);
  VP_CHECK(coarse != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (coarse == nullptr) {
    return;
  }
  VP_CHECK(coarse->id() == ancestorAtLevel(c, 4));
  VP_CHECK(contains(ancestorAtLevel(c, 4), zurichEcef));
  const WorldCell* rootHit = w.residentCellAt(zurichEcef, 0);
  VP_CHECK(rootHit != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (rootHit == nullptr) {
    return;
  }
  VP_CHECK(rootHit->id().level() == 0);
  VP_CHECK(rootHit->id() == ancestorAtLevel(c, 0));

  // Same root hemisphere, different level-1 cell: the walk skips the
  // untouched levels and answers with the root itself.
  const ECEF sameRoot = llaToEcef(LLA{10.0, 100.0, 0.0});
  VP_CHECK(cellOf(sameRoot, 1) != ancestorAtLevel(c, 1));  // really elsewhere
  VP_CHECK(ancestorAtLevel(cellOf(sameRoot, 1), 0) == rootHit->id());
  const WorldCell* rootFallback = w.residentCellAt(sameRoot, 5);
  VP_CHECK(rootFallback != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (rootFallback == nullptr) {
    return;
  }
  VP_CHECK(rootFallback->id() == rootHit->id());

  // A region with NO resident ancestor at all: nullptr.
  const LLA untouched{-45.0, -135.0, 0.0};
  VP_CHECK(cellOf(untouched, 0) != rootHit->id());  // other hemisphere
  VP_CHECK(w.residentCellAt(llaToEcef(untouched), 8) == nullptr);
  VP_CHECK(w.residentCellAt(llaToEcef(untouched), 1) == nullptr);

  // An empty world answers nullptr everywhere.
  World fresh;
  VP_CHECK(fresh.residentCellAt(zurichEcef, 3) == nullptr);

  // Levels outside [0, kMaxCellLevel] yield nullptr.
  VP_CHECK(w.residentCellAt(zurichEcef, -1) == nullptr);
  VP_CHECK(w.residentCellAt(zurichEcef, kMaxCellLevel + 1) == nullptr);

  // Part 2: equator-row convention. Latitude intervals are north-closed
  // (south, north] (decision 0002): lat 0 sits on the equator boundary
  // and belongs to the row south of it, whose north edge it is.
  const int eqLevel = 6;
  const CellId eqCell = cellOf(LLA{0.0, 100.0, 0.0}, eqLevel);
  VP_CHECK(eqCell.y() == static_cast<std::uint32_t>(cellYCount(eqLevel) / 2));
  w.ensureCell(eqCell);
  const ECEF justSouth = llaToEcef(LLA{-1.0, 100.0, 0.0});  // same row
  VP_CHECK(cellOf(justSouth, eqLevel) == eqCell);           // ECEF path agrees
  const WorldCell* hitEq = w.residentCellAt(justSouth, eqLevel);
  VP_CHECK(hitEq != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (hitEq == nullptr) {
    return;
  }
  VP_CHECK(hitEq->id() == eqCell);
  VP_CHECK(contains(eqCell, justSouth));

  // Part 3: poles. The exact poles clamp into the edge rows (+90 ->
  // y = 0, -90 -> last row) and resolve through ECEF as well; a near-pole
  // interior point of row 0 too.
  const int poleLevel = 5;
  const CellId north = cellOf(LLA{90.0, 0.0, 0.0}, poleLevel);
  const CellId south = cellOf(LLA{-90.0, 0.0, 0.0}, poleLevel);
  VP_CHECK(north.y() == 0U);
  VP_CHECK(south.y() == static_cast<std::uint32_t>(cellYCount(poleLevel) - 1));
  w.ensureCell(north);
  w.ensureCell(south);
  const ECEF northEcef = llaToEcef(LLA{90.0, 0.0, 0.0});
  const ECEF southEcef = llaToEcef(LLA{-90.0, 0.0, 0.0});
  const WorldCell* hitN = w.residentCellAt(northEcef, poleLevel);
  VP_CHECK(hitN != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (hitN == nullptr) {
    return;
  }
  VP_CHECK(hitN->id() == north);
  VP_CHECK(contains(north, northEcef));
  const WorldCell* hitS = w.residentCellAt(southEcef, poleLevel);
  VP_CHECK(hitS != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (hitS == nullptr) {
    return;
  }
  VP_CHECK(hitS->id() == south);
  VP_CHECK(contains(south, southEcef));

  const ECEF nearPole = llaToEcef(LLA{89.0, 40.0, 0.0});
  const CellId nearCell = cellOf(nearPole, poleLevel);
  VP_CHECK(nearCell == cellOf(LLA{89.0, 40.0, 0.0}, poleLevel));
  VP_CHECK(nearCell.y() == 0U);  // the row touching the pole
  w.ensureCell(nearCell);
  const WorldCell* hitNear = w.residentCellAt(nearPole, poleLevel);
  VP_CHECK(hitNear != nullptr);
  VP_CHECK(hitNear == w.find(nearCell));
  VP_CHECK(w.residentCellAt(nearPole, kMaxCellLevel) == hitNear);
  VP_CHECK(contains(nearCell, nearPole));

  // Part 4: the dateline, in its own world. Cells never straddle the
  // meridian; +180 and -180 are the same meridian and address into
  // column 0 (its west edge), while the last column's east edge is +180
  // (decision 0002). Queries use interior points: the meridian itself is
  // a cell edge, and ecefToLla's rounding may sit on either side of it.
  World dl;
  const int dlLevel = 6;
  const LLA meridian{40.0, 180.0, 0.0};
  const CellId dlCell = cellOf(meridian, dlLevel);
  VP_CHECK(dlCell == cellOf(LLA{40.0, -180.0, 0.0}, dlLevel));
  VP_CHECK(dlCell.x() == 0U);
  VP_CHECK(contains(dlCell, meridian));
  VP_CHECK(contains(dlCell, LLA{40.0, -180.0, 0.0}));
  dl.ensureCell(dlCell);

  // East of the dateline: interior of column 0 (lon -179).
  const LLA eastSide{40.0, -179.0, 0.0};
  VP_CHECK(cellOf(eastSide, dlLevel) == dlCell);
  const WorldCell* hitEast = dl.residentCellAt(llaToEcef(eastSide), dlLevel);
  VP_CHECK(hitEast != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (hitEast == nullptr) {
    return;
  }
  VP_CHECK(hitEast->id() == dlCell);
  VP_CHECK(contains(dlCell, llaToEcef(eastSide)));

  // West of the dateline: interior of the last column (lon +179).
  const LLA westSide{40.0, 179.0, 0.0};
  const CellId westCell = cellOf(westSide, dlLevel);
  VP_CHECK(westCell.x() == static_cast<std::uint32_t>(cellXCount(dlLevel) - 1));
  VP_CHECK(westCell != dlCell);
  dl.ensureCell(westCell);
  const WorldCell* hitWest = dl.residentCellAt(llaToEcef(westSide), dlLevel);
  VP_CHECK(hitWest != nullptr);
  // VP_CHECK above records the failure; non-null is a precondition.
  if (hitWest == nullptr) {
    return;
  }
  VP_CHECK(hitWest->id() == westCell);
  VP_CHECK(contains(westCell, llaToEcef(westSide)));
  VP_CHECK(dl.residentCellAt(llaToEcef(westSide), kMaxCellLevel) == hitWest);
}

VP_TEST(world_iteration_visits_all) {
  World w;
  std::vector<CellId> ensured;
  const CellId base = CellId::make(6, 20, 14);
  const std::array<CellId, 4> kids = children(base);
  for (const CellId& id :
       {base, kids.at(0), kids.at(3), CellId::make(6, 21, 14),
        children(kids.at(0)).at(2)}) {
    w.ensureCell(id);
    ensured.push_back(id);
  }
  const std::set<CellId> want = expectedResidents(ensured);

  // Collect what iteration visits; the ORDER is unspecified (hash map),
  // so compare sets only, never sequences.
  std::set<CellId> got;
  std::size_t visited = 0;
  w.forEachCell([&got, &visited](const WorldCell& c) {
    ++visited;
    got.insert(c.id());
  });
  VP_CHECK(visited == want.size());
  VP_CHECK(got == want);
  VP_CHECK(w.residentCount() == want.size());

  // forEachCell is const-usable.
  const World& cw = w;
  std::size_t constVisited = 0;
  cw.forEachCell([&constVisited](const WorldCell&) { ++constVisited; });
  VP_CHECK(constVisited == want.size());
}

VP_TEST(world_provenance_roundtrip) {
  const ContentProvenance base{7, 2, 0x123456789ABCDEF0ULL, 42};
  const ContentProvenance same = base;
  VP_CHECK(base == same);
  VP_CHECK(!(base != same));

  // Each field participates in the comparison.
  const ContentProvenance differing[] = {
      {8, 2, 0x123456789ABCDEF0ULL, 42},  // generatorId differs
      {7, 3, 0x123456789ABCDEF0ULL, 42},  // generatorVersion differs
      {7, 2, 0x123456789ABCDEF1ULL, 42},  // paramsHash differs
      {7, 2, 0x123456789ABCDEF0ULL, 43},  // seed differs
  };
  for (const ContentProvenance& p : differing) {
    VP_CHECK(base != p);
    VP_CHECK(!(base == p));
  }
  VP_CHECK((ContentProvenance{} == ContentProvenance{0, 0, 0, 0}));

  // Provenance stored on content survives attach and inspection.
  World w;
  WorldCell& cell = w.ensureCell(CellId::make(5, 11, 6));
  cell.attachContent(makeProbe(ContentKind::Terrain, 128, base));
  const CellContent* stored = cell.content(ContentKind::Terrain);
  VP_CHECK(stored != nullptr);
  VP_CHECK(stored->kind() == ContentKind::Terrain);
  VP_CHECK(stored->provenance() == base);
  VP_CHECK(stored->provenance().generatorId == 7U);
  VP_CHECK(stored->provenance().generatorVersion == 2U);
  VP_CHECK(stored->provenance().paramsHash == 0x123456789ABCDEF0ULL);
  VP_CHECK(stored->provenance().seed == 42U);

  // ... through detach ...
  std::unique_ptr<CellContent> detached =
      cell.detachContent(ContentKind::Terrain);
  VP_CHECK(detached != nullptr);
  VP_CHECK(detached->provenance() == base);

  // ... and through a re-attach (the streaming transfer path).
  cell.attachContent(std::move(detached));
  VP_CHECK(cell.content(ContentKind::Terrain) != nullptr);
  VP_CHECK(cell.content(ContentKind::Terrain)->provenance() == base);
}

VP_TEST(world_reensure_after_remove) {
  World w;
  const CellId leaf = children(CellId::make(5, 7, 3)).at(2);  // level 6
  WorldCell& first = w.ensureCell(leaf);
  first.attachContent(makeProbe(ContentKind::Vegetation, 512, kProvA));
  first.attachContent(makeProbe(ContentKind::Water, 64, kProvB));
  VP_CHECK(first.hasContent(ContentKind::Vegetation));
  VP_CHECK(first.hasContent(ContentKind::Water));
  const std::size_t before = w.residentCount();

  VP_CHECK(w.removeCell(leaf));
  VP_CHECK(w.residentCount() == before - 1);

  // Re-ensure: a fresh cell with empty slots and correct bookkeeping.
  WorldCell& again = w.ensureCell(leaf);
  VP_CHECK(again.id() == leaf);
  VP_CHECK(w.residentCount() == before);
  for (int k = 0; k < kContentKindCount; ++k) {
    const auto kind = static_cast<ContentKind>(k);
    VP_CHECK(again.content(kind) == nullptr);
    VP_CHECK(!again.hasContent(kind));
  }
  VP_CHECK(again.residentChildCount() == 0U);
  VP_CHECK(again.isResidentLeaf());

  // The ancestor chain and child counts are correct again.
  const WorldCell* pc = w.find(parent(leaf));
  VP_CHECK(pc != nullptr);
  VP_CHECK(pc->residentChildCount() == 1U);

  // The fresh cell accepts content again.
  again.attachContent(makeProbe(ContentKind::Terrain, 32, kProvA));
  VP_CHECK(again.content(ContentKind::Terrain) != nullptr);
  VP_CHECK(again.content(ContentKind::Vegetation) == nullptr);
}

VP_TEST(world_spawn_scenario) {
  // The vp_core usage shape: a spawn LLA becomes ECEF, a 3x3 level-12
  // neighborhood plus the exact level-14 cell goes resident, terrain
  // content attaches, and position queries answer for the renderer.
  const LLA spawn{47.3769, 8.5417, 540.0};  // Zurich-ish
  const ECEF spawnEcef = llaToEcef(spawn);
  World w;

  const CellId spawnCell = cellOf(spawnEcef, 12);
  VP_CHECK(spawnCell.valid());
  VP_CHECK(spawnCell == cellOf(spawn, 12));  // LLA/ECEF addressing agree
  std::vector<CellId> ensured;
  std::set<CellId> hood;  // mid-latitude/mid-longitude: no wrap or clamp
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const CellId n = neighbor(spawnCell, dx, dy);
      VP_CHECK(n.valid());
      w.ensureCell(n);
      ensured.push_back(n);
      hood.insert(n);
    }
  }
  VP_CHECK(hood.size() == 9U);

  // The exact cell at level 14 containing the spawn position.
  const CellId fine = cellOf(spawnEcef, 14);
  VP_CHECK(fine.valid());
  VP_CHECK(ancestorAtLevel(fine, 12) == spawnCell);  // inside the spawn cell
  w.ensureCell(fine);
  ensured.push_back(fine);

  // The 3x3 neighborhood shares ancestors, so the resident count is the
  // DISTINCT union of the ancestor chains (not 9 * 13 + ...). Derive it
  // by simulating the trie invariant.
  const std::set<CellId> want = expectedResidents(ensured);
  const std::array<std::uint64_t, kMaxCellLevel + 1> wantByLevel =
      countByLevel(want);
  VP_CHECK(w.residentCount() == want.size());
  WorldStats s = w.stats();
  VP_CHECK(s.residentCells == want.size());
  for (int l = 0; l <= kMaxCellLevel; ++l) {
    VP_CHECK(at(s.cellsByLevel, l) == wantByLevel.at(l));
  }
  VP_CHECK(s.cellsByLevel[12] == 9U);  // the neighborhood itself
  VP_CHECK(s.cellsByLevel[13] == 1U);  // fine's parent only
  VP_CHECK(s.cellsByLevel[14] == 1U);  // fine itself

  // Terrain content on the level-14 cell and one level-12 neighbor.
  auto fineDestroyed = std::make_shared<bool>(false);
  auto terrainFine =
      makeProbe(ContentKind::Terrain, 4096, kProvA, fineDestroyed);
  CellContent* terrainFineRaw = terrainFine.get();
  w.ensureCell(fine).attachContent(std::move(terrainFine));
  const CellId nb = neighbor(spawnCell, 1, 0);  // east neighbor
  auto terrainNb = makeProbe(ContentKind::Terrain, 2048, kProvB);
  CellContent* terrainNbRaw = terrainNb.get();
  // The neighborhood is already resident: attach through the idempotent
  // ensureCell() reference instead of re-looking the cell up.
  w.ensureCell(nb).attachContent(std::move(terrainNb));

  // Content shows up in the stats; residency is unchanged.
  s = w.stats();
  VP_CHECK(s.residentCells == want.size());
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Terrain)] == 2U);
  for (int k = 1; k < kContentKindCount; ++k) {
    VP_CHECK(at(s.contentCountByKind, k) == 0U);
  }
  VP_CHECK(s.contentBytes == 4096U + 2048U);

  // Position queries: the L14 cell answers at and below its level.
  const WorldCell* hit14 = w.residentCellAt(spawnEcef, 14);
  VP_CHECK(hit14 != nullptr);
  VP_CHECK(hit14->id() == fine);
  VP_CHECK(hit14->content(ContentKind::Terrain) == terrainFineRaw);
  VP_CHECK(contains(fine, spawnEcef));
  VP_CHECK(w.residentCellAt(spawnEcef, 20) == hit14);  // deep fallback
  VP_CHECK(w.residentCellAt(spawnEcef, kMaxCellLevel) == hit14);
  // At the neighborhood level the L14 cell is too deep to be eligible:
  // the spawn cell itself answers.
  const WorldCell* hit12 = w.residentCellAt(spawnEcef, 12);
  VP_CHECK(hit12 != nullptr);
  VP_CHECK(hit12->id() == spawnCell);
  VP_CHECK(contains(spawnCell, spawnEcef));

  // A position inside the east neighbor resolves to the neighbor's L12
  // cell (its subtree has no resident L14 cell); the payload follows.
  const CellBounds nbBounds = bounds(nb);
  const LLA nbCenter{(nbBounds.latSouthDeg + nbBounds.latNorthDeg) / 2.0,
                     (nbBounds.lonWestDeg + nbBounds.lonEastDeg) / 2.0, 0.0};
  const ECEF nbEcef = llaToEcef(nbCenter);
  VP_CHECK(cellOf(nbEcef, 12) == nb);
  const WorldCell* hitNb = w.residentCellAt(nbEcef, 14);
  VP_CHECK(hitNb != nullptr);
  VP_CHECK(hitNb->id() == nb);
  VP_CHECK(hitNb->content(ContentKind::Terrain) == terrainNbRaw);
  VP_CHECK(contains(nb, nbEcef));

  // Removing the L14 leaf works (its content dies with it, exactly once)
  // and the world falls back afterwards.
  VP_CHECK(w.find(fine)->isResidentLeaf());
  VP_CHECK(w.removeCell(fine));
  VP_CHECK(w.find(fine) == nullptr);
  VP_CHECK(*fineDestroyed);
  VP_CHECK(w.find(nb)->content(ContentKind::Terrain) == terrainNbRaw);

  // No auto-pruning (decision 0003): fine's L13 parent is still resident
  // and is now the deepest resident cell containing the spawn position.
  const CellId l13 = parent(fine);
  const WorldCell* l13Cell = w.find(l13);
  VP_CHECK(l13Cell != nullptr);
  VP_CHECK(l13Cell->isResidentLeaf());
  const WorldCell* fallback14 = w.residentCellAt(spawnEcef, 14);
  VP_CHECK(fallback14 != nullptr);
  VP_CHECK(fallback14->id() == l13);
  VP_CHECK(w.residentCellAt(spawnEcef, 20) == fallback14);
  // At the neighborhood level the query falls back to the L12 parent.
  const WorldCell* fallback12 = w.residentCellAt(spawnEcef, 12);
  VP_CHECK(fallback12 != nullptr);
  VP_CHECK(fallback12->id() == spawnCell);
  VP_CHECK(w.find(spawnCell)->residentChildCount() == 1U);  // l13 remains

  // Stats reflect the removal exactly.
  s = w.stats();
  VP_CHECK(s.residentCells == want.size() - 1);
  VP_CHECK(s.cellsByLevel[14] == 0U);
  VP_CHECK(s.cellsByLevel[13] == 1U);  // l13 still resident
  VP_CHECK(s.cellsByLevel[12] == 9U);
  VP_CHECK(s.contentCountByKind[static_cast<int>(ContentKind::Terrain)] == 1U);
  VP_CHECK(s.contentBytes == 2048U);
}
