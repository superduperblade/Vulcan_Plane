# Standing Development Protocol

## 0. Tooling and Permissions

- You are free to use all available tools, including memory, and to install
  any packages or libraries needed for research or development.
- You may run build tools, interpreters, and test harnesses (e.g. cmake,
  ninja, make, python) as part of building and verifying the project.
- Do not run executables unrelated to the project or its build/test
  toolchain.
- Model files (weights, datasets, reference material,3d objects) may be used freely;
  they are not executables.

## 1. Purpose

This protocol defines the engineering philosophy and working method for the
project across development sessions.

It is a framework for judgment, not a rigid checklist.

Use the amount of process appropriate to the task.

A small change should remain small.

A complex system should be treated as a complex system.

The objective is to consistently produce software that is correct, capable,
efficient, reliable, and maintainable without unnecessary engineering ceremony.

---

# 2. First Principles

These principles take precedence over individual rules.

## 2.1 Understand the real problem

Before deciding how to implement something, understand what the system is
actually required to accomplish.

Separate:

- the underlying objective;
- requirements;
- constraints;
- assumptions;
- quality expectations;
- performance expectations.

Do not confuse a requested implementation with the actual problem it is
intended to solve.

Ask:

> What is this system actually trying to accomplish?

> What must be true for it to be successful?

The implementation should follow from those answers.

---

## 2.2 Correctness is foundational

Do not knowingly sacrifice correctness for speed, simplicity, elegance, or
convenience.

Correctness may include:

- outputs;
- state;
- data integrity;
- numerical accuracy;
- concurrency;
- persistence;
- compatibility;
- security;
- failure behaviour.

A faster system that produces the wrong result is not an improvement.

---

## 2.3 Quality is multidimensional

Quality may include:

- correctness;
- output quality;
- reliability;
- performance;
- resource efficiency;
- scalability;
- security;
- maintainability;
- observability;
- usability.

Different systems will prioritize these differently.

Determine what matters to the actual project.

Do not improve one property while silently damaging a more important one.

---

## 2.4 Performance is part of quality

Performance should be considered during design, not only after the system
becomes slow.

A system that unnecessarily wastes CPU, GPU, memory, storage, bandwidth, or
time is not necessarily well-engineered.

However, do not optimize for speed at the expense of the system's actual
purpose.

When optimizing, prefer:

> **Eliminate work → reduce work → accelerate work → optimize implementation details.**

Look for:

- unnecessary computation;
- duplicate work;
- unnecessary copies;
- excessive conversions;
- repeated initialization;
- poor algorithms;
- excessive I/O;
- unnecessary synchronization.

When performance matters, find the actual bottleneck rather than optimizing
what merely looks expensive.

---

## 2.5 Complexity is not inherently bad

Do not pursue simplicity for its own sake.

A complex problem may require a complex architecture.

Distinguish between:

**Essential complexity** — imposed by the problem.

**Useful complexity** — justified by meaningful benefits such as scalability,
performance, reliability, isolation, or extensibility.

**Accidental complexity** — created by duplication, poor boundaries,
unnecessary abstraction, dead code, or historical baggage.

Preserve essential complexity.

Use useful complexity when its benefits justify its costs.

Remove accidental complexity.

The objective is:

> **As much structure as the problem requires, and no unnecessary structure beyond that.**

Prefer clarity over superficial simplicity.

---

## 2.6 Think causally

When something is wrong, understand why before changing it.

Ask:

> What causes this behaviour?

> What constrains it?

> Which component is actually responsible?

> What happens if that cause is changed?

Fix causes rather than symptoms whenever practical.

---

## 2.7 Reason, then verify

Use reasoning, then verify important assumptions where practical.

Do not blindly trust:

- remembered APIs;
- outdated documentation;
- assumptions about performance;
- assumptions about hardware;
- assumptions about concurrency.

When performance is uncertain and consequential, measure it where practical;
otherwise reason it out from first principles and say so.

When the answer is obvious and low-risk, do not create unnecessary
investigation.

---

# 3. Engineering Judgment

The protocol is not a substitute for judgment.

For small tasks, act directly.

For moderate changes, understand the affected architecture and verify the
result.

For large changes, consider requirements, architecture, alternatives, risks,
and verification before committing to major work.

For major architectural decisions, explicitly consider:

- requirements;
- constraints;
- alternatives;
- performance;
- reliability;
- complexity;
- future change.

Do not create planning or documentation merely for ceremony.

Do not ask for permission for every ordinary engineering decision.

When the requirements are clear, choose a reasonable solution and proceed.

Seek clarification only when ambiguity materially affects the intended result
or creates a significant irreversible tradeoff.

---

# 4. Architecture

## 4.1 Let the problem determine the architecture

Do not force the project into a preferred architectural style.

A system may appropriately be:

- simple;
- modular;
- monolithic;
- distributed;
- event-driven;
- plugin-based;
- hardware-specific;
- highly parallel;
- composed of specialized subsystems.

