#include "holdem/types.hpp"

#include <limits>
#include <stdexcept>

namespace holdem {
namespace {
constexpr std::string_view ranks = "23456789TJQKA";
constexpr std::string_view suits = "cdhs";
}

Card parse_card(std::string_view text) {
    if (text.size() != 2) throw std::invalid_argument("card must contain rank and suit");
    const auto rank = ranks.find(text[0]);
    const auto suit = suits.find(text[1]);
    if (rank == ranks.npos || suit == suits.npos) throw std::invalid_argument("invalid card notation");
    return {static_cast<Rank>(rank + 2), static_cast<Suit>(suit)};
}

std::uint8_t card_id(Card card) {
    const auto rank = static_cast<unsigned>(card.rank);
    const auto suit = static_cast<unsigned>(card.suit);
    if (rank < 2 || rank > 14 || suit > 3) throw std::invalid_argument("invalid card");
    return static_cast<std::uint8_t>((rank - 2) * 4 + suit);
}

Card card_from_id(std::uint8_t id) {
    if (id >= 52) throw std::invalid_argument("card ID must be below 52");
    return {static_cast<Rank>(id / 4 + 2), static_cast<Suit>(id % 4)};
}

std::string to_string(Card card) {
    const auto id = card_id(card);
    return {ranks[id / 4], suits[id % 4]};
}

std::array<Card, 52> standard_deck() {
    std::array<Card, 52> cards{};
    for (std::uint8_t i = 0; i < 52; ++i) cards[i] = card_from_id(i);
    return cards;
}

void validate_cards(std::span<const Card> cards) {
    std::uint64_t seen = 0;
    for (const auto card : cards) {
        const auto bit = std::uint64_t{1} << card_id(card);
        if (seen & bit) throw std::invalid_argument("duplicate card");
        seen |= bit;
    }
}

void validate(const Stakes& stakes) {
    if (stakes.small_blind <= 0 || stakes.big_blind < stakes.small_blind || stakes.ante < 0)
        throw std::invalid_argument("invalid blinds or ante");
    switch (stakes.ante_kind) {
    case AnteKind::none:
        if (stakes.ante != 0) throw std::invalid_argument("ante requires an ante kind");
        break;
    case AnteKind::per_player: case AnteKind::big_blind: break;
    default: throw std::invalid_argument("invalid ante kind");
    }
}

void validate(const LegalActions& actions) {
    if (actions.call_cost && (*actions.call_cost <= 0 || actions.can_check))
        throw std::invalid_argument("call must have a positive cost and exclude check");
    if (actions.full_raise && (actions.full_raise->minimum_to <= 0 ||
                             actions.full_raise->maximum_to < actions.full_raise->minimum_to))
        throw std::invalid_argument("invalid raise bounds");
    if (actions.short_all_in_to && (*actions.short_all_in_to <= 0 || actions.full_raise))
        throw std::invalid_argument("short all-in and full raise range are mutually exclusive");
}

bool is_legal(const LegalActions& legal, const Action& action) {
    validate(legal);
    if (action.kind != ActionKind::raise_to && action.raise_to != 0) return false;
    switch (action.kind) {
    case ActionKind::fold: return legal.can_fold;
    case ActionKind::check: return legal.can_check;
    case ActionKind::call: return legal.call_cost.has_value();
    case ActionKind::raise_to:
        return (legal.full_raise && action.raise_to >= legal.full_raise->minimum_to &&
                action.raise_to <= legal.full_raise->maximum_to) ||
               (legal.short_all_in_to && action.raise_to == *legal.short_all_in_to);
    }
    return false;
}

Chips checked_add(Chips left, Chips right) {
    constexpr auto max = std::numeric_limits<Chips>::max();
    constexpr auto min = std::numeric_limits<Chips>::min();
    if ((right > 0 && left > max - right) || (right < 0 && left < min - right))
        throw std::overflow_error("chip addition overflow");
    return left + right;
}

Chips checked_subtract(Chips left, Chips right) {
    constexpr auto max = std::numeric_limits<Chips>::max();
    constexpr auto min = std::numeric_limits<Chips>::min();
    if ((right > 0 && left < min + right) || (right < 0 && left > max + right))
        throw std::overflow_error("chip subtraction overflow");
    return left - right;
}
} // namespace holdem
