#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace holdem {

using Chips = std::int64_t;
using Seat = std::uint8_t;       // Stable table slot, including eliminated seats.
using PlayerId = std::uint64_t;  // Stable identity across hands and table moves.
using HandId = std::uint64_t;

enum class Suit : std::uint8_t { clubs, diamonds, hearts, spades };
enum class Rank : std::uint8_t {
    two = 2, three, four, five, six, seven, eight, nine, ten, jack, queen, king, ace
};

struct Card {
    Rank rank;
    Suit suit;
    bool operator==(const Card&) const = default;
};

using HoleCards = std::array<Card, 2>;
enum class Street { preflop, flop, turn, river };
enum class NodeKind { chance, decision, terminal };
enum class PlayerStatus { active, folded, all_in, out };
enum class AnteKind { none, per_player, big_blind };

struct Stakes {
    Chips small_blind = 50;
    Chips big_blind = 100;
    Chips ante = 0;
    AnteKind ante_kind = AnteKind::none;
};

struct SeatAssignment {
    PlayerId player;
    Seat seat;
    Chips stack;
};

enum class ActionKind { fold, check, call, raise_to };

struct Action {
    ActionKind kind;
    // Total contribution on this street for raise_to; zero for other actions.
    // A bet into an unopened pot is also represented as raise_to.
    Chips raise_to = 0;
    bool operator==(const Action&) const = default;
};

struct RaiseRange {
    Chips minimum_to;
    Chips maximum_to;
};

struct LegalActions {
    bool can_fold = false;
    bool can_check = false;
    std::optional<Chips> call_cost; // Actual chips paid, capped by remaining stack.
    std::optional<RaiseRange> full_raise;
    // A legal all-in below the full minimum raise, when betting rights permit it.
    std::optional<Chips> short_all_in_to;
};

struct PublicPlayerState {
    PlayerId player;
    Seat seat;
    Chips stack;
    Chips committed_this_street;
    Chips committed_this_hand;
    PlayerStatus status;
    std::optional<HoleCards> revealed_cards; // Only publicly exposed cards.
};

struct BettingEvent {
    Street street;
    Seat actor;
    Action action;
    Chips chips_paid;
};

struct Pot {
    Chips amount;
    std::vector<Seat> eligible;
};

struct PlayerResult {
    PlayerId player;
    Chips final_stack;
    Chips net_chips; // final_stack - stack before forced bets.
};

struct HandResult {
    std::vector<PlayerResult> players;
    std::vector<Pot> pots; // Settled pots, retained for auditing.
};

// Strict notation: ranks 23456789TJQKA and suits cdhs, e.g. As or Td.
[[nodiscard]] Card parse_card(std::string_view text);
[[nodiscard]] std::string to_string(Card card);
[[nodiscard]] std::uint8_t card_id(Card card); // rank-major IDs in [0, 51].
[[nodiscard]] Card card_from_id(std::uint8_t id);
[[nodiscard]] std::array<Card, 52> standard_deck();
void validate_cards(std::span<const Card> cards);
void validate(const Stakes& stakes);
void validate(const LegalActions& actions);
[[nodiscard]] bool is_legal(const LegalActions& legal, const Action& action);
// Throw overflow_error rather than wrap; also usable for signed chip deltas.
[[nodiscard]] Chips checked_add(Chips left, Chips right);
[[nodiscard]] Chips checked_subtract(Chips left, Chips right);

} // namespace holdem
