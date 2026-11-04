#pragma once

#include <chrono>
#include <cstddef>

namespace pptbridge {

// PowerPoint can briefly return a partial COM snapshot during slide transitions.
// Never interpret missing/invalid slide fields as slide 1.
inline bool LiveSnapshotFieldsValid(bool running, bool saw_running, bool saw_slide,
                                    bool saw_count, std::size_t slide, std::size_t count)
{
  if (!saw_running) return false;
  if (!running) return true; // An explicitly stopped slideshow is valid.
  return saw_slide && saw_count && count > 0 && slide > 0 && slide <= count;
}

// Hold the last confirmed live state briefly during transient polling failures.
// A persistent failure must still reach the existing recovery/watchdog path.
inline bool HoldPreviousLiveSnapshot(
  bool previously_ready,
  std::chrono::steady_clock::time_point &first_failure,
  std::chrono::steady_clock::time_point now,
  std::chrono::milliseconds grace)
{
  if (!previously_ready) return false;
  if (first_failure == std::chrono::steady_clock::time_point::min()) first_failure = now;
  return now - first_failure < grace;
}

} // namespace pptbridge
