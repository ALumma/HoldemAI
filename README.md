# HoldemAI

C++20 foundations for a no-limit Texas Hold'em tournament engine and CFR research.
The first target is a single-table freezeout tournament. No external libraries are
required for the implemented modules.

## Implementation status

| Component | Status |
| --- | --- |
| `types` | Card notation/IDs, deck generation, duplicate detection, legal-action validation, checked chip arithmetic |
| `tournament` | Single-table manager, dead-button rotation, heads-up transitions, blind levels, eliminations, tied prizes, independent clones |
| `training` | Legal pot-size abstraction, uniform/learned policies, reproducible sampling, regret matching, full-tree tabular CFR, checkpoint save/load |
| `hand_engine` | Interface only: betting, card dealing, side pots, and settlement still need implementation |
| `evaluator` | Interface only: no showdown evaluator selected yet |
| `history` | Interface only: no PHH import/export or replay implementation yet |

The trainer is a working **small-game correctness baseline**, not a neural trainer
or a full Hold'em solver. The test suite supplies an independent Kuhn poker game.
Full tournament self-play requires a concrete hand engine and a game adapter;
scaling will also require sampled traversal and neural/value approximation.

## Build and test

With CMake 3.20 or newer:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/engine_outline
```

Without CMake, using a C++20 compiler:

```sh
mkdir -p build
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude src/types.cpp src/tournament.cpp src/training.cpp examples/engine_outline.cpp -o build/engine_outline
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude src/types.cpp src/tournament.cpp src/training.cpp tests/engine_tests.cpp -o build/engine_tests
./build/engine_tests
./build/engine_outline
```

For memory and undefined-behavior checks with Clang, compile the tests with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` in place of `-O2`.

## Shared types

- Chips and prizes use signed 64-bit integers; prizes are separate currency units.
  `checked_add` and `checked_subtract` reject overflow, including signed deltas.
- Cards use ranks 2 through ace and suits clubs/diamonds/hearts/spades. `parse_card`
  accepts exactly two characters, e.g. `As` or `Td`. Numeric IDs are rank-major
  from 0 through 51; `validate_cards` rejects invalid or duplicate cards.
- A raise amount is the total contribution on the current street. A bet into an
  unopened pot is also `raise_to`. Fold/check/call actions have amount zero.
- `LegalActions` describes full-raise bounds or a short all-in, never both. Calls
  use the actual payable amount, capped by the stack. The hand engine remains
  responsible for deriving these bounds and betting-reopening rights.
- Simulator state and private history records must never be passed to policies.
  `PlayerObservation` contains only the viewer's cards and public information,
  including explicit dead-blind positions.

## Tournament usage and rules

```cpp
#include "holdem/tournament.hpp"

holdem::TournamentConfig config;
config.levels = {
    {{50, 100, 0, holdem::AnteKind::none}, 10},
    {{100, 200, 200, holdem::AnteKind::big_blind}, 10}
};
config.prizes = {3000, 1800, 1200}; // e.g. cents
holdem::SingleTableTournament tournament(config, {10, 20, 30, 40, 50, 60});
auto hand = tournament.next_hand();
// Future hand engine: play hand, then tournament.finish_hand(hand.id, result).
```

The player vector assigns stable seats 0..N-1. Supported fields have 2..10
players with equal positive starting stacks; IDs must be unique. The caller can
shuffle seating and choose `initial_button`. Blind schedules must be nonempty,
have positive hand durations and nondecreasing blinds. Prize schedules must be
nonempty, nonnegative, nonincreasing, and fit the field size.

Implemented rules and explicit simulation choices:

- **Freezeout:** no rebuys, late registration, multi-table balancing, satellite
  early termination, or clock-based level changes. Play continues to one winner.
- **Blinds:** advance between hands after the specified number of completed
  hands. The final level remains indefinitely; its observation countdown is zero.
  Levels use zero-based indexes, finishing places use one-based indexes.
- **Dead button:** advance the big blind to the next surviving seat. The previous
  big-blind position becomes the small-blind position, possibly dead; the previous
  small-blind position becomes the button, possibly dead. `HandSetup::blinds`
  explicitly identifies live blinds. The hand engine must honor this override.
- **Heads-up:** advance the big blind to the next survivor; the other player gets
  the button and small blind. This prevents consecutive big blinds on transition.
- **Elimination:** zero final stack eliminates a player. Among players eliminated
  in the same hand, larger starting stacks finish higher. Equal starting stacks
  share the best occupied rank and split the combined prizes for the occupied
  ranks. Any indivisible currency remainder goes to lower original seat IDs.
- **Results:** require exactly one result per participating player, matching hand
  ID, nonnegative stacks, correct net deltas, and conserved chips. Pot audit data,
  when present, is validated for positive amounts and distinct participating
  eligible seats; this manager does not verify poker hand strength or adjudicate
  pots. Invalid results leave the tournament unchanged; duplicate consumption is
  rejected. `next_hand()` can be read repeatedly without advancing anything.
