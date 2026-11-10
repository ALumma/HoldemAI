#include "holdem/training.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace holdem {
namespace {
void validate_candidates(const DecisionInput& input, std::span<const Action> candidates) {
    if (input.hand.actor != input.hand.viewer || candidates.empty())
        throw std::invalid_argument("policy requires an acting player and candidates");
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!is_legal(input.hand.legal, candidates[i]) ||
            std::find(candidates.begin(), candidates.begin() + i, candidates[i]) != candidates.begin() + i)
            throw std::invalid_argument("candidate actions must be legal and unique");
    }
}

// floor(amount * numerator / denominator), capped without overflowing.
Chips fraction_of(Chips amount, PotFraction fraction, Chips cap) {
    const auto whole = static_cast<std::uint64_t>(amount) / fraction.denominator;
    const auto remainder = static_cast<std::uint64_t>(amount) % fraction.denominator;
    const auto limit = static_cast<std::uint64_t>(cap);
    if (whole > limit / fraction.numerator) return cap;
    const auto base = whole * fraction.numerator;
    const auto extra = remainder * fraction.numerator / fraction.denominator;
    return static_cast<Chips>(base + std::min(extra, limit - base));
}

double finite_add(double left, double right) {
    const auto result = left + right;
    if (!std::isfinite(result)) throw std::overflow_error("non-finite CFR accumulation");
    return result;
}

void validate_game_actions(std::span<const Action> actions) {
    if (actions.empty()) throw std::invalid_argument("decision node has no actions");
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto& action = actions[i];
        switch (action.kind) {
        case ActionKind::fold: case ActionKind::check: case ActionKind::call:
            if (action.raise_to != 0) throw std::invalid_argument("non-raise action has an amount");
            break;
        case ActionKind::raise_to:
            if (action.raise_to <= 0) throw std::invalid_argument("nonpositive raise");
            break;
        default: throw std::invalid_argument("invalid action kind");
        }
        if (std::find(actions.begin(), actions.begin() + i, action) != actions.begin() + i)
            throw std::invalid_argument("duplicate game action");
    }
}
}

PotBetAbstraction::PotBetAbstraction(std::vector<PotFraction> fractions, bool include_all_in)
    : fractions_(std::move(fractions)), include_all_in_(include_all_in) {
    for (const auto fraction : fractions_)
        if (fraction.numerator == 0 || fraction.denominator == 0)
            throw std::invalid_argument("pot fractions must be positive");
}

std::vector<Action> PotBetAbstraction::candidates(const DecisionInput& input) const {
    if (input.hand.actor != input.hand.viewer) throw std::invalid_argument("viewer is not the acting player");
    const auto& legal = input.hand.legal;
    validate(legal);
    const auto player = std::find_if(input.hand.players.begin(), input.hand.players.end(),
        [&](const auto& p) { return p.seat == input.hand.viewer; });
    if (player == input.hand.players.end() || player->status != PlayerStatus::active ||
        player->stack <= 0 || player->committed_this_street < 0 || input.hand.pot < 0)
        throw std::invalid_argument("invalid acting player state");
    const auto call = legal.call_cost.value_or(0);
    if (call > player->stack) throw std::invalid_argument("call exceeds stack");
    const auto all_in = checked_add(player->committed_this_street, player->stack);
    const auto called_to = checked_add(player->committed_this_street, call);
    if ((legal.full_raise && (legal.full_raise->maximum_to > all_in || legal.full_raise->minimum_to <= called_to)) ||
        (legal.short_all_in_to && (*legal.short_all_in_to != all_in || all_in <= called_to)))
        throw std::invalid_argument("raise bounds disagree with player stack or call");

    std::vector<Action> result;
    if (legal.can_fold) result.push_back({ActionKind::fold});
    if (legal.can_check) result.push_back({ActionKind::check});
    if (legal.call_cost) result.push_back({ActionKind::call});
    auto add_raise = [&](Chips amount) {
        const Action action{ActionKind::raise_to, amount};
        if (is_legal(legal, action) && std::find(result.begin(), result.end(), action) == result.end()) result.push_back(action);
    };
    if (legal.full_raise) {
        add_raise(legal.full_raise->minimum_to);
        const auto pot_after_call = checked_add(input.hand.pot, call);
        for (const auto fraction : fractions_) {
            const auto increment = fraction_of(pot_after_call, fraction, legal.full_raise->maximum_to - called_to);
            add_raise(std::max(legal.full_raise->minimum_to, called_to + increment));
        }
        if (include_all_in_) add_raise(all_in);
    }
    // Preserve a short all-in even if optional full-size all-ins are disabled.
    if (legal.short_all_in_to) add_raise(*legal.short_all_in_to);
    validate_candidates(input, result);
    return result;
}

std::vector<double> UniformPolicy::probabilities(const DecisionInput& input, std::span<const Action> candidates) const {
    validate_candidates(input, candidates);
    return std::vector<double>(candidates.size(), 1.0 / static_cast<double>(candidates.size()));
}

