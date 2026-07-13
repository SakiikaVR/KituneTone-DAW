/*
 * AudioRecorder.h - records audio from the default input device
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

#ifndef LMMS_AUDIO_RECORDER_H
#define LMMS_AUDIO_RECORDER_H

#include <QString>

#include <mutex>
#include <vector>

#include "lmms_export.h"

namespace lmms
{

//! Records audio into memory and saves it as a WAV file when stopped. The
//! samples come from the input device configured for the audio interface
//! (fed in by AudioPortAudio's duplex stream); if the engine has no capture
//! channel, a standalone stream on the system default input is used instead.
class LMMS_EXPORT AudioRecorder
{
public:
	static AudioRecorder& instance();

	//! begin capturing; returns false if no input device is available
	bool start();

	bool isRecording() const { return m_recording; }

	//! called from the audio interface's callback with the frames of the
	//! configured input device; ignored unless recording in engine mode
	void captureFromEngine(const float* interleaved, unsigned long frames, int channels, double sampleRate);

	//! stop capturing, write the recording to a WAV file below the user's
	//! sample directory and return its path (empty string on failure)
	QString stopAndSave();

private:
	AudioRecorder() = default;
	~AudioRecorder();

	void appendFrames(const float* interleaved, unsigned long frames);
	friend struct AudioRecorderCallback;

	void* m_stream = nullptr;
	bool m_recording = false;
	bool m_engineCapture = false;	//!< frames arrive via captureFromEngine()
	int m_channels = 0;
	double m_sampleRate = 0.;
	std::mutex m_dataMutex;
	std::vector<float> m_data;
};

} // namespace lmms

#endif // LMMS_AUDIO_RECORDER_H
