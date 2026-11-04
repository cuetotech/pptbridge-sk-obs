#include "live_snapshot_validation.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace pptbridge;
using Clock = std::chrono::steady_clock;

int main()
{
  auto check = [](bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
  };
  check(LiveSnapshotFieldsValid(true,true,true,true,4,12), "valid running slide");
  check(!LiveSnapshotFieldsValid(true,false,true,true,4,12), "missing running rejected");
  check(!LiveSnapshotFieldsValid(true,true,false,true,4,12), "missing slide rejected");
  check(!LiveSnapshotFieldsValid(true,true,true,false,4,12), "missing count rejected");
  check(!LiveSnapshotFieldsValid(true,true,true,true,0,12), "zero slide rejected");
  check(!LiveSnapshotFieldsValid(true,true,true,true,13,12), "out-of-range slide rejected");
  check(!LiveSnapshotFieldsValid(true,true,true,true,1,0), "zero count rejected");
  check(LiveSnapshotFieldsValid(false,true,true,true,0,0), "explicit stop accepted");

  auto since = Clock::time_point::min();
  const auto now = Clock::now();
  check(HoldPreviousLiveSnapshot(true,since,now,std::chrono::milliseconds(2500)), "first error held");
  check(HoldPreviousLiveSnapshot(true,since,now+std::chrono::milliseconds(2499),
    std::chrono::milliseconds(2500)), "transient error held");
  check(!HoldPreviousLiveSnapshot(true,since,now+std::chrono::milliseconds(2500),
    std::chrono::milliseconds(2500)), "persistent error released");
  check(!HoldPreviousLiveSnapshot(false,since,now,std::chrono::milliseconds(2500)),
    "not ready never held");
  std::cout << "12 live snapshot validation checks passed\n";
  return 0;
}
