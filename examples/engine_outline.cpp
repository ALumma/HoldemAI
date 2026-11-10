#include "holdem/evaluator.hpp"
#include "holdem/history.hpp"
#include "holdem/training.hpp"

#include <iostream>

int main() {
    holdem::TournamentConfig config;
    config.levels = {
        {{50, 100, 0, holdem::AnteKind::none}, 10},
        {{100, 200, 200, holdem::AnteKind::big_blind}, 10},
        {{150, 300, 300, holdem::AnteKind::big_blind}, 10},
    };
    config.prizes = {3'000, 1'800, 1'200}; // Example prizes in cents.
    holdem::SingleTableTournament tournament(config, {10, 20, 30, 40, 50, 60});
    const auto hand = tournament.next_hand();

    std::cout << "HoldemAI C++20 engine foundations\n"
              << "Seats: " << static_cast<unsigned>(config.seats) << '\n'
              << "Starting stack: " << config.starting_stack << '\n'
              << "Blind levels: " << config.levels.size() << '\n'
              << "First hand ID: " << hand.id << '\n'
              << "Button seat: " << static_cast<unsigned>(hand.button) << '\n'
              << "Big blind seat: " << static_cast<unsigned>(hand.blinds->big_blind) << '\n'
              << "Tournament management and tabular CFR are implemented.\n"
              << "Hold'em hand rules and neural training are not implemented yet.\n";
}
