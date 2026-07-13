/*
 * TimeStretch.cpp - offline pitch-preserving time-stretching (WSOLA)
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

#include "TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace lmms
{

namespace
{
	// WSOLA parameters (in frames). Chosen for musical material at typical
	// sample rates; larger windows are smoother but blur transients.
	constexpr int WINDOW = 2048;         //!< analysis/synthesis window length
	constexpr int SYNTH_HOP = WINDOW / 2;//!< synthesis hop (50% overlap)
	constexpr int SEARCH = 512;          //!< +/- search range for best overlap

	//! normalized cross-correlation of the left channel between the previous
	//! synthesis tail and a candidate input position
	float similarity(const std::vector<SampleFrame>& in, int candidate,
			const std::vector<float>& tail, int overlap)
	{
		double acc = 0.0;
		const int total = static_cast<int>(in.size());
		for (int i = 0; i < overlap; ++i)
		{
			const int idx = candidate + i;
			const float s = (idx >= 0 && idx < total) ? in[idx].left() : 0.f;
			acc += static_cast<double>(s) * tail[i];
		}
		return static_cast<float>(acc);
	}
} // namespace




std::vector<SampleFrame> timeStretch(const SampleFrame* input, size_t numFrames, double factor)
{
	std::vector<SampleFrame> in(input, input + numFrames);
	if (numFrames == 0 || factor <= 0.0) { return in; }
	// a factor of ~1 or a very short sample: nothing to do
	if (std::abs(factor - 1.0) < 1e-3 || static_cast<int>(numFrames) < WINDOW * 2)
	{
		return in;
	}

	const int total = static_cast<int>(numFrames);
	const int overlap = SYNTH_HOP;                       // overlap between windows
	const double analysisHop = SYNTH_HOP / factor;       // input advance per window

	const auto outFrames = static_cast<size_t>(numFrames * factor) + WINDOW;
	std::vector<SampleFrame> out(outFrames, SampleFrame(0.f, 0.f));
	std::vector<float> norm(outFrames, 0.f);             // window-sum for normalization

	// Hann window
	std::vector<float> win(WINDOW);
	for (int i = 0; i < WINDOW; ++i)
	{
		win[i] = 0.5f * (1.f - std::cos(2.f * std::numbers::pi_v<float> * i / (WINDOW - 1)));
	}

	// the tail of the previous synthesis window (left channel), used to find
	// the most similar next input segment (waveform similarity)
	std::vector<float> tail(overlap, 0.f);

	double analysisPos = 0.0;
	int outPos = 0;
	bool first = true;

	while (analysisPos < total)
	{
		int base = static_cast<int>(analysisPos);
		int best = base;

		if (!first)
		{
			// search around `base` for the segment best matching `tail`
			float bestScore = -1e30f;
			const int lo = std::max(0, base - SEARCH);
			const int hi = std::min(total - 1, base + SEARCH);
			for (int cand = lo; cand <= hi; ++cand)
			{
				const float score = similarity(in, cand, tail, overlap);
				if (score > bestScore) { bestScore = score; best = cand; }
			}
		}

		// overlap-add this window into the output
		for (int i = 0; i < WINDOW; ++i)
		{
			const int inIdx = best + i;
			if (inIdx < 0 || inIdx >= total) { continue; }
			const int o = outPos + i;
			if (o < 0 || o >= static_cast<int>(outFrames)) { continue; }
			const float w = win[i];
			out[o].left() += in[inIdx].left() * w;
			out[o].right() += in[inIdx].right() * w;
			norm[o] += w;
		}

		// remember the tail for the next similarity search
		for (int i = 0; i < overlap; ++i)
		{
			const int inIdx = best + SYNTH_HOP + i;
			tail[i] = (inIdx >= 0 && inIdx < total) ? in[inIdx].left() : 0.f;
		}

		outPos += SYNTH_HOP;
		analysisPos += analysisHop;
		first = false;
	}

	// normalize by the accumulated window weights and trim to the real length
	const auto finalLen = std::min(static_cast<size_t>(outPos + overlap), outFrames);
	out.resize(finalLen);
	for (size_t i = 0; i < finalLen; ++i)
	{
		const float n = (i < norm.size() && norm[i] > 1e-6f) ? norm[i] : 1.f;
		out[i].left() /= n;
		out[i].right() /= n;
	}
	return out;
}


} // namespace lmms
