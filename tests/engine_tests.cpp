#include "holdem/evaluator.hpp"
#include "holdem/history.hpp"
#include "holdem/training.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

using namespace holdem;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception = std::invalid_argument, class F>
void rejects(F&& action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}
void near(double actual, double expected, double tolerance = 1e-10) {
    require(std::abs(actual - expected) <= tolerance, "value outside tolerance");
}

TournamentConfig config(std::uint8_t seats = 4) {
    TournamentConfig result;
    result.seats = seats;
    result.starting_stack = 100;
    result.levels = {{{1, 2, 0, AnteKind::none}, 2}, {{2, 4, 4, AnteKind::big_blind}, 1}};
    result.prizes = {100, 50};
    return result;
}

HandResult outcome(const Tournament& tournament, const std::vector<Chips>& stacks) {
    HandResult result;
    for (const auto& player : tournament.next_hand().players)
        result.players.push_back({player.player, stacks.at(player.seat), stacks.at(player.seat) - player.stack});
    return result;
}

void test_types() {
    const auto deck = standard_deck();
    validate_cards(deck);
    for (std::uint8_t i = 0; i < deck.size(); ++i) {
        require(card_id(deck[i]) == i, "card ID mismatch");
        require(parse_card(to_string(deck[i])) == deck[i], "notation round trip failed");
    }
    require(to_string(parse_card("As")) == "As", "ace notation failed");
    for (const auto bad : {"", "AS", "10s", "1c", "asd"}) rejects([&] { (void)parse_card(bad); });
    rejects([] { (void)card_from_id(52); });
    rejects([] { (void)card_id({static_cast<Rank>(1), Suit::clubs}); });
    const std::array<Card, 2> duplicates{deck[0], deck[0]};
    rejects([&] { validate_cards(duplicates); });
    constexpr auto max = std::numeric_limits<Chips>::max();
    constexpr auto min = std::numeric_limits<Chips>::min();
    require(checked_add(max, -1) == max - 1 && checked_subtract(min, min) == 0, "checked arithmetic failed");
    require(checked_subtract(-1, min) == max && checked_add(min, max) == -1, "signed boundaries failed");
    rejects<std::overflow_error>([&] { (void)checked_add(max, 1); });
    rejects<std::overflow_error>([&] { (void)checked_add(min, -1); });
    rejects<std::overflow_error>([&] { (void)checked_subtract(min, 1); });
    rejects<std::overflow_error>([&] { (void)checked_subtract(max, -1); });
    validate(Stakes{});
    rejects([] { validate(Stakes{0, 100, 0, AnteKind::none}); });
    rejects([] { validate(Stakes{50, 100, 1, AnteKind::none}); });
    LegalActions legal{true, false, 100, RaiseRange{200, 1000}, std::nullopt};
    require(is_legal(legal, {ActionKind::raise_to, 200}), "minimum raise rejected");
    require(!is_legal(legal, {ActionKind::raise_to, 199}), "undersized raise accepted");
    require(!is_legal(legal, {ActionKind::call, 100}), "noncanonical call accepted");
    legal.full_raise.reset();
    legal.short_all_in_to = 150;
    require(is_legal(legal, {ActionKind::raise_to, 150}), "short all-in rejected");
    require(!is_legal(legal, {ActionKind::raise_to, 149}), "invalid short all-in accepted");
    legal.can_check = true;
    rejects([&] { validate(legal); });
}