Choose based on actual requirements.

Consider:

- scale;
- data flow;
- latency;
- throughput;
- fault isolation;
- deployment;
- hardware;
- security;
- expected change.

---

## 4.2 Use meaningful boundaries

Create components around real responsibilities and useful boundaries.

Boundaries may provide:

- isolation;
- ownership;
- independent change;
- testability;
- performance control;
- security separation.

Do not create layers that merely pass data through.

Do not split simple logic into excessive micro-components.

Do not let unrelated components become unnecessarily coupled.

---

## 4.3 Isolate volatility

Parts likely to change rapidly should be isolated when that isolation
provides real value.

Examples:

- external APIs;
- experimental implementations;
- model backends;
- hardware-specific code;
- database implementations;
- third-party integrations.

Do not abstract merely because abstraction is possible.

---

# 5. Performance Engineering

When performance is important:

1. Identify the workload.
2. Find the bottleneck.
3. Understand why it is expensive.
4. Remove unnecessary work.
5. Reduce necessary work.
6. Use appropriate hardware or parallelism.
7. Optimize implementation details.
8. Verify that the improvement is real.

Consider whether the workload is:

- CPU-bound;
- GPU-bound;
- memory-bound;
- I/O-bound;
- latency-bound.

Use appropriate techniques such as:

- batching;
- caching;
- SIMD;
- multithreading;
- asynchronous I/O;
- GPU acceleration;
- native libraries;
- efficient data structures.

Do not assume more threads, caching, abstraction, or lower-level code
automatically makes something faster.

Avoid unbounded concurrency and uncontrolled resource growth.

Keep performance-specific complexity isolated where practical.

Meaningful performance gains may justify additional complexity.

Tiny gains normally do not.

---

# 6. Performance and Quality Tradeoffs

Never silently sacrifice important output quality for performance.

Be deliberate when considering:

- lower precision;
- reduced resolution;
- fewer iterations;
- smaller models;
- reduced context;
- approximate algorithms;
- lossy compression;
- dropped data;
- reduced validation.

These may be valid engineering decisions, but the tradeoff must be intentional
and appropriate to the actual workload.

A performance optimization is successful only when the resulting system still
satisfies its important requirements.

---

# 7. Maintainability

Write code so that another competent developer can understand its structure
and reasoning.

Prefer:

- meaningful names;
- clear dependencies;
- coherent responsibilities;
- predictable control flow;
- appropriate abstractions;
- consistent conventions.

Do not optimize for minimum line count.

Do not optimize for maximum abstraction.

Do not optimize for cleverness.

Sophisticated code is acceptable when it solves a sophisticated problem well.

Comments should explain important **why**, not merely repeat **what** the code
does.

Document unusual workarounds, invariants, and non-obvious performance
decisions.

Remove dead code, obsolete commented-out implementations, stale debugging
code, and meaningless TODOs.

---

# 8. Scope and Initiative

Do not silently turn a task into an unrelated rewrite.

However, fix directly related problems when they prevent:

- correctness;
- the requested feature;
- required performance;
- meaningful verification;
- architectural integrity.

Use initiative.

Do not ask for approval for routine implementation choices.

Do not expand the scope merely because unrelated improvements are available.

---

# 9. Verification

A task is complete when the resulting system has been adequately verified.

Use verification proportional to risk.

Depending on the change, this may include:

- compilation;
- building;
- tests;
- integration tests;
- static analysis;
- linting;
- formatting;
- runtime checks;
- manual verification;
- performance testing.

Consider regressions in:

- correctness;
- compatibility;
- performance;
- memory;
- concurrency;
- error handling;
- resource usage.

When performance is a significant concern, benchmark representative workloads
rather than relying exclusively on artificial microbenchmarks.

Never fabricate measurements or claim checks that were not performed.

If an important check could not be performed, state that clearly.

---

# 10. Debugging

When debugging:

1. Reproduce the problem.
2. Reduce uncertainty.
3. Find where actual behaviour diverges from expected behaviour.
4. Identify the underlying cause.
5. Fix the cause.
6. Verify the result.
7. Consider regressions.

Use evidence such as:

- logs;
- stack traces;
- tests;
- profiling;
- runtime inspection;
- instrumentation;
- minimal reproductions.

Do not repeatedly patch symptoms without understanding the failure.

---

# 11. External Dependencies

Use libraries and services when they provide meaningful value.

Consider:

- capability;
- correctness;
- performance;
- stability;
- maintenance;
- compatibility;
- security;
- licensing.

Do not reimplement mature functionality without a reason.

Do not add large dependencies to solve trivial problems.

When external behaviour materially affects implementation, verify the current
API or documentation instead of relying solely on memory.

---

# 12. Persistent Project Knowledge

Maintain:

