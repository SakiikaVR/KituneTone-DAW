/*
 * AudioRecorder.cpp - records audio from the default input device
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
#include "AudioPortAudio.h"
#endif

#include <sndfile.h>

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




void AudioRecorder::captureFromEngine(const float* interleaved, unsigned long frames, int channels, double sampleRate)
{
	if (!m_recording || !m_engineCapture) { return; }
	m_channels = channels;
	m_sampleRate = sampleRate;
	appendFrames(interleaved, frames);
}




#ifdef LMMS_HAVE_PORTAUDIO

struct AudioRecorderCallback
{
	static int process(const void* input, void* /*output*/, unsigned long frameCount,
			const PaStreamCallbackTimeInfo* /*timeInfo*/,
			PaStreamCallbackFlags /*statusFlags*/, void* userData)
	{
		auto recorder = static_cast<AudioRecorder*>(userData);
		if (input != nullptr)
		{
			recorder->appendFrames(static_cast<const float*>(input), frameCount);
		}
		return paContinue;
	}
};




bool AudioRecorder::start()
{
	if (m_recording) { return true; }

	// preferred path: the engine's PortAudio duplex stream already delivers
	// the configured input device to captureFromEngine()
	if (auto pa = dynamic_cast<AudioPortAudio*>(Engine::audioEngine()->audioDev());
		pa != nullptr && pa->inputChannels() > 0)
	{
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			m_data.clear();
			m_data.reserve(static_cast<size_t>(pa->sampleRate()) * pa->inputChannels() * 60);
		}
		m_channels = pa->inputChannels();
		m_sampleRate = static_cast<double>(pa->sampleRate());
		m_engineCapture = true;
		m_recording = true;
		return true;
	}

	// fallback: standalone stream on the system default input
	if (Pa_Initialize() != paNoError) { return false; }

	const PaDeviceIndex device = Pa_GetDefaultInputDevice();
	if (device == paNoDevice)
	{
		qWarning() << "AudioRecorder: no input device available";
		Pa_Terminate();
		return false;
	}

	const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
	m_channels = std::min(2, info->maxInputChannels);
	if (m_channels < 1)
	{
		Pa_Terminate();
		return false;
	}

	PaStreamParameters params = {};
	params.device = device;
	params.channelCount = m_channels;
	params.sampleFormat = paFloat32;
	params.suggestedLatency = info->defaultLowInputLatency;

	// prefer the engine's sample rate, fall back to the device default
	m_sampleRate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
	PaStream* stream = nullptr;
	if (Pa_OpenStream(&stream, &params, nullptr, m_sampleRate, paFramesPerBufferUnspecified,
			paNoFlag, &AudioRecorderCallback::process, this) != paNoError)
	{
		m_sampleRate = info->defaultSampleRate;
		if (Pa_OpenStream(&stream, &params, nullptr, m_sampleRate, paFramesPerBufferUnspecified,
				paNoFlag, &AudioRecorderCallback::process, this) != paNoError)
		{
			qWarning() << "AudioRecorder: could not open input stream";
			Pa_Terminate();
			return false;
		}
	}

	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		m_data.clear();
		// reserve one minute up front to reduce reallocations in the callback
		m_data.reserve(static_cast<size_t>(m_sampleRate) * m_channels * 60);
	}

	if (Pa_StartStream(stream) != paNoError)
	{
		Pa_CloseStream(stream);
		Pa_Terminate();
		return false;
	}

	m_stream = stream;
	m_engineCapture = false;
	m_recording = true;
	return true;
}




QString AudioRecorder::stopAndSave()
{
	if (!m_recording) { return QString(); }

	if (m_engineCapture)
	{
		m_recording = false;
		m_engineCapture = false;
	}
	else
	{
		auto stream = static_cast<PaStream*>(m_stream);
		Pa_StopStream(stream);
		Pa_CloseStream(stream);
		Pa_Terminate();
		m_stream = nullptr;
		m_recording = false;
	}

	std::vector<float> data;
	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		data.swap(m_data);
	}
	if (data.empty()) { return QString(); }

	const QString dirPath = ConfigManager::inst()->userSamplesDir() + "recordings";
	QDir().mkpath(dirPath);
	const QString file = dirPath + "/rec_"
			+ QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".wav";

	SF_INFO sfinfo = {};
	sfinfo.samplerate = static_cast<int>(m_sampleRate);
	sfinfo.channels = m_channels;
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

	SNDFILE* sf = sf_open(file.toLocal8Bit().constData(), SFM_WRITE, &sfinfo);
	if (sf == nullptr)
	{
		qWarning() << "AudioRecorder: could not write" << file;
		return QString();
	}
	sf_writef_float(sf, data.data(), static_cast<sf_count_t>(data.size() / m_channels));
	sf_close(sf);

	return file;
}

#else // !LMMS_HAVE_PORTAUDIO

bool AudioRecorder::start() { return false; }
QString AudioRecorder::stopAndSave() { return QString(); }

#endif




AudioRecorder::~AudioRecorder()
{
#ifdef LMMS_HAVE_PORTAUDIO
	if (m_recording && m_stream != nullptr)
	{
		Pa_StopStream(static_cast<PaStream*>(m_stream));
		Pa_CloseStream(static_cast<PaStream*>(m_stream));
		Pa_Terminate();
	}
#endif
}




void AudioRecorder::appendFrames(const float* interleaved, unsigned long frames)
{
	std::lock_guard<std::mutex> lock(m_dataMutex);
	m_data.insert(m_data.end(), interleaved, interleaved + frames * m_channels);
}


} // namespace lmms
