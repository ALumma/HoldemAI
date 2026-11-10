# HoldemAI

C++20 architecture outline for a no-limit Texas Hold'em tournament engine and
neural CFR research. The initial target is a six-player, single-table tournament.

**Status:** buildable interface skeleton only. The example constructs tournament
settings; it does not play poker. Rules, settlement, evaluation, serialization,
tournament progression, and training still require implementations. Abstract
interfaces deliberately prevent accidentally treating placeholder logic as a
working engine. No external dependencies are required for this outline.

## Build

With CMake 3.20 or newer:

```sh
cmake -S . -B build
cmake --build build
./build/engine_outline
```

Or directly with a C++20 compiler:

```sh
mkdir -p build
c++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude examples/engine_outline.cpp -o build/engine_outline
./build/engine_outline
```

## Components

| Header in `include/holdem/` | Contract |
| --- | --- |
| `types.hpp` | Integer chips, cards, actions, legal raise bounds, pots, and results |
| `hand_engine.hpp` | Cloneable simulator state, explicit chance, legal transitions, observations |
| `evaluator.hpp` | Adapter for an independently validated showdown evaluator |
| `tournament.hpp` | Blind schedule, inter-hand progression, elimination, and prizes |
| `training.hpp` | Decision inputs, candidate bet sizes, and policy probabilities |
| `history.hpp` | Replay records and an eventual PHH notation adapter |

## State and training boundaries

The hand engine handles one hand. The tournament manager supplies the next
hand's stacks, occupied seats, button, and stakes, then consumes its result once.
Seat IDs remain stable when a player is eliminated. Player IDs survive table
changes if multi-table support is added later.

The simulator holds the complete hidden state. Policies receive only their own
cards and public information, including betting history and tournament context.
History records containing private cards are privileged simulation data.

Every random card draw is an explicit chance node. A traversal can enumerate
outcomes or sample them using its own seeded random generator. A cloned state
must be independent of its parent. No random generator is shared implicitly.
For the first simulator, omit burn cards: uniformly dealing from the remaining
cards has the same player-visible distribution. Physical-deck replay with burn
cards would require additional event types.

The rules engine accepts all legal integer chip amounts. Training chooses a
smaller set of candidate raises through `ActionAbstraction`. Calls are capped by
the caller's stack; short all-in raises are distinct from full raises. Betting
rights must be tracked per player, including cumulative short all-in raises.

The tournament utility is prize money, not the chip delta from a single hand.
No neural framework, CFR algorithm, or approximation of future tournament value
is selected or implemented by this skeleton. Future Python bindings should
support batches of traversals and observations rather than require a Python call
for every small state transition.

## Implementation order and acceptance checks

1. **Choose and document exact tournament rules.** Specify short-stack blind/ante
   priority, dead-button handling, heads-up transitions, odd chips, simultaneous
   elimination ties and tied prizes, and showdown exposure. For the initial
   schedule, advance levels between hands and hold the final level indefinitely.
   Reject invalid configurations and define checked integer arithmetic.
2. **Implement concrete state and hand transitions.** Forced bets, hole-card
   dealing, turn order, streets, legal actions, fold wins, and all-in runouts.
   Validate actions before mutation; reject operations on the wrong node kind.
3. **Add evaluator and settlement.** Validate card uniqueness, hand ranks, ties,
   side-pot eligibility, uncalled bet refunds, and odd-chip distribution. Award
   chips once on reaching terminal state; `result()` only reads that settlement.
4. **Implement tournament progression.** Move the button, apply blind levels,
   handle eliminations and tied finishes, and calculate final prizes. Check hand
   identity and consume each result exactly once. Add multi-table support later.
5. **Implement history and replay.** Version records, add PHH conversion, and
   verify that replay reconstructs the same actions, stacks, and results.
6. **Validate before training.** Scenario tests for betting reopening, short
   calls, side pots, ties, heads-up blinds, and elimination transitions; randomized
   legal trajectories checking chip conservation and unique cards; clone
   independence; and observation checks for hidden-information leakage. Compare
   equivalent scenarios with a reference engine under matching rule settings.
7. **Measure and connect training.** Benchmark state copying, transitions,
   evaluation, and complete traversals. Start with a tabular CFR baseline on
   small games, then add sampled traversal and neural training independently.

## Reference projects

- [OpenSpiel](https://github.com/google-deepmind/open_spiel): game-state interfaces
  and small-game CFR baselines.
- [PokerKit](https://github.com/uoftcprg/pokerkit): potential rules reference and
  hand-history tooling.
- [PokerHandEvaluator](https://github.com/HenryRLee/PokerHandEvaluator): potential
  C/C++ showdown evaluator; assess licensing and validate an adapter before use.
- [PHH specification](https://phh.readthedocs.io/): external hand-history format.
