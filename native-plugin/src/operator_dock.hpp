#pragma once
namespace pptbridge {
void RegisterOperatorDock();
void UnregisterOperatorDock();
// Atomic, safe from OBS hotkey threads; never reads Qt widgets off the UI thread.
bool OperatorDockHasFocus();
}
