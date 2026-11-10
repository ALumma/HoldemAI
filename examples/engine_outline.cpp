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

    std::cout << "HoldemAI C++20 engine outline\n"
              << "Seats: " << static_cast<unsigned>(config.seats) << '\n'
              << "Starting stack: " << config.starting_stack << '\n'
              << "Blind levels: " << config.levels.size() << '\n'
              << "Interfaces compile; poker rules are not implemented yet.\n";
}