```text
docs/
  GOALS.md
  STATUS.md
  ARCHITECTURE.md
  CONVENTIONS.md
  WORLD.md
  PERFORMANCE.md
  decisions/
    0001-short-title.md
    0002-short-title.md
```

Documentation exists to preserve useful knowledge between sessions.

Do not duplicate the source code.

Document information that affects future reasoning.

---

## `GOALS.md`

```markdown
# Goals

## Current Focus

What the project is primarily trying to accomplish now.

## Near-Term

What should happen next.

## Longer-Term

Important future objectives.

## Out of Scope

Things deliberately not being pursued.
```

---

## `STATUS.md`

```markdown
# Status

## Last Updated

YYYY-MM-DD

## Just Finished

What was completed.

## In Progress

What is currently being worked on.

## Performance

Important bottlenecks, measurements, constraints, or recent improvements.

## Known Issues

Important known problems.

## Next Step

The most logical next action.
```

`STATUS.md` describes reality, not intention.

Another AI should be able to continue development from it.

---

## `ARCHITECTURE.md`

```markdown
# Architecture

## Layers

Major architectural layers and their responsibilities.

## Major Components

Important components and relationships.

## Data Flow

Important paths through the system.

## Performance Characteristics

Important bottlenecks, scaling characteristics, resource constraints, or
hardware assumptions.

## Security Boundaries

Important trust boundaries and security assumptions.

## Known Rough Edges

Known temporary, imperfect, or unfinished areas.
```

Keep this architectural rather than duplicating source code.

---

## `WORLD.md`

Record the world model: world representation, geographic coordinate model,
terrain model, biome model, tile hierarchy, streaming, LOD, geographic data
sources, and generation pipeline.

---

## `PERFORMANCE.md`

Record meaningful benchmarks, bottlenecks, target hardware, performance
constraints, major optimizations, and known performance limitations.

---

## `CONVENTIONS.md`

Record project-specific conventions such as:

- formatting;
- linting;
- build commands;
- test commands;
- supported environments;
- deployment requirements;
- benchmark commands;
- performance-sensitive areas;
- hardware assumptions.

---

## Decision Records

For significant architectural or technical decisions, create:

```text
docs/decisions/000X-short-title.md
```

Use:

```markdown
# Decision: Short Title

## Date

YYYY-MM-DD

## Context

What problem required a decision?

## Decision

What was chosen?

## Alternatives

What reasonable alternatives were considered?

## Consequences

What becomes easier or harder?

## Performance Considerations

Relevant performance implications.

## Quality Considerations

Implications for correctness, quality, reliability, or compatibility.
```

Do not create decision records for trivial implementation choices.

---

# 13. Session Start

At the beginning of a meaningful development session:

1. Read `docs/STATUS.md`.
2. Read `docs/GOALS.md`.
3. Read `docs/ARCHITECTURE.md`.
4. Read `docs/CONVENTIONS.md` if present.
5. Inspect filenames in `docs/decisions/`.
6. Inspect the relevant code, configuration, and tests.

Compare documentation with the actual project.

Do not blindly trust stale documentation.

If a documented constraint conflicts with the actual task, recognize the
conflict and make the appropriate decision.

---

# 14. Session End

Before ending a meaningful development session:

1. Leave the project in a coherent state.
2. Perform appropriate verification.
3. Record what was completed.
4. Record unfinished work.
5. Record important discovered issues.
6. Record significant performance findings or tradeoffs.
7. Record significant architectural decisions.
8. Record the exact logical next step.
9. Update persistent documentation where necessary.

The next session should be able to continue without reconstructing important
context from the previous conversation.

---

# 15. Large or Uncertain Work

For substantial work where the implementation direction is genuinely
uncertain, create a concise temporary `PLAN.md` containing:

- objective;
- current understanding;
- proposed approach;
- alternatives;
- risks;
- verification strategy.

Use planning to reduce uncertainty, not as mandatory paperwork.

Do not create elaborate plans for straightforward tasks.

---

# 16. Final Operating Principle

Build the system the problem actually requires.

Do not undershoot the problem.

Do not overshoot the problem.

Do not remove necessary complexity merely because it looks complicated.

Do not preserve accidental complexity merely because it already exists.

Do not waste computation unnecessarily.

Do not sacrifice quality for meaningless performance gains.

Do not sacrifice performance through careless architecture.

Do not blindly optimize.

Do not blindly simplify.

Do not blindly abstract.

Do not blindly preserve existing code.

Understand the problem.

Understand the causes.

Understand the constraints.

Choose the appropriate structure.

Build it well.

Measure when it matters.

Verify it.

Preserve the knowledge that matters.

Then continue.

The central question is:

> **What does this system actually need, what causes its current behaviour, what constraints matter, and what is the best engineering decision given those facts?**

The guiding principle is:

> **Build as much complexity as the problem genuinely requires, remove the complexity it does not, and make the resulting system as correct, capable, efficient, reliable, and maintainable as the real requirements justify.**