void validate_probabilities(std::span<const double> probabilities) {
    if (probabilities.empty()) throw std::invalid_argument("empty probability distribution");
    double sum = 0;
    for (const auto p : probabilities) {
        if (!std::isfinite(p) || p < 0 || p > 1) throw std::invalid_argument("invalid probability");
        sum += p;
    }
    if (!std::isfinite(sum) || std::abs(sum - 1) > 1e-10) throw std::invalid_argument("probabilities must sum to one");
}

std::vector<double> regret_matching(std::span<const double> regrets) {
    if (regrets.empty()) throw std::invalid_argument("empty regret vector");
    double maximum = 0;
    for (const auto regret : regrets) {
        if (!std::isfinite(regret)) throw std::invalid_argument("non-finite regret");
        maximum = std::max(maximum, regret);
    }
    std::vector<double> policy(regrets.size(), 1.0 / static_cast<double>(regrets.size()));
    if (maximum == 0) return policy;
    double sum = 0;
    for (std::size_t i = 0; i < regrets.size(); ++i) sum += policy[i] = std::max(0.0, regrets[i]) / maximum;
    for (auto& p : policy) p /= sum;
    return policy;
}

std::size_t sample_action(std::span<const double> probabilities, std::mt19937_64& rng) {
    validate_probabilities(probabilities);
    // Use the top 53 bits directly rather than implementation-specific distributions.
    const double draw = static_cast<double>(rng() >> 11) * 0x1.0p-53;
    double cumulative = 0;
    std::size_t last_positive = 0;
    for (std::size_t i = 0; i < probabilities.size(); ++i) {
        cumulative += probabilities[i];
        if (probabilities[i] > 0) last_positive = i;
        if (draw < cumulative) return i;
    }
    return last_positive; // Only floating-point normalization residue.
}

std::size_t CfrState::current_player() const { throw std::logic_error("not a decision node"); }
std::string CfrState::information_set() const { throw std::logic_error("not a decision node"); }
std::vector<Action> CfrState::actions() const { throw std::logic_error("not a decision node"); }
std::unique_ptr<CfrState> CfrState::child(const Action&) const { throw std::logic_error("not a decision node"); }
std::vector<double> CfrState::chance_probabilities() const { throw std::logic_error("not a chance node"); }
std::unique_ptr<CfrState> CfrState::chance_child(std::size_t) const { throw std::logic_error("not a chance node"); }
std::vector<double> CfrState::utilities() const { throw std::logic_error("not a terminal node"); }

TabularCfr::TabularCfr(std::size_t players, std::size_t max_depth) : players_(players), max_depth_(max_depth) {
    if (players < 2 || max_depth == 0) throw std::invalid_argument("CFR requires at least two players and positive depth");
}

void TabularCfr::train(const CfrState& root, std::uint64_t iterations) {
    if (iterations > std::numeric_limits<std::uint64_t>::max() - iterations_) throw std::overflow_error("iteration counter overflow");
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        // Each iteration commits atomically and uses one frozen strategy profile.
        auto next = entries_;
        std::map<InformationSet, std::vector<double>> strategies;
        std::map<InformationSet, double> own_reaches;
        std::set<InformationSet> active_path;
        std::function<std::vector<double>(const CfrState&, std::vector<double>, double, std::size_t)> visit;
        visit = [&](const CfrState& state, std::vector<double> reach, double chance_reach, std::size_t depth) {
            if (depth > max_depth_) throw std::invalid_argument("CFR tree exceeds maximum depth");
            std::vector<double> value(players_, 0);
            switch (state.node_kind()) {
            case NodeKind::terminal: {
                value = state.utilities();
                if (value.size() != players_ || std::any_of(value.begin(), value.end(), [](auto v) { return !std::isfinite(v); }))
                    throw std::invalid_argument("invalid terminal utilities");
                return value;
            }
            case NodeKind::chance: {
                const auto probabilities = state.chance_probabilities();
                validate_probabilities(probabilities);
                for (std::size_t i = 0; i < probabilities.size(); ++i) {
                    if (probabilities[i] == 0) continue;
                    const auto child = state.chance_child(i);
                    if (!child) throw std::invalid_argument("null chance child");
                    const auto utility = visit(*child, reach, chance_reach * probabilities[i], depth + 1);
                    for (std::size_t p = 0; p < players_; ++p) value[p] = finite_add(value[p], probabilities[i] * utility[p]);
                }
                return value;
            }
            case NodeKind::decision: break;
            default: throw std::invalid_argument("invalid node kind");
            }
            const auto player = state.current_player();
            if (player >= players_) throw std::invalid_argument("acting player outside field");
            const InformationSet key{player, state.information_set()};
            if (key.second.empty() || !active_path.insert(key).second)
                throw std::invalid_argument("empty or repeated information set on a path");
            const auto actions = state.actions();
            validate_game_actions(actions);
            auto [entry_it, inserted] = next.try_emplace(key, CfrEntry{actions,
                std::vector<double>(actions.size(), 0), std::vector<double>(actions.size(), 0)});
            auto& entry = entry_it->second;
            if (!inserted && entry.actions != actions) throw std::invalid_argument("inconsistent information-set actions");
            auto [strategy_it, first_visit] = strategies.try_emplace(key);
            if (first_visit) {
                strategy_it->second = regret_matching(entry.cumulative_regrets);
                own_reaches.emplace(key, reach[player]);
                for (std::size_t i = 0; i < actions.size(); ++i)
                    entry.strategy_sum[i] = finite_add(entry.strategy_sum[i], reach[player] * strategy_it->second[i]);
            } else if (std::abs(own_reaches.at(key) - reach[player]) > 1e-10) {
                throw std::invalid_argument("information set violates perfect recall");
            }
            const auto& strategy = strategy_it->second;
            std::vector<double> action_values(actions.size());
            for (std::size_t i = 0; i < actions.size(); ++i) {
                auto child_reach = reach;
                child_reach[player] *= strategy[i];
                const auto child = state.child(actions[i]);
                if (!child) throw std::invalid_argument("null decision child");
                const auto utility = visit(*child, std::move(child_reach), chance_reach, depth + 1);
                action_values[i] = utility[player];
                for (std::size_t p = 0; p < players_; ++p) value[p] = finite_add(value[p], strategy[i] * utility[p]);
            }
            auto counterfactual_reach = chance_reach;
            for (std::size_t p = 0; p < players_; ++p) if (p != player) counterfactual_reach *= reach[p];
            for (std::size_t i = 0; i < actions.size(); ++i)
                entry.cumulative_regrets[i] = finite_add(entry.cumulative_regrets[i],
                    counterfactual_reach * (action_values[i] - value[player]));
            active_path.erase(key);
            return value;
        };
        visit(root, std::vector<double>(players_, 1), 1, 0);
        entries_ = std::move(next);
        ++iterations_;
    }
}

