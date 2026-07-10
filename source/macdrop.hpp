// macOS Finder drag-and-drop of files onto the window.
//
// Slint 1.17 has no OS-level drop support (DropArea is app-internal only,
// and external drops are "in development upstream in winit"), so this shim
// retargets the NSDraggingDestination methods of the winit content view.
// TODO: replace with Slint's DropArea once it learns external file drops.
#pragma once

#include <functional>
#include <string>

namespace spdsx {

// Call after the window is shown. on_drop receives the drop position in
// the window's logical coordinates (origin top-left, matching Slint) and
// the dropped file's path, on the main thread.
void install_file_drop_handler(
    std::function<void(double x, double y, std::string path)> on_drop);

}  // namespace spdsx