void test_tournament_validation() {
    auto settings = config();
    rejects([&] { SingleTableTournament tournament(settings, {1, 2, 3, 3}); });
    rejects([&] { SingleTableTournament tournament(settings, {1, 2}); });
    settings.starting_stack = std::numeric_limits<Chips>::max();
    rejects<std::overflow_error>([&] { validate(settings); });
    settings = config(); settings.levels.clear();
    rejects([&] { validate(settings); });
    settings = config(); settings.levels[0].hands = 0;
    rejects([&] { validate(settings); });
    settings = config(); settings.prizes = {1, 2};
    rejects([&] { validate(settings); });
    settings = config(); settings.initial_button = 4;
    rejects([&] { validate(settings); });

    SingleTableTournament tournament(config(), {10, 20, 30, 40});
    const auto initial = tournament.next_hand();
    require(initial.id == 1 && initial.button == 0 && initial.blinds->small_blind == 1 && initial.blinds->big_blind == 2,
            "initial positions incorrect");
    rejects<std::logic_error>([&] { (void)tournament.awards(); });
    const auto valid = outcome(tournament, {200, 0, 100, 100});
    auto bad = valid;
    bad.players[0].net_chips = 99;
    rejects([&] { tournament.finish_hand(1, bad); });
    bad = valid; bad.players[0].final_stack = 201; bad.players[0].net_chips = 101;
    rejects([&] { tournament.finish_hand(1, bad); });
    bad = valid; bad.players[0].player = 999;
    rejects([&] { tournament.finish_hand(1, bad); });
    bad = valid; bad.players[0] = bad.players[1];
    rejects([&] { tournament.finish_hand(1, bad); });
    bad = valid; bad.players.pop_back();
    rejects([&] { tournament.finish_hand(1, bad); });
    bad = valid; bad.pots = {{500, {0}}};
    rejects([&] { tournament.finish_hand(1, bad); });
    require(tournament.next_hand().id == 1 && tournament.observe().standings[0].stack == 100,
            "invalid hand mutated tournament");
    auto branch = tournament.clone();
    tournament.finish_hand(1, valid);
    require(branch->next_hand().id == 1 && branch->observe().standings[1].stack == 100, "clone was mutated");
    rejects([&] { tournament.finish_hand(1, valid); });
    const auto second = tournament.next_hand();
    require(second.button == 1 && second.blinds->small_blind == 2 && second.blinds->big_blind == 3, "dead button incorrect");
    require(tournament.observe().hands_until_next_level == 1, "blind countdown incorrect");
    tournament.finish_hand(2, outcome(tournament, {200, 0, 100, 100}));
    require(tournament.observe().level == 1 && tournament.observe().hands_until_next_level == 0, "level transition incorrect");
    for (int i = 0; i < 10; ++i) tournament.finish_hand(tournament.next_hand().id, outcome(tournament, {200, 0, 100, 100}));
    require(tournament.observe().level == 1, "final blind level did not persist");
}

void test_tournament_positions_and_prizes() {
    SingleTableTournament dead(config(5), {0, 1, 2, 3, 4});
    dead.finish_hand(1, outcome(dead, {200, 100, 0, 100, 100}));
    auto hand = dead.next_hand();
    require(hand.button == 1 && !hand.blinds->small_blind && hand.blinds->big_blind == 3, "missing dead small blind");
    dead.finish_hand(2, outcome(dead, {200, 100, 0, 100, 100}));
    hand = dead.next_hand();
    require(hand.button == 2 && hand.blinds->small_blind == 3 && hand.blinds->big_blind == 4, "dead blind recovery failed");

    for (std::size_t eliminated = 0; eliminated < 3; ++eliminated) {
        SingleTableTournament tournament(config(3), {0, 1, 2});
        std::vector<Chips> stacks(3, 100);
        stacks[eliminated] = 0;
        stacks[(eliminated + 1) % 3] = 200;
        tournament.finish_hand(1, outcome(tournament, stacks));
        const auto next = tournament.next_hand();
        require(next.blinds->big_blind != 2 && next.blinds->small_blind == next.button,
                "heads-up transition repeated BB or lost button/SB");
        require(stacks[next.button] > 0 && stacks[next.blinds->big_blind] > 0, "heads-up blind assigned to eliminated seat");
        tournament.finish_hand(2, outcome(tournament, stacks));
        require(tournament.next_hand().blinds->big_blind == next.button, "heads-up blinds did not alternate");
    }
    auto settings = config();
    settings.prizes = {1001, 501, 200, 100};
    SingleTableTournament tournament(settings, {0, 1, 2, 3});
    tournament.finish_hand(1, outcome(tournament, {150, 100, 50, 100}));
    tournament.finish_hand(2, outcome(tournament, {400, 0, 0, 0}));
    require(tournament.is_finished(), "tournament did not finish");
    const auto standings = tournament.observe().standings;
    require(standings[0].finishing_place == 1 && standings[1].finishing_place == 2 &&
            standings[3].finishing_place == 2 && standings[2].finishing_place == 4, "tied elimination ranking incorrect");
    const auto awards = tournament.awards();
    require(awards[0].amount == 1001 && awards[1].amount == 351 && awards[3].amount == 350 && awards[2].amount == 100,
            "tied prizes or remainder incorrect");
    rejects<std::logic_error>([&] { (void)tournament.next_hand(); });
    rejects<std::logic_error>([&] { tournament.finish_hand(2, {}); });
}

