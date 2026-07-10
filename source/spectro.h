// Spectrogram thumbnails for sample slots, rendered with specgram.
#ifndef SPDSX_PATCHEDIT_SOURCE_SPECTRO_H_
#define SPDSX_PATCHEDIT_SOURCE_SPECTRO_H_

#include <string>

namespace spdsx {

// Renders a spectrogram thumbnail PNG for wav_path and returns the PNG's
// path, or "" if the file is unreadable or shorter than one FFT window.
// Each call writes a fresh file (Slint caches images by path).
std::string render_spectrogram(const std::string& wav_path, int slot);

}  // namespace spdsx

#endif  // SPDSX_PATCHEDIT_SOURCE_SPECTRO_H_
