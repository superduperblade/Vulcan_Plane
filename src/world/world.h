// World model (implementation step 4).
//
// The world state layer on top of the coordinate foundation (decision 0001)
// and cell addressing (decision 0002); binding decision:
// docs/decisions/0003-world-model.md.
//
//   - Resident cells form a sparse trie: ensureCell() creates the requested
//     cell and all missing ancestors, so a resident non-root cell always
//     has a resident parent. removeCell() removes leaves only; pruning is
//     streaming policy (step 7), not world-model policy.
//   - Cells own content slots keyed by ContentKind (one slot per kind).
//     Payloads are type-erased CellContent subclasses carrying their
//     provenance (the determinism contract) and byte size (memory
//     accounting for the debug tooling).
//   - residentCellAt() is the position query the runtime actually asks:
//     the deepest resident cell containing a position at or above a level.
//   - Deliberately NOT here: which cells to load, eviction, async loading,
//     LOD selection (streaming, step 7), and any content generation
//     (step 5+). Also not thread-safe — the world state is
//     single-threaded until streaming defines its synchronization
//     boundary. Cell iteration order is unspecified; deterministic outputs
//     must not depend on visit order.
//
// All position math is double precision via vp::geo (pure CPU, glm only).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include "geospatial/cells.h"

namespace vp::world {

// Content kinds match the world hierarchy in agents.md (Tiles / Cells ->
// Terrain, Biomes, Water, Vegetation, Structures). Extend as systems land;
// the slot array is sized from kCount, so appending is safe and stable.
enum class ContentKind : std::uint8_t {
  Terrain = 0,
  Biomes,
  Water,
  Vegetation,
  Structures,
  kCount
};

constexpr int kContentKindCount = static_cast<int>(ContentKind::kCount);

// Lower-case names ("terrain", ...) for logs, caches, and the debug UI.
[[nodiscard]] const char* contentKindName(ContentKind kind);

// Determinism bookkeeping (agents.md): how a piece of content was produced.
// Generation systems (step 5+) must fill it honestly; it becomes the cache
// identity when caching arrives.
struct ContentProvenance {
  std::uint32_t generatorId = 0;       // registered generator (step 5+)
  std::uint32_t generatorVersion = 0;  // generator logic version
  std::uint64_t paramsHash = 0;        // hash of the generation parameters
  std::uint64_t seed = 0;              // generation seed

  friend bool operator==(const ContentProvenance& a,
                         const ContentProvenance& b) {
    return a.generatorId == b.generatorId &&
           a.generatorVersion == b.generatorVersion &&
           a.paramsHash == b.paramsHash && a.seed == b.seed;
  }
  friend bool operator!=(const ContentProvenance& a,
                         const ContentProvenance& b) {
    return !(a == b);
  }
};

// Type-erased per-cell content payload. Subclasses hold the actual data
// (terrain meshes, biome maps, ... from later steps); the base carries only
// what the world model itself needs.
class CellContent {
 public:
  CellContent(ContentKind kind, const ContentProvenance& provenance);
  virtual ~CellContent() = default;

  CellContent(const CellContent&) = delete;
  CellContent& operator=(const CellContent&) = delete;
  CellContent(CellContent&&) = delete;
  CellContent& operator=(CellContent&&) = delete;

  [[nodiscard]] ContentKind kind() const { return kind_; }
  [[nodiscard]] const ContentProvenance& provenance() const {
    return provenance_;
  }

  // Resident memory of the payload in bytes (RAM accounting for the debug
  // tooling; GPU memory is a renderer concern and is NOT included).
  [[nodiscard]] virtual std::uint64_t sizeBytes() const = 0;

 private:
  ContentKind kind_;
  ContentProvenance provenance_;
};

// A resident cell: its identity and whatever content has been attached.
// Created only through World::ensureCell; hierarchy bookkeeping is done by
// World (friend).
class WorldCell {
 public:
  [[nodiscard]] vp::geo::CellId id() const { return id_; }

  // The cell's content for a kind; nullptr when the slot is empty.
  [[nodiscard]] CellContent* content(ContentKind kind);
  [[nodiscard]] const CellContent* content(ContentKind kind) const;

  // Attaches content to the matching slot and returns the previous content
  // (ownership passes to the caller; null if the slot was empty).
  // Precondition: content != nullptr. The payload's kind() selects the
  // slot and must match its own claim.
  std::unique_ptr<CellContent> attachContent(
      std::unique_ptr<CellContent> content);

  // Detaches and returns the content of a slot (null if empty). Ownership
  // passes to the caller — streaming may transfer payloads between cells
  // instead of destroying them.
  std::unique_ptr<CellContent> detachContent(ContentKind kind);

  [[nodiscard]] bool hasContent(ContentKind kind) const;

  // Resident-hierarchy bookkeeping, maintained by World.
  [[nodiscard]] unsigned residentChildCount() const {
    return residentChildren_;
  }
  [[nodiscard]] bool isResidentLeaf() const { return residentChildren_ == 0; }

 private:
  friend class World;
  explicit WorldCell(vp::geo::CellId id);

  vp::geo::CellId id_;
  std::array<std::unique_ptr<CellContent>, kContentKindCount> slots_;
  unsigned residentChildren_ = 0;
};

// Aggregate debug/telemetry snapshot (agents.md debug tooling): what is
// resident, at which levels, and how much content memory it holds.
struct WorldStats {
  std::uint64_t residentCells = 0;
  std::uint64_t cellsByLevel[vp::geo::kMaxCellLevel + 1] = {};
  std::uint64_t contentCountByKind[kContentKindCount] = {};
  std::uint64_t contentBytes = 0;
};

// The world state: resident cells and their content.
//
// Invariants (tested):
//   - every resident non-root cell's parent is resident;
//   - residentChildCount() of a cell equals its resident children exactly;
//   - a CellId is resident at most once (ensureCell is idempotent).
class World {
 public:
  World() = default;
  ~World() = default;
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) = delete;
  World& operator=(World&&) = delete;

  // Makes the cell resident (idempotent), creating all missing ancestors.
  // Precondition: id.valid().
  WorldCell& ensureCell(vp::geo::CellId id);

  // The resident cell, or nullptr. O(1).
  [[nodiscard]] WorldCell* find(vp::geo::CellId id);
  [[nodiscard]] const WorldCell* find(vp::geo::CellId id) const;

  // Removes a resident LEAF cell (no resident children) and destroys its
  // content. Returns false if the cell is not resident or not a leaf.
  bool removeCell(vp::geo::CellId id);

  // The deepest resident cell containing the position at or above `level`
  // (walks up the resident trie from cellOf(pos, level); nullptr when the
  // region is untouched). A level outside [0, kMaxCellLevel] yields
  // nullptr.
  [[nodiscard]] const WorldCell* residentCellAt(const vp::geo::ECEF& pos,
                                                int level) const;

  [[nodiscard]] std::size_t residentCount() const { return cells_.size(); }

  // Walks all resident cells. Visit order is UNSPECIFIED (hash map);
  // deterministic outputs must not depend on it.
  template <typename Fn>
  void forEachCell(Fn&& fn) const {
    // Forward once before the loop: forwarding per iteration would move
    // from an rvalue callable on every pass.
    auto&& visit = std::forward<Fn>(fn);
    for (const auto& entry : cells_) {
      visit(*entry.second);
    }
  }

  [[nodiscard]] WorldStats stats() const;

 private:
  std::unordered_map<std::uint64_t, std::unique_ptr<WorldCell>> cells_;
};

}  // namespace vp::world