void test_random_tournaments() {
    std::mt19937_64 rng(71);
    for (int run = 0; run < 100; ++run) {
        auto settings = config(6);
        settings.initial_button = static_cast<Seat>(run % 6);
        SingleTableTournament tournament(settings, {10, 11, 12, 13, 14, 15});
        while (!tournament.is_finished()) {
            const auto hand = tournament.next_hand();
            std::vector<Chips> stacks(6, 0);
            for (const auto& player : hand.players) stacks[player.seat] = player.stack;
            require(stacks[hand.blinds->big_blind] > 0, "BB has no stack");
            if (hand.blinds->small_blind) require(stacks[*hand.blinds->small_blind] > 0, "SB has no stack");
            const auto loser = rng() % hand.players.size();
            const auto winner = (loser + 1 + rng() % (hand.players.size() - 1)) % hand.players.size();
            stacks[hand.players[winner].seat] += stacks[hand.players[loser].seat];
            stacks[hand.players[loser].seat] = 0;
            tournament.finish_hand(hand.id, outcome(tournament, stacks));
            Chips total = 0;
            for (const auto& player : tournament.observe().standings) total += player.stack;
            require(total == 600, "random tournament lost chips");
        }
        std::int64_t total = 0;
        for (const auto& prize : tournament.awards()) total += prize.amount;
        require(total == 150, "random tournament lost prize money");
    }
}

DecisionInput decision() {
    DecisionInput input{};
    input.hand.viewer = 0;
    input.hand.actor = 0;
    input.hand.pot = 300;
    input.hand.players = {{10, 0, 950, 50, 50, PlayerStatus::active, std::nullopt}};
    input.hand.legal = {true, false, 50, RaiseRange{200, 1000}, std::nullopt};
    return input;
}

void test_training_helpers() {
    auto input = decision();
    const auto actions = PotBetAbstraction().candidates(input);
    const std::vector<Action> expected{{ActionKind::fold}, {ActionKind::call},
        {ActionKind::raise_to, 200}, {ActionKind::raise_to, 275}, {ActionKind::raise_to, 450}, {ActionKind::raise_to, 1000}};
    require(actions == expected, "pot-sized raise calculation incorrect");
    const auto uniform = UniformPolicy().probabilities(input, actions);
    validate_probabilities(uniform);
    near(uniform[0], 1.0 / 6);
    input.hand.players[0].stack = 100;
    input.hand.legal.full_raise.reset(); input.hand.legal.short_all_in_to = 150;
    require(PotBetAbstraction({}, false).candidates(input).back() == Action{ActionKind::raise_to, 150}, "lost short all-in");
    input.hand.legal.short_all_in_to.reset(); input.hand.legal.call_cost = 100;
    require(PotBetAbstraction().candidates(input).size() == 2, "all-in call has raise candidates");
    input.hand.actor = 1;
    rejects([&] { (void)PotBetAbstraction().candidates(input); });
    rejects([] { (void)PotBetAbstraction({{1, 0}}); });
    input = decision();
    const auto huge = std::numeric_limits<std::uint32_t>::max();
    const auto capped = PotBetAbstraction({{huge, 1}, {huge, huge}}).candidates(input);
    require(std::count(capped.begin(), capped.end(), Action{ActionKind::raise_to, 1000}) == 1, "clamping duplicated all-in");
    auto duplicate = actions; duplicate.push_back(actions[0]);
    rejects([&] { (void)UniformPolicy().probabilities(input, duplicate); });
    const auto policy = regret_matching(std::array{-2.0, 1.0, 3.0});
    near(policy[0], 0); near(policy[1], .25); near(policy[2], .75);
    near(regret_matching(std::array{-1.0, 0.0})[0], .5);
    near(regret_matching(std::array{1e308, 1e308})[0], .5);
    rejects([] { (void)regret_matching(std::array{std::numeric_limits<double>::infinity()}); });
    rejects([] { validate_probabilities(std::array{.1, .1}); });
    rejects([] { validate_probabilities(std::array{-1.0, 2.0}); });
    std::mt19937_64 rng1(42), rng2(42);
    for (int i = 0; i < 1000; ++i) {
        const auto a = sample_action(policy, rng1);
        require(a != 0 && a == sample_action(policy, rng2), "sampling not reproducible or sampled zero weight");
    }
}

// Independent three-card Kuhn poker fixture. The information key contains only
// the acting player's own card and public betting history.
class Kuhn final : public CfrState {
public:
    int first = -1, second = -1;
    std::string history;
    NodeKind node_kind() const override {
        if (first == -1) return NodeKind::chance;
        if (history == "cc" || history == "bc" || history == "bf" || history == "cbc" || history == "cbf") return NodeKind::terminal;
        return NodeKind::decision;
    }
    std::size_t current_player() const override { return history.empty() || history == "cb" ? 0 : 1; }
    std::string information_set() const override { return std::to_string(current_player() == 0 ? first : second) + ":" + history; }
    std::vector<Action> actions() const override {
        if (history.empty() || history == "c") return {{ActionKind::check}, {ActionKind::raise_to, 1}};
        return {{ActionKind::fold}, {ActionKind::call}};
    }
    std::unique_ptr<CfrState> child(const Action& action) const override {
        auto next = std::make_unique<Kuhn>(*this);
        next->history += action.kind == ActionKind::raise_to ? 'b' : action.kind == ActionKind::fold ? 'f' : 'c';
        return next;
    }
    std::vector<double> chance_probabilities() const override { return std::vector<double>(6, 1.0 / 6); }
    std::unique_ptr<CfrState> chance_child(std::size_t outcome) const override {
        auto next = std::make_unique<Kuhn>();
        next->first = static_cast<int>(outcome / 2);
        next->second = static_cast<int>(outcome % 2);
        if (next->second >= next->first) ++next->second;
        return next;
    }
    std::vector<double> utilities() const override {
        double value;
        if (history == "bf") value = 1;
        else if (history == "cbf") value = -1;
        else value = (first > second ? 1.0 : -1.0) * (history == "cc" ? 1 : 2);
        return {value, -value};
    }
};

