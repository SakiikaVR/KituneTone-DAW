/*
 * AudioRecorder.cpp - records audio from selectable input devices
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

#include "AudioRecorder.h"

#include "lmmsconfig.h"

#include <QDateTime>
#include <QDir>
#include <QDebug>

#ifdef LMMS_HAVE_PORTAUDIO
#include <portaudio.h>
#endif

#include <sndfile.h>

#include <algorithm>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"

namespace lmms
{

AudioRecorder& AudioRecorder::instance()
{
	static AudioRecorder recorder;
	return recorder;
}




#ifdef LMMS_HAVE_PORTAUDIO

struct AudioRecorderCallback
{
	static int process(const void* input, void* /*output*/, unsigned long frameCount,
			const PaStreamCallbackTimeInfo* /*timeInfo*/,
			PaStreamCallbackFlags /*statusFlags*/, void* userData)
	{
		auto capture = static_cast<AudioRecorder::Capture*>(userData);
		if (input != nullptr && capture != nullptr)
		{
			AudioRecorder::instance().appendFrames(capture,
					static_cast<const float*>(input), frameCount);
		}
		return paContinue;
	}
};




std::vector<AudioRecorder::InputDevice> AudioRecorder::availableInputDevices()
{
	std::vector<InputDevice> devices;

	const bool needInit = Pa_GetDeviceCount() < 0;
	if (needInit && Pa_Initialize() != paNoError) { return devices; }

	const PaDeviceIndex count = Pa_GetDeviceCount();
	for (PaDeviceIndex i = 0; i < count; ++i)
	{
		const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
		if (info == nullptr || info->maxInputChannels < 1) { continue; }
		const PaHostApiInfo* hostApi = Pa_GetHostApiInfo(info->hostApi);
		devices.push_back({QString::fromUtf8(info->name),
				hostApi != nullptr ? QString::fromUtf8(hostApi->name) : QString()});
	}

	if (needInit) { Pa_Terminate(); }
	return devices;
}




//! resolve a "<hostApi>|<name>" key to a PortAudio input device index, or the
//! default input if the key is empty / not found. Expects PortAudio init.
static PaDeviceIndex resolveInputDevice(const QString& key)
{
	if (!key.isEmpty())
	{
		const PaDeviceIndex count = Pa_GetDeviceCount();
		for (PaDeviceIndex i = 0; i < count; ++i)
		{
			const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
			if (info == nullptr || info->maxInputChannels < 1) { continue; }
			const PaHostApiInfo* hostApi = Pa_GetHostApiInfo(info->hostApi);
			const auto device = AudioRecorder::InputDevice{QString::fromUtf8(info->name),
					hostApi != nullptr ? QString::fromUtf8(hostApi->name) : QString()};
			if (AudioRecorder::deviceKey(device) == key) { return i; }
		}
		qWarning() << "AudioRecorder: input device" << key << "not found, using default";
	}
	return Pa_GetDefaultInputDevice();
}




