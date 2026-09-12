// World model (implementation step 4).
// See world.h for the resident-cell trie and content-slot model, and
// docs/decisions/0003-world-model.md for the binding decision (cells own
// content on top of the content-free addressing layer, decision 0002; which
// cells to load, eviction, and LOD selection are streaming policy, step 7 —
// never world-model policy).

#include "world/world.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

namespace vp::world {
namespace {

// Bounds-checked element access for the guarded runtime indices below (the
// pro-bounds rules forbid operator[] with non-constant indices). Both
// overloads are safe at every call site: the std::array one uses .at() after
// a range guard, the C-array one asserts and is fed only in-range indices
// (levels [0, kMaxCellLevel], kinds [0, kContentKindCount)).
template <typename ArrayT>
auto& at(ArrayT& arr, int idx) {
  return arr.at(static_cast<std::size_t>(idx));
}

template <typename T, std::size_t N>
T& at(T (&arr)[N], int idx) {
  // Runtime backstop, not a compile-time fact (the static-assert heuristic
  // misfires here, as on ensureCell's precondition below).
  // NOLINTNEXTLINE(misc-static-assert)
  assert(idx >= 0 && idx < static_cast<int>(N));
  return *(arr + idx);
}

}  // namespace

const char* contentKindName(ContentKind kind) {
  // No default label: a newly added enumerator must fail compilation here
  // (-Wswitch) instead of silently logging under the wrong name. Kinds
  // outside the enumerator range (corruption) fall through to the trailing
  // return.
  switch (kind) {
    case ContentKind::Terrain:
      return "terrain";
    case ContentKind::Biomes:
      return "biomes";
    case ContentKind::Water:
      return "water";
    case ContentKind::Vegetation:
      return "vegetation";
    case ContentKind::Structures:
      return "structures";
  }
  return "unknown";
}

CellContent::CellContent(ContentKind kind, const ContentProvenance& provenance)
    : kind_(kind), provenance_(provenance) {}

WorldCell::WorldCell(vp::geo::CellId id) : id_(id) {}

CellContent* WorldCell::content(ContentKind kind) {
  const int idx = static_cast<int>(kind);
  // Defensive against corrupted kinds (e.g. a payload forged outside this
  // module): an out-of-range index would be UB into slots_.
  if (idx < 0 || idx >= kContentKindCount) {
    return nullptr;
  }
  return at(slots_, idx).get();
}

const CellContent* WorldCell::content(ContentKind kind) const {
  const int idx = static_cast<int>(kind);
  if (idx < 0 || idx >= kContentKindCount) {
    return nullptr;
  }
  return at(slots_, idx).get();
}

std::unique_ptr<CellContent> WorldCell::attachContent(
    std::unique_ptr<CellContent> content) {
  // Precondition: content != nullptr. The payload's own kind() selects the
  // slot, so the slot index can never disagree with the payload.
  const int idx = static_cast<int>(content->kind());
  if (idx < 0 || idx >= kContentKindCount) {
    // Corrupted kind: bounce the payload straight back rather than index
    // out of range (dropping it would leak the caller's ownership).
    return content;
  }
  std::unique_ptr<CellContent>& slot = at(slots_, idx);
  std::unique_ptr<CellContent> previous = std::move(slot);
  slot = std::move(content);
  return previous;
}

std::unique_ptr<CellContent> WorldCell::detachContent(ContentKind kind) {
  const int idx = static_cast<int>(kind);
  if (idx < 0 || idx >= kContentKindCount) {
    return nullptr;  // corrupted kind: nothing to detach
  }
  // Moving out empties the slot; ownership passes to the caller.
  return std::move(at(slots_, idx));
}

bool WorldCell::hasContent(ContentKind kind) const {
  return content(kind) != nullptr;
}

WorldCell& World::ensureCell(vp::geo::CellId id) {
  // Runtime precondition, not a compile-time fact: id is caller data, so the
  // static-assert heuristic cannot apply.
  // NOLINTNEXTLINE(misc-static-assert)
  assert(id.valid());

  // Ancestor chain, leaf first (chain[i] is at level id.level() - i, down to
  // the level-0 cell). A fixed array suffices — the trie depth is bounded by
  // kMaxCellLevel + 1 — so the walk itself allocates nothing.
  std::array<vp::geo::CellId, vp::geo::kMaxCellLevel + 1> chain;
  int depth = 0;
  for (vp::geo::CellId c = id; c.valid(); c = vp::geo::parent(c)) {
    at(chain, depth) = c;
    ++depth;
  }

  // Create root-down so a cell's parent is always resident before the cell
  // itself (the trie invariant). Only newly created cells bump the parent's
  // resident child count — re-ensuring an existing chain is a no-op
  // (idempotence).
  for (int i = depth - 1; i >= 0; --i) {
    const vp::geo::CellId& cur = at(chain, i);
    if (cells_.find(cur.bits) != cells_.end()) {
      continue;
    }
    // new (not make_unique): WorldCell's constructor is private and World is
    // its only friend, so make_unique could not access it.
    cells_.emplace(cur.bits, std::unique_ptr<WorldCell>(new WorldCell(cur)));
    if (i + 1 < depth) {
      // The parent (chain[i + 1]) was made resident by an earlier iteration
      // of this walk; the level-0 cell (i == depth - 1) has no parent.
      WorldCell& parentCell = *cells_.at(at(chain, i + 1).bits);
      ++parentCell.residentChildren_;
    }
  }
  return *cells_[id.bits];
}

WorldCell* World::find(vp::geo::CellId id) {
  const auto it = cells_.find(id.bits);
  return it == cells_.end() ? nullptr : it->second.get();
}

const WorldCell* World::find(vp::geo::CellId id) const {
  const auto it = cells_.find(id.bits);
  return it == cells_.end() ? nullptr : it->second.get();
}

bool World::removeCell(vp::geo::CellId id) {
  const auto it = cells_.find(id.bits);
  // Leaves only: subtree pruning is streaming policy (step 7), which can
  // walk residentChildCount().
  if (it == cells_.end() || !it->second->isResidentLeaf()) {
    return false;
  }
  // A resident non-root cell's parent is always resident (the trie invariant
  // ensureCell maintains); a level-0 cell has no parent to update.
  const vp::geo::CellId parentId = vp::geo::parent(id);
  if (parentId.valid()) {
    WorldCell& parentCell = *cells_.at(parentId.bits);
    --parentCell.residentChildren_;
  }
  cells_.erase(it);  // unique_ptr destroys the cell and all its content
  return true;
}

const WorldCell* World::residentCellAt(const vp::geo::ECEF& pos,
                                       int level) const {
  if (level < 0 || level > vp::geo::kMaxCellLevel) {
    return nullptr;
  }
  // Address the position, then walk up the resident trie: the first resident
  // cell on the way (possibly the addressed cell itself) is the deepest one
  // containing pos. parent() yields invalid() at level 0, ending the walk
  // with nullptr when the region is untouched.
  for (vp::geo::CellId c = vp::geo::cellOf(pos, level); c.valid();
       c = vp::geo::parent(c)) {
    const WorldCell* const cell = find(c);
    if (cell != nullptr) {
      return cell;
    }
  }
  return nullptr;
}

WorldStats World::stats() const {
  WorldStats out;
  // One walk over all resident cells; visit order is unspecified (hash map)
  // and must not matter — every counter is an order-independent sum.
  forEachCell([&out](const WorldCell& cell) {
    ++out.residentCells;
    ++at(out.cellsByLevel, cell.id().level());
    for (int kind = 0; kind < kContentKindCount; ++kind) {
      const CellContent* const payload =
          cell.content(static_cast<ContentKind>(kind));
      if (payload == nullptr) {
        continue;
      }
      ++at(out.contentCountByKind, kind);
      out.contentBytes += payload->sizeBytes();
    }
  });
  return out;
}

}  // namespace vp::world
