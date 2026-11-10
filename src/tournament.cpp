#include "holdem/tournament.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace holdem {

void validate(const TournamentConfig& config) {
    if (config.seats < 2 || config.seats > 10 || config.starting_stack <= 0 ||
        config.initial_button >= config.seats)
        throw std::invalid_argument("tournament requires 2..10 seats, positive stacks, and a valid button");
    Chips chips = 0;
    for (unsigned i = 0; i < config.seats; ++i) chips = checked_add(chips, config.starting_stack);
    if (config.levels.empty() || config.levels.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("tournament requires a blind schedule");
    Stakes previous = config.levels.front().stakes;
    for (const auto& level : config.levels) {
        validate(level.stakes);
        if (level.hands == 0 || level.stakes.small_blind < previous.small_blind ||
            level.stakes.big_blind < previous.big_blind)
            throw std::invalid_argument("levels require positive durations and nondecreasing blinds");
        previous = level.stakes;
    }
    if (config.prizes.empty() || config.prizes.size() > config.seats || config.prizes.front() <= 0)
        throw std::invalid_argument("provide a positive prize schedule within the field size");
    std::int64_t pool = 0;
    auto last = config.prizes.front();
    for (const auto prize : config.prizes) {
        if (prize < 0 || prize > last) throw std::invalid_argument("prizes must be nonnegative and nonincreasing");
        pool = checked_add(pool, prize);
        last = prize;
    }
}

SingleTableTournament::SingleTableTournament(TournamentConfig config, std::vector<PlayerId> players)
    : config_(std::move(config)) {
    validate(config_);
    if (players.size() != config_.seats || std::set<PlayerId>(players.begin(), players.end()).size() != players.size())
        throw std::invalid_argument("provide one distinct player per seat");
    for (const auto player : players) standings_.push_back({player, config_.starting_stack, std::nullopt});
    awarded_.resize(players.size());
    button_ = config_.initial_button;
    small_blind_anchor_ = players.size() == 2 ? button_ : next_live(button_);
    big_blind_ = next_live(small_blind_anchor_);
}

Seat SingleTableTournament::next_live(Seat after) const {
    for (std::size_t offset = 1; offset <= standings_.size(); ++offset) {
        const auto seat = (after + offset) % standings_.size();
        if (standings_[seat].stack > 0) return static_cast<Seat>(seat);
    }
    throw std::logic_error("tournament has no surviving player");
}

std::unique_ptr<Tournament> SingleTableTournament::clone() const {
    return std::make_unique<SingleTableTournament>(*this);
}

bool SingleTableTournament::is_finished() const {
    return std::count_if(standings_.begin(), standings_.end(), [](const auto& p) { return p.stack > 0; }) == 1;
}

HandSetup SingleTableTournament::next_hand() const {
    if (is_finished()) throw std::logic_error("tournament is finished");
    HandSetup setup;
    setup.id = next_id_;
    setup.stakes = config_.levels[level_].stakes;
    setup.button = button_;
    setup.blinds = BlindPositions{std::nullopt, big_blind_};
    if (standings_[small_blind_anchor_].stack > 0) setup.blinds->small_blind = small_blind_anchor_;
    for (std::size_t seat = 0; seat < standings_.size(); ++seat) {
        const auto& player = standings_[seat];
        if (player.stack > 0) setup.players.push_back({player.player, static_cast<Seat>(seat), player.stack});
    }
    return setup;
}

void SingleTableTournament::assign_prizes(std::span<const std::size_t> seats, std::uint32_t place) {
    std::int64_t pool = 0;
    for (std::size_t rank = place - 1; rank < place - 1 + seats.size(); ++rank)
        if (rank < config_.prizes.size()) pool = checked_add(pool, config_.prizes[rank]);
    // Deterministic remainder rule: lower original seat receives the extra unit.
    auto ordered = std::vector<std::size_t>(seats.begin(), seats.end());
    std::sort(ordered.begin(), ordered.end());
    const auto count = static_cast<std::int64_t>(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        standings_[ordered[i]].finishing_place = place;
        awarded_[ordered[i]] = pool / count + (static_cast<std::int64_t>(i) < pool % count ? 1 : 0);
    }
}

void SingleTableTournament::finish_hand(HandId id, const HandResult& result) {
    // Transactional: malformed results must not partially advance the tournament.
    auto next = *this;
    next.apply_result(id, result);
    *this = std::move(next);
}

void SingleTableTournament::apply_result(HandId id, const HandResult& result) {
    if (is_finished()) throw std::logic_error("tournament is finished");
    if (id != next_id_) throw std::invalid_argument("unexpected or already consumed hand ID");
    const auto setup = next_hand();
    if (result.players.size() != setup.players.size()) throw std::invalid_argument("hand result has wrong field size");
    std::set<PlayerId> seen;
    auto final_stacks = std::vector<Chips>(standings_.size(), 0);
    Chips before = 0, after = 0;
    for (const auto& entry : result.players) {
        const auto it = std::find_if(setup.players.begin(), setup.players.end(),
            [&](const auto& player) { return player.player == entry.player; });
        if (it == setup.players.end() || !seen.insert(entry.player).second || entry.final_stack < 0)
            throw std::invalid_argument("unknown, duplicate, or invalid player result");
        if (entry.net_chips != checked_subtract(entry.final_stack, it->stack))
            throw std::invalid_argument("net chip delta disagrees with final stack");
        before = checked_add(before, it->stack);
        after = checked_add(after, entry.final_stack);
        final_stacks[it->seat] = entry.final_stack;
    }
    if (before != after) throw std::invalid_argument("hand result does not conserve chips");
    Chips pot_total = 0;
    for (const auto& pot : result.pots) {
        if (pot.amount <= 0 || pot.eligible.empty()) throw std::invalid_argument("invalid settled pot");
        std::set<Seat> eligible;
        for (const auto seat : pot.eligible) {
            if (seat >= standings_.size() || standings_[seat].stack == 0 || !eligible.insert(seat).second)
                throw std::invalid_argument("invalid pot eligibility");
        }
        pot_total = checked_add(pot_total, pot.amount);
    }
    if (pot_total > before) throw std::invalid_argument("settled pots exceed chips in play");

    std::vector<std::size_t> eliminated;
    for (const auto& player : setup.players)
        if (final_stacks[player.seat] == 0) eliminated.push_back(player.seat);
    std::sort(eliminated.begin(), eliminated.end(), [&](auto left, auto right) {
        return standings_[left].stack > standings_[right].stack;
    });
    const auto survivors = setup.players.size() - eliminated.size();
    for (std::size_t first = 0; first < eliminated.size();) {
        auto end = first + 1;
        while (end < eliminated.size() && standings_[eliminated[first]].stack == standings_[eliminated[end]].stack) ++end;
        assign_prizes(std::span(eliminated).subspan(first, end - first),
                      static_cast<std::uint32_t>(survivors + first + 1));
        first = end;
    }
    for (std::size_t seat = 0; seat < standings_.size(); ++seat) standings_[seat].stack = final_stacks[seat];
    if (survivors == 1) {
        const std::array<std::size_t, 1> winner{next_live(0)};
        assign_prizes(winner, 1);
        return;
    }
    if (next_id_ == std::numeric_limits<HandId>::max()) throw std::overflow_error("hand ID exhausted");
    ++next_id_;
    if (level_ + 1 < config_.levels.size() && ++hands_in_level_ == config_.levels[level_].hands) {
        ++level_;
        hands_in_level_ = 0;
    }
    const auto old_big_blind = big_blind_;
    big_blind_ = next_live(old_big_blind);
    if (survivors == 2) {
        // The next surviving player pays BB; no player pays it twice on transition.
        button_ = small_blind_anchor_ = next_live(big_blind_);
    } else {
        button_ = small_blind_anchor_;
        small_blind_anchor_ = old_big_blind;
    }
}

TournamentObservation SingleTableTournament::observe() const {
    const auto remaining = level_ + 1 < config_.levels.size() ? config_.levels[level_].hands - hands_in_level_ : 0;
    return {level_, remaining, config_.levels, standings_, config_.prizes};
}

std::vector<PrizeAward> SingleTableTournament::awards() const {
    if (!is_finished()) throw std::logic_error("final awards require a completed tournament");
    std::vector<PrizeAward> awards;
    for (std::size_t seat = 0; seat < standings_.size(); ++seat)
        awards.push_back({standings_[seat].player, awarded_[seat]});
    return awards;
}
} // namespace holdem