- **Prizes:** only `awards()` after completion returns final awards, including
  zeros. `clone()` branches the manager independently.

These choices are documented simulation rules, not a claim of implementing every
[TDA procedure](https://www.pokertda.com/view-poker-tda-rules/). For the later hand
engine, short-stack ante/blind priority, showdown exposure, and odd poker chips
still need explicit rules. Prize splitting here concerns currency, not poker chips.

## Training usage and contracts

`PotBetAbstraction` includes legal folds/checks/calls, the minimum full raise,
configured pot fractions (default half-pot and pot), and optionally the all-in.
Duplicate raises are removed. Facing a bet, a fraction `f` means:

```text
raise_to = current_street_contribution + call_cost
           + floor(f * (pot_including_current_bets + call_cost))
```

Amounts are clamped to legal full-raise bounds using overflow-safe integer
arithmetic. A short all-in remains available even if optional full-size all-ins
are disabled. The engine supports all legal integer amounts; this abstraction
only restricts which actions a training experiment considers.

`UniformPolicy` is a baseline. `regret_matching` normalizes positive regrets,
falling back to uniform if none are positive. `sample_action` accepts a caller-owned
`std::mt19937_64`; its conversion to a draw is explicitly defined for repeatability.
Invalid distributions, duplicate candidates, and illegal candidate actions fail.

Implement `CfrState` for a finite perfect-recall game, then:

```cpp
holdem::TabularCfr trainer(2);
trainer.train(root, 5000); // root is a concrete CfrState
// player_index and information_key are supplied by the game's encoder.
auto probabilities = trainer.average_strategy({player_index, information_key});
trainer.save(output_stream);
// Later, same player count and same game/action encoding:
holdem::TabularCfr resumed(2);
resumed.load(input_stream);
resumed.train(root, 1000);
```

- Decision nodes supply a player index, an information-set key, ordered candidate
  actions, and child states. A key must reflect the acting player's information
  and memory only. Distinct histories in the same information set must agree on
  their actions and the player's own reach probability. Keys are namespaced by
  player index. General perfect recall remains the adapter's responsibility.
- Chance nodes supply a normalized distribution and child states. Terminal nodes
  supply one finite utility per player. A tournament adapter should supply prize
  utility, not just a hand's chip profit. No future-prize estimator is included.
- Each iteration traverses the full tree with a frozen strategy profile, sums
  regrets weighted by chance and opponent reach, and accumulates the average
  strategy once per information set using the player's own reach. All players
  update simultaneously. Failed iterations leave the preceding checkpoint intact.
- The averaged strategy is the inference output. `AverageStrategyPolicy` connects
  it to `Policy` using a caller-supplied encoder of public decision inputs. Unknown
  keys and mismatched action order fail explicitly. Give it a copied trainer for
  a frozen evaluation opponent; the trainer itself is not thread-safe.
- Versioned text checkpoints retain full floating-point precision and validate
  entries before replacement. The caller must keep game rules and key semantics
  consistent: the checkpoint has no automatic game fingerprint. Loads reject a
  different player count, malformed actions, negative strategy sums, duplicate
  entries, and trailing data. Checkpoints are intended for trusted local use.
- The usual equilibrium guarantee is for two-player zero-sum games. Supporting
  multiplayer utility vectors does not give multiplayer tournaments that guarantee.

The implementation follows the tabular method in
[Regret Minimization in Games with Incomplete Information](https://proceedings.neurips.cc/paper/2007/file/08d98638c6fcd194a4b1e6992063e944-Paper.pdf).

## Verification

The dependency-free test executable covers card encoding and invalid inputs,
arithmetic boundaries, tournament result validation and rollback, dead blinds,
every three-to-two-player elimination position, level progression, prize ties,
independent clones, and 100 seeded synthetic tournaments checking chip/prize
conservation. Synthetic results test tournament accounting; they do not test
unimplemented Hold'em betting or settlement.

Training tests cover pot-sized raises, short all-ins, deduplication, regret
normalization, reproducible sampling, learned-policy action alignment, and exact
checkpoint continuation. The independent Kuhn fixture checks the learned game
value against -1/18 and enumerates all 64 deterministic strategies for each player
to measure best-response exploitability after 5,000 iterations. These are meaningful
small-game checks, not evidence of full Hold'em playing strength.

## Next implementation steps

1. Concrete hand state and legal transitions, including forced bets, explicit
   chance nodes, minimum raises, reopening, fold wins, and all-in runouts.
2. A validated showdown evaluator, side pots, uncalled-bet refunds, ties, and
   settlement with chip conservation checks.
3. PHH history/replay and a public-information-preserving training adapter.
4. Sampled CFR, profiling/batched inference, neural models, and independent
   evaluation opponents before scaling tournament self-play.

Potential reference projects: [OpenSpiel](https://github.com/google-deepmind/open_spiel),
[PokerKit](https://github.com/uoftcprg/pokerkit),
[PokerHandEvaluator](https://github.com/HenryRLee/PokerHandEvaluator), and
the [PHH specification](https://phh.readthedocs.io/).
