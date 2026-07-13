/*
 * AudioRecorder.h - records audio from selectable input devices
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

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "lmms_export.h"

namespace lmms
{

//! Records audio from one or more input devices simultaneously (multitrack
//! recording) and writes each capture to a WAV file when stopped. Each capture
//! is identified by a caller-provided id (e.g. a track's id).
class LMMS_EXPORT AudioRecorder
{
public:
	//! one selectable capture device as reported by PortAudio
	struct InputDevice
	{
		QString name;
		QString hostApi;	//!< e.g. "Windows WASAPI", "ASIO", "MME"
	};

	static AudioRecorder& instance();

	AudioRecorder(const AudioRecorder&) = delete;
	AudioRecorder& operator=(const AudioRecorder&) = delete;

	//! all devices with at least one input channel, in PortAudio order
	static std::vector<InputDevice> availableInputDevices();
	static QString deviceKey(const InputDevice& device) { return device.hostApi + "|" + device.name; }

	//! Start capturing for the given id from the device identified by key
	//! ("<hostApi>|<name>", empty = system default input). Returns false if
	//! the device cannot be opened.
	bool startCapture(int id, const QString& deviceKey);

	bool isRecording() const { return !m_captures.empty(); }

	//! Stop every capture, write each to a WAV file below the user's sample
	//! directory, and return a map of id -> file path (missing on failure).
	std::map<int, QString> stopAllAndSave();

private:
	AudioRecorder() = default;
	~AudioRecorder();

	struct Capture
	{
		int id = 0;
		void* stream = nullptr;
		int channels = 0;
		double sampleRate = 0.;
		std::mutex mutex;
		std::vector<float> data;
	};

	void appendFrames(Capture* capture, const float* interleaved, unsigned long frames);
	QString writeWav(Capture& capture);
	friend struct AudioRecorderCallback;

	bool m_paInitialized = false;
	std::vector<std::unique_ptr<Capture>> m_captures;
};

} // namespace lmms

#endif // LMMS_AUDIO_RECORDER_H