double evaluate(const Kuhn& state, const TabularCfr& trainer, int best_player = -1, unsigned choices = 0) {
    if (state.node_kind() == NodeKind::terminal) return state.utilities()[0];
    if (state.node_kind() == NodeKind::chance) {
        double value = 0;
        for (std::size_t i = 0; i < 6; ++i) {
            const auto child = state.chance_child(i);
            value += evaluate(static_cast<const Kuhn&>(*child), trainer, best_player, choices) / 6;
        }
        return value;
    }
    const auto player = state.current_player();
    const auto actions = state.actions();
    auto strategy = trainer.average_strategy({player, state.information_set()});
    if (static_cast<int>(player) == best_player) {
        const auto card = player == 0 ? state.first : state.second;
        const auto slot = card * 2 + (player == 0 ? state.history == "cb" : state.history == "b");
        const auto action = (choices >> slot) & 1;
        strategy = {action == 0 ? 1.0 : 0.0, action == 1 ? 1.0 : 0.0};
    }
    double value = 0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const auto child = state.child(actions[i]);
        value += strategy[i] * evaluate(static_cast<const Kuhn&>(*child), trainer, best_player, choices);
    }
    return value;
}

void test_cfr() {
    Kuhn root;
    TabularCfr trainer(2);
    trainer.train(root, 5000);
    require(trainer.entries().size() == 12 && trainer.iterations() == 5000, "wrong Kuhn information sets or iteration count");
    const auto value = evaluate(root, trainer);
    near(value, -1.0 / 18, .005);
    double first_best = -10, second_best = -10;
    for (unsigned strategy = 0; strategy < 64; ++strategy) {
        first_best = std::max(first_best, evaluate(root, trainer, 0, strategy));
        second_best = std::max(second_best, -evaluate(root, trainer, 1, strategy));
    }
    const auto exploitability = (first_best + second_best) / 2;
    require(exploitability < .02, "Kuhn strategy remains exploitable");
    std::cout << "Kuhn: value=" << value << ", exploitability=" << exploitability << '\n';

    AverageStrategyPolicy learned(std::make_shared<TabularCfr>(trainer),
        [](const DecisionInput&) { return InformationSet{0, "0:"}; });
    auto input = decision();
    input.hand.legal = {false, true, std::nullopt, RaiseRange{1, 10}, std::nullopt};
    const std::array<Action, 2> choices{{{ActionKind::check}, {ActionKind::raise_to, 1}}};
    require(learned.probabilities(input, choices) == trainer.average_strategy({0, "0:"}), "policy bridge changed strategy");
    const std::array<Action, 2> reversed{{choices[1], choices[0]}};
    rejects([&] { (void)learned.probabilities(input, reversed); });

    std::stringstream checkpoint;
    trainer.save(checkpoint);
    TabularCfr resumed(2);
    resumed.load(checkpoint);
    resumed.train(root, 10); trainer.train(root, 10);
    std::stringstream a, b;
    trainer.save(a); resumed.save(b);
    require(a.str() == b.str(), "checkpoint resume diverged");
    std::stringstream corrupt("HOLDEM_CFR_V1 2 10 1\n0 \"x\" 1\n0 0 0 -1\n");
    rejects([&] { resumed.load(corrupt); });
    std::stringstream after; resumed.save(after);
    require(after.str() == b.str(), "invalid checkpoint mutated trainer");
    std::stringstream mismatch(a.str());
    TabularCfr different(3);
    rejects([&] { different.load(mismatch); });
    TabularCfr limited(2, 1);
    rejects([&] { limited.train(root); });
    require(limited.entries().empty() && limited.iterations() == 0, "failed iteration was partially committed");
}
}

int main() {
    try {
        test_types();
        test_tournament_validation();
        test_tournament_positions_and_prizes();
        test_random_tournaments();
        test_training_helpers();
        test_cfr();
        std::cout << "All engine foundation tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
