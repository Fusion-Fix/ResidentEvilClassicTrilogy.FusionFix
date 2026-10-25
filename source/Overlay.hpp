#pragma once
#include <windows.h>
#include <functional>

// The in-game settings overlay (Ctrl+F10, or Back + Start on a controller), drawn on the Direct3D 9 device
// of the game's DirectDraw wrapper. The game is paused while it is open.
namespace FixOverlay
{
    void Init();
    // True while the overlay is open, and briefly after it closes until its keys and buttons are released
    bool Blocks();
    // Call first in the game's window procedure; true: the overlay handled the message
    bool Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);
    // Runs on the render thread when Blocks changes
    void OnBlocking(std::function<void(bool)> callback);
}
