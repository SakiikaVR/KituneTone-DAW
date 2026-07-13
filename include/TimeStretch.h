/*
 * TimeStretch.h - offline pitch-preserving time-stretching (WSOLA)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_TIME_STRETCH_H
#define LMMS_TIME_STRETCH_H

#include <vector>

#include "SampleFrame.h"
#include "lmms_export.h"

namespace lmms
{

//! Time-stretch a stereo sample by `factor` while preserving pitch, using a
//! WSOLA (waveform-similarity overlap-add) algorithm. factor > 1 makes the
//! result longer (slower), factor < 1 shorter (faster). Runs offline.
LMMS_EXPORT std::vector<SampleFrame> timeStretch(
		const SampleFrame* input, size_t numFrames, double factor);

} // namespace lmms

#endif // LMMS_TIME_STRETCH_H