std::vector<double> TabularCfr::average_strategy(const InformationSet& key) const {
    return regret_matching(entries_.at(key).strategy_sum);
}

void TabularCfr::save(std::ostream& out) const {
    out << "HOLDEM_CFR_V1 " << players_ << ' ' << iterations_ << ' ' << entries_.size() << '\n';
    for (const auto& [key, entry] : entries_) {
        out << key.first << ' ' << std::quoted(key.second) << ' ' << entry.actions.size() << '\n';
        for (std::size_t i = 0; i < entry.actions.size(); ++i)
            out << static_cast<int>(entry.actions[i].kind) << ' ' << entry.actions[i].raise_to << ' '
                << std::setprecision(std::numeric_limits<double>::max_digits10)
                << entry.cumulative_regrets[i] << ' ' << entry.strategy_sum[i] << '\n';
    }
    if (!out) throw std::runtime_error("failed to write CFR checkpoint");
}

void TabularCfr::load(std::istream& in) {
    std::string magic;
    std::size_t players, count;
    std::uint64_t iterations;
    if (!(in >> magic >> players >> iterations >> count) || magic != "HOLDEM_CFR_V1" || players != players_ || count > 1'000'000)
        throw std::invalid_argument("invalid or incompatible CFR checkpoint");
    std::map<InformationSet, CfrEntry> entries;
    for (std::size_t n = 0; n < count; ++n) {
        InformationSet key;
        std::size_t actions;
        if (!(in >> key.first >> std::quoted(key.second) >> actions) || key.first >= players_ ||
            key.second.empty() || key.second.size() > 1'000'000 || actions == 0 || actions > 10'000)
            throw std::invalid_argument("invalid checkpoint information set");
        CfrEntry entry;
        for (std::size_t i = 0; i < actions; ++i) {
            int kind;
            Chips amount;
            double regret, sum;
            if (!(in >> kind >> amount >> regret >> sum) || kind < 0 || kind > 3 ||
                !std::isfinite(regret) || !std::isfinite(sum) || sum < 0)
                throw std::invalid_argument("invalid checkpoint entry");
            entry.actions.push_back({static_cast<ActionKind>(kind), amount});
            entry.cumulative_regrets.push_back(regret);
            entry.strategy_sum.push_back(sum);
        }
        validate_game_actions(entry.actions);
        if (!entries.emplace(std::move(key), std::move(entry)).second) throw std::invalid_argument("duplicate checkpoint information set");
    }
    in >> std::ws;
    if (in.bad() || in.peek() != std::char_traits<char>::eof()) throw std::invalid_argument("trailing checkpoint data");
    entries_ = std::move(entries);
    iterations_ = iterations;
}

AverageStrategyPolicy::AverageStrategyPolicy(std::shared_ptr<const TabularCfr> trainer, Encoder encoder)
    : trainer_(std::move(trainer)), encoder_(std::move(encoder)) {
    if (!trainer_ || !encoder_) throw std::invalid_argument("average policy requires a trainer and information-set encoder");
}

std::vector<double> AverageStrategyPolicy::probabilities(const DecisionInput& input, std::span<const Action> candidates) const {
    validate_candidates(input, candidates);
    const auto key = encoder_(input);
    const auto& entry = trainer_->entries().at(key);
    if (!std::equal(entry.actions.begin(), entry.actions.end(), candidates.begin(), candidates.end()))
        throw std::invalid_argument("policy candidates disagree with trained action ordering");
    return trainer_->average_strategy(key);
}
} // namespace holdem
