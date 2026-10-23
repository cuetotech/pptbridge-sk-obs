#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pptbridge {

struct OperatorSlide {
  std::string title;
  std::string notes;
  std::string image_path;
};

struct OperatorSource {
  std::string id;
  std::string name;
};

struct OperatorDeck {
  std::string identity;
  std::string name;
  std::string path;
  std::string capture_status;
  std::string error;
  std::vector<OperatorSlide> slides;
  std::size_t current = 0;
  bool valid = false;
  bool loading = false;
  bool busy = false;
  bool live_enabled = false;
  bool live_ready = false;
  bool black = false;
  bool in_program = false;
};

enum class OperatorCommand { Show, Next, Previous, Black };

class OperatorBackend {
public:
  virtual ~OperatorBackend() = default;
  virtual std::vector<OperatorSource> Sources() = 0;
  virtual OperatorDeck Read(const std::string &source_id) = 0;
  virtual bool Execute(const std::string &source_id, const std::string &identity,
                       OperatorCommand command, std::size_t index,
                       std::string &error) = 0;
};

// Selection is deliberately not a presentation command. Only observed slide
// changes update history, including changes made with a physical clicker.
class OperatorSelection {
public:
  bool Observe(const std::string &identity, std::size_t count, std::size_t current)
  {
    const bool reset = identity_ != identity || count_ != count;
    if (reset) {
      identity_ = identity;
      count_ = count;
      selected_.reset();
      reported_.reset();
      previous_.reset();
    }
    if (count == 0 || current >= count) {
      selected_.reset();
      reported_.reset();
      previous_.reset();
      return reset;
    }
    if (reported_ && *reported_ != current)
      previous_ = reported_;
    reported_ = current;
    if (reset)
      selected_ = current;
    return reset;
  }
  bool Select(std::size_t index)
  {
    if (index >= count_) return false;
    selected_ = index;
    return true;
  }
  void ClearSelection() { selected_.reset(); }
  std::optional<std::size_t> Selected() const { return selected_; }
  std::optional<std::size_t> Previous() const { return previous_; }
private:
  std::string identity_;
  std::size_t count_ = 0;
  std::optional<std::size_t> selected_, reported_, previous_;
};

// A shared ticket survives until the asynchronous command actually finishes.
// Destruction also handles an exception while constructing its worker thread.
class OperatorCommandTicket {
public:
  explicit OperatorCommandTicket(std::atomic<unsigned> &counter) : counter_(counter)
  { counter_.fetch_add(1, std::memory_order_relaxed); }
  ~OperatorCommandTicket() { counter_.fetch_sub(1, std::memory_order_relaxed); }
  OperatorCommandTicket(const OperatorCommandTicket &) = delete;
  OperatorCommandTicket &operator=(const OperatorCommandTicket &) = delete;
private:
  std::atomic<unsigned> &counter_;
};

inline bool OperatorCanNavigate(const OperatorDeck &deck)
{
  return deck.valid && !deck.loading && !deck.busy && !deck.slides.empty() &&
         (!deck.live_enabled || deck.live_ready);
}

} // namespace pptbridge
