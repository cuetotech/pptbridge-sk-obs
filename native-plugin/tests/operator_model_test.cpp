#include "operator_model.hpp"
#include <iostream>
#include <stdexcept>
using namespace pptbridge;
int checks = 0;
void require(bool condition, const char *message)
{
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
int main()
{
  try {
    OperatorSelection state;
    require(state.Observe("deck-a:1", 30, 5), "initial identity resets");
    require(state.Selected() == 5, "initial private selection");
    require(state.Select(12), "valid selection");
    require(!state.Previous(), "selection is not navigation");
    state.Observe("deck-a:1", 30, 5);
    require(state.Selected() == 12, "polling preserves private selection");
    state.Observe("deck-a:1", 30, 12);
    require(state.Previous() == 5, "only observation records history");
    state.Observe("deck-a:1", 30, 5);
    require(state.Previous() == 12, "back toggles last shown");
    require(!state.Select(30), "out of range rejected");
    state.Observe("deck-a:2", 30, 7);
    require(!state.Previous() && state.Selected() == 7, "reload resets stale mapping");
    state.Observe("deck-b:1", 30, 0);
    require(!state.Previous() && state.Selected() == 0, "deck switch resets history");
    state.ClearSelection();
    state.Observe("deck-b:1", 30, 0);
    require(!state.Selected(), "filtered-out selection is not reselected by polling");
    state.Observe("deck-b:1", 2, 1);
    require(state.Selected() == 1, "slide count change resets selection");
    state.Observe("", 0, 0);
    require(!state.Selected() && !state.Previous(), "removed source resets");
    OperatorDeck deck;
    require(!OperatorCanNavigate(deck), "missing deck disabled");
    deck.valid = true;
    deck.slides.resize(3);
    require(OperatorCanNavigate(deck), "cached/pdf navigation available");
    deck.live_enabled = true;
    require(!OperatorCanNavigate(deck), "unready live cannot silently use cache");
    deck.live_ready = true;
    require(OperatorCanNavigate(deck), "ready live available");
    deck.busy = true;
    require(!OperatorCanNavigate(deck), "pending command blocks a new one");
    deck.busy = false;
    deck.loading = true;
    require(!OperatorCanNavigate(deck), "loading blocks stale slides");
    std::atomic<unsigned> counter{0};
    { OperatorCommandTicket a(counter); OperatorCommandTicket b(counter);
      require(counter == 2, "tickets track concurrent commands"); }
    require(counter == 0, "tickets release commands");
    std::cout << checks << " model checks passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