bool AudioRecorder::startCapture(int id, const QString& deviceKey)
{
	if (!m_paInitialized)
	{
		if (Pa_Initialize() != paNoError) { return false; }
		m_paInitialized = true;
	}

	const PaDeviceIndex device = resolveInputDevice(deviceKey);
	if (device == paNoDevice)
	{
		qWarning() << "AudioRecorder: no input device available for capture" << id;
		if (m_captures.empty()) { Pa_Terminate(); m_paInitialized = false; }
		return false;
	}

	const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
	auto capture = std::make_unique<Capture>();
	capture->id = id;
	capture->channels = std::min(2, info->maxInputChannels);
	if (capture->channels < 1)
	{
		if (m_captures.empty()) { Pa_Terminate(); m_paInitialized = false; }
		return false;
	}

	PaStreamParameters params = {};
	params.device = device;
	params.channelCount = capture->channels;
	params.sampleFormat = paFloat32;
	params.suggestedLatency = info->defaultLowInputLatency;

	// prefer the engine's sample rate, fall back to the device default
	capture->sampleRate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
	PaStream* stream = nullptr;
	if (Pa_OpenStream(&stream, &params, nullptr, capture->sampleRate, paFramesPerBufferUnspecified,
			paNoFlag, &AudioRecorderCallback::process, capture.get()) != paNoError)
	{
		capture->sampleRate = info->defaultSampleRate;
		if (Pa_OpenStream(&stream, &params, nullptr, capture->sampleRate, paFramesPerBufferUnspecified,
				paNoFlag, &AudioRecorderCallback::process, capture.get()) != paNoError)
		{
			qWarning() << "AudioRecorder: could not open input stream for capture" << id;
			if (m_captures.empty()) { Pa_Terminate(); m_paInitialized = false; }
			return false;
		}
	}

	capture->data.reserve(static_cast<size_t>(capture->sampleRate) * capture->channels * 60);

	if (Pa_StartStream(stream) != paNoError)
	{
		Pa_CloseStream(stream);
		if (m_captures.empty()) { Pa_Terminate(); m_paInitialized = false; }
		return false;
	}

	capture->stream = stream;
	m_captures.push_back(std::move(capture));
	return true;
}




std::map<int, QString> AudioRecorder::stopAllAndSave()
{
	std::map<int, QString> result;
	if (m_captures.empty()) { return result; }

	for (auto& capture : m_captures)
	{
		auto stream = static_cast<PaStream*>(capture->stream);
		if (stream != nullptr)
		{
			Pa_StopStream(stream);
			Pa_CloseStream(stream);
			capture->stream = nullptr;
		}
		const QString file = writeWav(*capture);
		if (!file.isEmpty()) { result[capture->id] = file; }
	}

	m_captures.clear();
	if (m_paInitialized) { Pa_Terminate(); m_paInitialized = false; }
	return result;
}




QString AudioRecorder::writeWav(Capture& capture)
{
	std::vector<float> data;
	{
		std::lock_guard<std::mutex> lock(capture.mutex);
		data.swap(capture.data);
	}
	if (data.empty() || capture.channels < 1) { return QString(); }

	const QString dirPath = ConfigManager::inst()->userSamplesDir() + "recordings";
	QDir().mkpath(dirPath);
	const QString file = dirPath + "/rec_"
			+ QString::number(capture.id) + "_"
			+ QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz") + ".wav";

	SF_INFO sfinfo = {};
	sfinfo.samplerate = static_cast<int>(capture.sampleRate);
	sfinfo.channels = capture.channels;
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

	SNDFILE* sf = sf_open(file.toLocal8Bit().constData(), SFM_WRITE, &sfinfo);
	if (sf == nullptr)
	{
		qWarning() << "AudioRecorder: could not write" << file;
		return QString();
	}
	sf_writef_float(sf, data.data(), static_cast<sf_count_t>(data.size() / capture.channels));
	sf_close(sf);

	return file;
}

#else // !LMMS_HAVE_PORTAUDIO

std::vector<AudioRecorder::InputDevice> AudioRecorder::availableInputDevices() { return {}; }
bool AudioRecorder::startCapture(int, const QString&) { return false; }
std::map<int, QString> AudioRecorder::stopAllAndSave() { return {}; }
QString AudioRecorder::writeWav(Capture&) { return QString(); }

#endif




AudioRecorder::~AudioRecorder()
{
#ifdef LMMS_HAVE_PORTAUDIO
	for (auto& capture : m_captures)
	{
		if (capture->stream != nullptr)
		{
			Pa_StopStream(static_cast<PaStream*>(capture->stream));
			Pa_CloseStream(static_cast<PaStream*>(capture->stream));
		}
	}
	if (m_paInitialized) { Pa_Terminate(); }
#endif
}




void AudioRecorder::appendFrames(Capture* capture, const float* interleaved, unsigned long frames)
{
	if (capture == nullptr) { return; }
	std::lock_guard<std::mutex> lock(capture->mutex);
	capture->data.insert(capture->data.end(), interleaved,
			interleaved + frames * capture->channels);
}


} // namespace lmms
