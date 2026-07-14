/*
 * AudioPortAudio.cpp - device-class that performs PCM-output via PortAudio
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
 * Copyright (c) 2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <iostream>
#include <vector>

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_PORTAUDIO

#include "AudioEngine.h"
#include "AudioPortAudio.h"
#include "ConfigManager.h"
#include "LcdSpinBox.h"

#include <QCheckBox>

#ifdef LMMS_BUILD_WIN32
#include <pa_win_wasapi.h>
#endif

namespace {
enum class Direction
{
	Input,
	Output
};

constexpr auto tag()
{
	return "audioportaudio";
}

constexpr auto backendAttribute()
{
	return "backend";
}

constexpr auto wasapiExclusiveAttribute()
{
	return "wasapiexclusive";
}

constexpr auto wasapiBackendName()
{
	return "Windows WASAPI";
}

constexpr auto deviceNameAttribute(Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return "inputdevice";
	case Direction::Output:
		return "outputdevice";
	}

	return "";
}

constexpr auto channelsAttribute(Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return "inputchannels";
	case Direction::Output:
		return "outputchannels";
	}

	return "";
}

int maxChannels(const PaDeviceInfo* info, Direction direction)
{
	switch (direction)
	{
	case Direction::Input:
		return info->maxInputChannels;
	case Direction::Output:
		return info->maxOutputChannels;
	}

	return 0;
}

} // namespace

namespace lmms {
AudioPortAudio::AudioPortAudio(bool& successful, AudioEngine* engine)
	: AudioDevice(DEFAULT_CHANNELS, engine)
{
	const auto numDevices = Pa_GetDeviceCount();
	if (numDevices < 0)
	{
		std::cerr << "Pa_GetDeviceCount() failed: " << Pa_GetErrorText(numDevices) << '\n';
		successful = false;
		return;
	}

	const auto backend = ConfigManager::inst()->value(tag(), backendAttribute());

	const auto inputDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(Direction::Input));
	auto inputDeviceChannels = ConfigManager::inst()->value(tag(), channelsAttribute(Direction::Input)).toInt();

	const auto outputDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(Direction::Output));
	const auto outputDeviceChannels = ConfigManager::inst()->value(tag(), channelsAttribute(Direction::Output)).toInt();

	auto inputDeviceIndex = paNoDevice;
	auto outputDeviceIndex = paNoDevice;

	for (auto i = 0; i < numDevices && (inputDeviceIndex == paNoDevice || outputDeviceIndex == paNoDevice); ++i)
	{
		const auto deviceInfo = Pa_GetDeviceInfo(i);
		const auto hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
		if (deviceInfo->name == inputDeviceName && hostApiInfo->name == backend) { inputDeviceIndex = i; }
		if (deviceInfo->name == outputDeviceName && hostApiInfo->name == backend) { outputDeviceIndex = i; }
	}

	if (inputDeviceIndex != paNoDevice && inputDeviceChannels < 1) { inputDeviceChannels = DEFAULT_CHANNELS; }

	const auto sampleRate = engine->baseSampleRate();
	const auto framesPerBuffer = engine->framesPerPeriod();
	const auto inputLatency
		= inputDeviceIndex == paNoDevice ? 0. : Pa_GetDeviceInfo(inputDeviceIndex)->defaultLowInputLatency;
	const auto outputLatency
		= outputDeviceIndex == paNoDevice ? 0. : Pa_GetDeviceInfo(outputDeviceIndex)->defaultLowOutputLatency;

	auto inputParameters = PaStreamParameters {
		.device = inputDeviceIndex,
		.channelCount = inputDeviceChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = inputLatency,
		.hostApiSpecificStreamInfo = nullptr
	};

	auto outputParameters = PaStreamParameters {
		.device = outputDeviceIndex,
		.channelCount = outputDeviceChannels,
		.sampleFormat = paFloat32,
		.suggestedLatency = outputLatency,
		.hostApiSpecificStreamInfo = nullptr
	};

#ifdef LMMS_BUILD_WIN32
	// WASAPI tuning: run the stream thread at MMCSS "Pro Audio" priority.
	// Exclusive mode bypasses the Windows mixer as the low-latency
	// alternative to ASIO; shared mode gets automatic sample rate conversion
	// so the stream still opens when the project rate differs from the
	// device's mix format (e.g. 44.1 kHz project on a 48 kHz device).
	auto wasapiInfo = PaWasapiStreamInfo{};
	const bool wasapiUsed = backend == wasapiBackendName();
	const bool wasapiExclusive = wasapiUsed
			&& ConfigManager::inst()->value(tag(), wasapiExclusiveAttribute()).toInt() != 0;
	if (wasapiUsed)
	{
		wasapiInfo.size = sizeof(PaWasapiStreamInfo);
		wasapiInfo.hostApiType = paWASAPI;
		wasapiInfo.version = 1;
		wasapiInfo.flags = paWinWasapiThreadPriority
				| (wasapiExclusive ? paWinWasapiExclusive : paWinWasapiAutoConvert);
		wasapiInfo.threadPriority = eThreadPriorityProAudio;
	}
#else
	const bool wasapiUsed = false;
	const bool wasapiExclusive = false;
#endif

	// The main stream is output-only: recording is handled separately by
	// AudioRecorder, which opens its own capture-only streams on demand. Opening
	// a duplex stream here (input + output on the same device, e.g. a shared
	// audio interface in WASAPI exclusive mode) made playback stutter/noisy even
	// for a single note, on every PortAudio backend, so the capture side must
	// never be part of the playback stream.
	//
	// Try the requested configuration first, then back off without the WASAPI
	// tuning if the device rejects it.
	struct Attempt { bool withInput; bool withWasapiInfo; };
	auto attempts = std::vector<Attempt>{};
	const bool hasInput = false;
	attempts.push_back({hasInput, wasapiUsed});
	if (wasapiUsed) { attempts.push_back({hasInput, false}); }
	if (hasInput)
	{
		attempts.push_back({false, wasapiUsed});
		if (wasapiUsed) { attempts.push_back({false, false}); }
	}

	PaError err = paNoError;
	bool opened = false;
	bool inputOpened = false;
	for (const auto& attempt : attempts)
	{
#ifdef LMMS_BUILD_WIN32
		auto info = attempt.withWasapiInfo ? &wasapiInfo : nullptr;
#else
		void* info = nullptr;
#endif
		inputParameters.hostApiSpecificStreamInfo = info;
		outputParameters.hostApiSpecificStreamInfo = info;
		const auto inputParametersPtr = attempt.withInput ? &inputParameters : nullptr;
		const auto outputParametersPtr = outputDeviceIndex == paNoDevice ? nullptr : &outputParameters;

		err = Pa_IsFormatSupported(inputParametersPtr, outputParametersPtr, sampleRate);
		if (err != paFormatIsSupported)
		{
			std::cerr << "AudioPortAudio: format not supported (input: " << attempt.withInput
					<< ", tuned: " << attempt.withWasapiInfo << "): " << Pa_GetErrorText(err) << '\n';
			continue;
		}

		// In WASAPI exclusive mode the host buffer must line up with the
		// device's own period. Forcing our configured size (e.g. 256) gave
		// PortAudio a mismatched buffer that it serviced with audible noise, so
		// let it negotiate the device-aligned period there. Shared mode and the
		// other backends keep the configured size.
		const auto requestedFrames = (attempt.withWasapiInfo && wasapiExclusive)
				? paFramesPerBufferUnspecified : framesPerBuffer;
		err = Pa_OpenStream(&m_paStream, inputParametersPtr, outputParametersPtr, sampleRate, requestedFrames,
			paNoFlag, &AudioPortAudio::processCallback, this);
		if (err == paNoError)
		{
			opened = true;
			inputOpened = attempt.withInput;
			break;
		}
		std::cerr << "AudioPortAudio: could not open stream (input: " << attempt.withInput
				<< ", tuned: " << attempt.withWasapiInfo << "): " << Pa_GetErrorText(err) << '\n';
	}

	if (!opened)
	{
		std::cerr << "AudioPortAudio: no usable stream configuration found\n";
		successful = false;
		return;
	}

	successful = true;
	setSampleRate(sampleRate);
	setChannels(outputDeviceChannels);
	m_inputChannels = inputOpened ? inputDeviceChannels : 0;
	// the record buttons stay available even without a configured input
	// device - AudioRecorder then falls back to the system default input
	m_supportsCapture = true;
}

AudioPortAudio::~AudioPortAudio()
{
	stopProcessing();
	Pa_CloseStream(m_paStream);
}

void AudioPortAudio::startProcessingImpl()
{
	Pa_StartStream(m_paStream);
}

void AudioPortAudio::stopProcessingImpl()
{
	Pa_StopStream(m_paStream);
}

int AudioPortAudio::processCallback(const void* input, void* output, unsigned long frameCount,
	const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* userData)
{
	const auto device = static_cast<AudioPortAudio*>(userData);
	const auto channels = device->channels();
	const auto outputBuffer = reinterpret_cast<float*>(output);

	if (!device->isRunning())
	{
		std::fill_n(outputBuffer, frameCount * channels, 0.f);
		return paComplete;
	}

	device->audioEngine()->renderNextBuffer({outputBuffer, channels, frameCount});
	return paContinue;
}
} // namespace lmms

namespace lmms::gui {

class AudioPortAudioSetupWidget::DeviceSelectorWidget : public QGroupBox
{
public:
	DeviceSelectorWidget(const QString& deviceLabel, Direction direction, QWidget* parent = nullptr)
		: QGroupBox{parent}
		, m_deviceComboBox{new QComboBox{this}}
		, m_channelSpinBox{new LcdSpinBox{1, this}}
		, m_direction(direction)
	{
		m_channelSpinBox->setModel(&m_channelModel);

		const auto layout = new QFormLayout{this};
		layout->addRow(deviceLabel, m_deviceComboBox);
		layout->addRow(AudioDeviceSetupWidget::tr("Channels"), m_channelSpinBox);

		connect(m_deviceComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
			[this](int index) { refreshChannels(m_deviceComboBox->itemData(index).toInt()); });
	}

	void refreshFromConfig(PaHostApiIndex backendIndex)
	{
		using namespace lmms;

		m_deviceComboBox->clear();

		for (auto i = 0, deviceCount = Pa_GetDeviceCount(); i < deviceCount; ++i)
		{
			const auto deviceInfo = Pa_GetDeviceInfo(i);

			if (maxChannels(deviceInfo, m_direction) > 0 && deviceInfo->hostApi == backendIndex)
			{
				m_deviceComboBox->addItem(deviceInfo->name, i);
			}
		}

		const auto selectedDeviceName = ConfigManager::inst()->value(tag(), deviceNameAttribute(m_direction));
		const auto selectedDeviceIndex = std::max(0, m_deviceComboBox->findText(selectedDeviceName));
		m_deviceComboBox->setCurrentIndex(selectedDeviceIndex);
	}

	void refreshChannels(PaDeviceIndex deviceIndex)
	{
		const auto maxChannelCount = maxChannels(Pa_GetDeviceInfo(deviceIndex), m_direction);
		const auto channelCount = ConfigManager::inst()->value(tag(), channelsAttribute(m_direction)).toInt();
		m_channelModel.setRange(1, maxChannelCount);
		m_channelModel.setValue(channelCount == 0 ? DEFAULT_CHANNELS : channelCount);
		m_channelSpinBox->setNumDigits(QString::number(maxChannelCount).length());
	}

	void saveToConfig()
	{
		ConfigManager::inst()->setValue(tag(), deviceNameAttribute(m_direction), m_deviceComboBox->currentText());
		ConfigManager::inst()->setValue(tag(), channelsAttribute(m_direction), QString::number(m_channelModel.value()));
	}

private:
	QComboBox* m_deviceComboBox = nullptr;
	LcdSpinBox* m_channelSpinBox = nullptr;
	IntModel m_channelModel;
	Direction m_direction;
};

AudioPortAudioSetupWidget::AudioPortAudioSetupWidget(QWidget* parent)
	: AudioDeviceSetupWidget{AudioPortAudio::name(), parent}
	, m_backendComboBox{new QComboBox{this}}
	, m_wasapiExclusiveCheckBox{new QCheckBox{tr("Exclusive mode (lower latency)"), this}}
	, m_inputDevice{new DeviceSelectorWidget{tr("Input device"), Direction::Input}}
	, m_outputDevice(new DeviceSelectorWidget{tr("Output device"), Direction::Output})
{
	constexpr auto formVerticalSpacing = 10;
	const auto form = new QFormLayout{this};
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setVerticalSpacing(formVerticalSpacing);

	form->addRow(tr("Backend"), m_backendComboBox);
	form->addRow(QString(), m_wasapiExclusiveCheckBox);
	form->addRow(m_outputDevice);
	form->addRow(m_inputDevice);

	m_wasapiExclusiveCheckBox->setChecked(
		ConfigManager::inst()->value(tag(), wasapiExclusiveAttribute()).toInt() != 0);

	connect(m_backendComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		m_inputDevice->refreshFromConfig(m_backendComboBox->itemData(index).toInt());
		m_outputDevice->refreshFromConfig(m_backendComboBox->itemData(index).toInt());
		updateExclusiveCheckBox();
	});

}




void AudioPortAudioSetupWidget::updateExclusiveCheckBox()
{
	// the option only has an effect for the WASAPI backend
	m_wasapiExclusiveCheckBox->setVisible(m_backendComboBox->currentText() == wasapiBackendName());
}

void AudioPortAudioSetupWidget::show()
{
	static auto s_initGuard = detail::PortAudioInitializationGuard{};

	if (m_backendComboBox->count() == 0)
	{
		for (auto i = 0, backendCount = Pa_GetHostApiCount(); i < backendCount; ++i)
		{
			m_backendComboBox->addItem(Pa_GetHostApiInfo(i)->name, i);
		}

		const auto selectedBackendName = ConfigManager::inst()->value(tag(), backendAttribute());
		const auto selectedBackendIndex = std::max(0, m_backendComboBox->findText(selectedBackendName));
		m_backendComboBox->setCurrentIndex(selectedBackendIndex);
	}

	updateExclusiveCheckBox();
	AudioDeviceSetupWidget::show();
}

void AudioPortAudioSetupWidget::saveSettings()
{
	ConfigManager::inst()->setValue(tag(), backendAttribute(), m_backendComboBox->currentText());
	ConfigManager::inst()->setValue(tag(), wasapiExclusiveAttribute(),
		m_wasapiExclusiveCheckBox->isChecked() ? "1" : "0");
	m_inputDevice->saveToConfig();
	m_outputDevice->saveToConfig();
}
} // namespace lmms::gui

#endif // LMMS_HAVE_PORTAUDIO
