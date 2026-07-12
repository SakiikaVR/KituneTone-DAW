/*
 * Vst3Plugin.cpp - in-process VST3 hosting for LMMS
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

#include "Vst3Plugin.h"

#include "lmmsconfig.h"

#include <QCloseEvent>
#include <QDomDocument>
#include <QDomElement>
#include <QHBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#ifdef LMMS_BUILD_WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstring>

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#ifdef LMMS_HAVE_ARA
#include "ARA_API/ARAVST3.h"
#include "Vst3AraHost.h"
// provide the definitions for the ARA VST3 interface IIDs we reference
namespace ARA
{
DEF_CLASS_IID(IMainFactory)
DEF_CLASS_IID(IPlugInEntryPoint)
DEF_CLASS_IID(IPlugInEntryPoint2)
}
#endif
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/base/funknownimpl.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "SampleFrame.h"
#include "Song.h"

namespace lmms
{

namespace
{

using namespace Steinberg;

//! Shared host context for all plug-in instances
Vst::HostApplication* pluginContext()
{
	static Vst::HostApplication* ctx = []
	{
		auto c = new Vst::HostApplication();
		Vst::PluginContextFactory::instance().setPluginContext(c);
		return c;
	}();
	return ctx;
}

bool isInstrumentClass(const VST3::Hosting::ClassInfo& info)
{
	const auto& subs = info.subCategories();
	return std::any_of(subs.begin(), subs.end(), [](const std::string& s)
			{ return s.find("Instrument") != std::string::npos; });
}

} // anonymous namespace


//! Top-level window hosting the plug-in's native editor view, with a small
//! toolbar (always-on-top toggle) above the plug-in area
class Vst3EditorWidget : public QWidget
{
public:
	Vst3EditorWidget(Vst3Plugin* plugin, Steinberg::IPlugView* view) :
		QWidget(nullptr, Qt::Window),
		m_plugin(plugin),
		m_view(view)
	{
		setWindowTitle(plugin->name());

		auto layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		auto bar = new QWidget(this);
		auto barLayout = new QHBoxLayout(bar);
		barLayout->setContentsMargins(4, 2, 4, 2);

		auto pinButton = new QToolButton(bar);
		pinButton->setCheckable(true);
		pinButton->setText(QStringLiteral("\U0001F4CC")); // pushpin
		pinButton->setToolTip(Vst3Plugin::tr("Always on top"));
		pinButton->setAutoRaise(true);
		QObject::connect(pinButton, &QToolButton::toggled,
				[this](bool on) { setAlwaysOnTop(on); });
		barLayout->addWidget(pinButton);
		barLayout->addStretch();
		layout->addWidget(bar);

		m_container = new QWidget(this);
		m_container->setAttribute(Qt::WA_NativeWindow);
		layout->addWidget(m_container, 1);
	}

	//! native window the plug-in view gets attached to
	QWidget* container() const { return m_container; }

	//! resize the window so the plug-in area has the given size
	void setPluginSize(int w, int h, bool fixed)
	{
		m_container->setFixedSize(w, h);
		adjustSize();
		if (fixed)
		{
			setFixedSize(size());
		}
		else
		{
			m_container->setMinimumSize(64, 64);
			m_container->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
			setMinimumSize(160, 90);
			setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		}
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		// keep the editor alive, just hide it
		event->ignore();
		hide();
		emit m_plugin->editorClosed();
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		if (m_view && m_view->canResize() == Steinberg::kResultTrue)
		{
			Steinberg::ViewRect rect(0, 0, m_container->width(), m_container->height());
			m_view->onSize(&rect);
		}
	}

private:
	void setAlwaysOnTop(bool on)
	{
#ifdef LMMS_BUILD_WIN32
		// use the Win32 API directly - toggling Qt window flags would
		// recreate the native window the plug-in view is attached to
		SetWindowPos(reinterpret_cast<HWND>(winId()),
				on ? HWND_TOPMOST : HWND_NOTOPMOST,
				0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
		setWindowFlag(Qt::WindowStaysOnTopHint, on);
		show();
#endif
	}

	Vst3Plugin* m_plugin;
	Steinberg::IPlugView* m_view;
	QWidget* m_container = nullptr;
};




Vst3Plugin::Vst3Plugin(const QString& path, Kind kind, const QString& uid) :
	m_kind(kind)
{
	m_failed = !load(path, uid);

	m_outputSyncTimer = new QTimer(this);
	m_outputSyncTimer->setInterval(50);
	connect(m_outputSyncTimer, &QTimer::timeout, this, [this] { syncOutputParameters(); });
	if (!m_failed) { m_outputSyncTimer->start(); }
}




Vst3Plugin::~Vst3Plugin()
{
	unload();
}




bool Vst3Plugin::load(const QString& path, const QString& uid)
{
	using namespace Steinberg;

	std::string error;
	m_module = VST3::Hosting::Module::create(path.toStdString(), error);
	if (!m_module)
	{
		m_error = QString::fromStdString(error);
		return false;
	}
	m_path = path;

	const auto& factory = m_module->getFactory();
	factory.setHostContext(static_cast<FUnknown*>(pluginContext()));

	// pick the class to instantiate
	VST3::Hosting::ClassInfo selected;
	bool found = false;
	bool foundPreferred = false;
	for (const auto& classInfo : factory.classInfos())
	{
		if (classInfo.category() != kVstAudioEffectClass) { continue; }
		if (!uid.isEmpty())
		{
			if (QString::fromStdString(classInfo.ID().toString()) == uid)
			{
				selected = classInfo;
				found = true;
				break;
			}
			continue;
		}
		const bool preferred = (m_kind == Kind::Instrument)
				? isInstrumentClass(classInfo)
				: !isInstrumentClass(classInfo);
		if (!found || (preferred && !foundPreferred))
		{
			selected = classInfo;
			found = true;
			foundPreferred = preferred;
			if (preferred) { break; }
		}
	}
	if (!found)
	{
		m_error = tr("No suitable VST3 audio class found in %1").arg(path);
		return false;
	}

	m_provider = owned(new Vst::PlugProvider(factory, selected, true));
	if (!m_provider->initialize())
	{
		m_provider = nullptr;
		m_error = tr("Failed to initialize VST3 plugin %1").arg(path);
		return false;
	}

	m_component = m_provider->getComponentPtr();
	m_controller = m_provider->getControllerPtr();
	m_processor = U::cast<Vst::IAudioProcessor>(m_component);
	if (!m_component || !m_processor)
	{
		m_error = tr("VST3 plugin %1 has no audio processor").arg(path);
		return false;
	}

	m_name = QString::fromStdString(selected.name());
	m_vendor = QString::fromStdString(selected.vendor());
	m_uid = QString::fromStdString(selected.ID().toString());

	if (m_controller)
	{
		m_controller->setComponentHandler(this);
		// sync initial component state to the controller
		MemoryStream stream;
		if (m_component->getState(&stream) == kResultTrue)
		{
			stream.seek(0, IBStream::kIBSeekSet, nullptr);
			m_controller->setComponentState(&stream);
		}
		m_midiMapping = U::cast<Vst::IMidiMapping>(m_controller);
	}

	// activate main audio buses, deactivate the others
	for (int32 dir = Vst::kInput; dir <= Vst::kOutput; ++dir)
	{
		const int32 busCount = m_component->getBusCount(Vst::kAudio, dir);
		for (int32 i = 0; i < busCount; ++i)
		{
			m_component->activateBus(Vst::kAudio, dir, i, i == 0);
		}
		if (busCount > 0)
		{
			(dir == Vst::kInput ? m_hasInputBus : m_hasOutputBus) = true;
		}
	}
	// activate event buses (MIDI input)
	for (int32 i = 0; i < m_component->getBusCount(Vst::kEvent, Vst::kInput); ++i)
	{
		m_component->activateBus(Vst::kEvent, Vst::kInput, i, i == 0);
	}

	if (!m_hasOutputBus)
	{
		m_error = tr("VST3 plugin %1 has no audio output").arg(path);
		return false;
	}

	// try to get stereo main buses
	Vst::SpeakerArrangement stereo = Vst::SpeakerArr::kStereo;
	m_processor->setBusArrangements(m_hasInputBus ? &stereo : nullptr, m_hasInputBus ? 1 : 0,
			&stereo, 1);

	const auto sampleRate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
	if (!setupProcessing(sampleRate, MAXIMUM_BUFFER_SIZE))
	{
		m_error = tr("Failed to set up processing for VST3 plugin %1").arg(path);
		return false;
	}

#ifdef LMMS_HAVE_ARA
	// experimental: detect ARA support (e.g. Melodyne). Full ARA hosting
	// (audio source analysis, playback regions) is not implemented yet - we
	// only surface whether the plug-in provides an ARA factory.
	for (const auto& classInfo : factory.classInfos())
	{
		if (classInfo.category() != kARAMainFactoryClass) { continue; }
		if (auto araMainFactory =
				factory.createInstance<ARA::IMainFactory>(classInfo.ID()))
		{
			if (const ARA::ARAFactory* araFactory = araMainFactory->getFactory())
			{
				m_hasAra = true;
				m_araName = QString::fromUtf8(araFactory->plugInName);
				m_araFactory = araFactory; // owned by the module, valid while loaded
			}
		}
		break;
	}
#endif

	return true;
}




bool Vst3Plugin::setupProcessing(double sampleRate, Steinberg::int32 maxBlockSize)
{
	using namespace Steinberg;

	if (m_processingActive)
	{
		m_processor->setProcessing(false);
		m_component->setActive(false);
		m_processingActive = false;
	}

	Vst::ProcessSetup setup;
	setup.processMode = Vst::kRealtime;
	setup.symbolicSampleSize = Vst::kSample32;
	setup.maxSamplesPerBlock = maxBlockSize;
	setup.sampleRate = sampleRate;
	if (m_processor->setupProcessing(setup) != kResultOk)
	{
		// some plugins are picky but still work with defaults; treat as fatal only
		// if activation fails below
	}

	m_processData.unprepare();
	m_processData.prepare(*m_component, maxBlockSize, Vst::kSample32);
	m_processData.inputEvents = &m_inputEvents;
	m_processData.outputEvents = &m_outputEvents;
	m_processData.inputParameterChanges = &m_inputParamChanges;
	m_processData.outputParameterChanges = &m_outputParamChanges;
	m_processData.processContext = &m_processContext;

	if (m_component->setActive(true) != kResultOk) { return false; }
	m_processor->setProcessing(true);

	m_sampleRate = sampleRate;
	m_maxBlockSize = maxBlockSize;
	m_processingActive = true;
	return true;
}




void Vst3Plugin::unload()
{
	destroyEditor();

	if (m_processingActive)
	{
		m_processor->setProcessing(false);
		m_component->setActive(false);
		m_processingActive = false;
	}
	m_processData.unprepare();

	m_midiMapping = nullptr;
	m_processor = nullptr;
	m_controller = nullptr;
	m_component = nullptr;
	if (m_provider)
	{
		m_provider->releasePlugIn(nullptr, nullptr);
		m_provider = nullptr;
	}
	m_module = nullptr;
}




void Vst3Plugin::process(const SampleFrame* in, SampleFrame* out, f_cnt_t frames)
{
	using namespace Steinberg;

	if (m_failed || !m_processingActive || frames == 0)
	{
		if (in != out && out != nullptr)
		{
			if (in != nullptr) { std::memcpy(out, in, sizeof(SampleFrame) * frames); }
			else { zeroSampleFrames(out, frames); }
		}
		return;
	}

	const auto engineRate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
	if (engineRate != m_sampleRate)
	{
		setupProcessing(engineRate, m_maxBlockSize);
	}

	const auto numSamples = static_cast<int32>(std::min<f_cnt_t>(frames,
			static_cast<f_cnt_t>(m_maxBlockSize)));
	m_processData.numSamples = numSamples;

	// fill input bus
	if (m_hasInputBus && m_processData.numInputs > 0)
	{
		auto& bus = m_processData.inputs[0];
		for (int32 ch = 0; ch < bus.numChannels; ++ch)
		{
			float* dst = bus.channelBuffers32[ch];
			if (dst == nullptr) { continue; }
			if (in != nullptr)
			{
				const int srcCh = ch < 2 ? ch : ch % 2;
				for (int32 f = 0; f < numSamples; ++f) { dst[f] = in[f][srcCh]; }
			}
			else
			{
				std::memset(dst, 0, sizeof(float) * numSamples);
			}
		}
		bus.silenceFlags = (in == nullptr) ? Vst::HostProcessData::kAllChannelsSilent : 0;
	}

	// transfer queued events and parameter changes
	{
		QMutexLocker lock(&m_queueMutex);
		for (auto& event : m_pendingEvents)
		{
			if (event.sampleOffset >= numSamples) { event.sampleOffset = numSamples - 1; }
			m_inputEvents.addEvent(event);
		}
		m_pendingEvents.clear();

		for (const auto& [id, value] : m_pendingParams)
		{
			int32 queueIndex = 0;
			if (auto* queue = m_inputParamChanges.addParameterData(id, queueIndex))
			{
				int32 pointIndex = 0;
				queue->addPoint(0, value, pointIndex);
			}
		}
		m_pendingParams.clear();
	}

	// process context
	auto* song = Engine::getSong();
	m_processContext = {};
	m_processContext.sampleRate = m_sampleRate;
	// ARA playback rendering needs the process time to follow the real song
	// timeline so the playback region maps to the correct position; otherwise
	// a free-running counter is fine.
	if (araActive() && song != nullptr)
	{
		// follow the real song timeline (in output-rate frames) so the ARA
		// playback region maps to the correct position
		m_processContext.projectTimeSamples = static_cast<Steinberg::int64>(song->getFrames());
	}
	else
	{
		m_processContext.projectTimeSamples = m_projectTimeSamples;
	}
	m_processContext.state |= Vst::ProcessContext::kTempoValid | Vst::ProcessContext::kTimeSigValid;
	if (song)
	{
		m_processContext.tempo = song->getTempo();
		m_processContext.timeSigNumerator = song->getTimeSigModel().getNumerator();
		m_processContext.timeSigDenominator = song->getTimeSigModel().getDenominator();
		if (song->isPlaying()) { m_processContext.state |= Vst::ProcessContext::kPlaying; }

		// musical position in quarter notes (needed for the plug-in's beat-based
		// ruler / playhead, e.g. Vovious "Use DAW Tempo / Beat")
		const double seconds = static_cast<double>(m_processContext.projectTimeSamples) / m_sampleRate;
		m_processContext.projectTimeMusic = seconds * m_processContext.tempo / 60.0;
		m_processContext.state |= Vst::ProcessContext::kProjectTimeMusicValid;
	}
	else
	{
		m_processContext.tempo = 120.;
		m_processContext.timeSigNumerator = 4;
		m_processContext.timeSigDenominator = 4;
	}

	m_processor->process(m_processData);

	// fetch output
	if (m_processData.numOutputs > 0)
	{
		auto& bus = m_processData.outputs[0];
		if (bus.numChannels >= 2)
		{
			const float* left = bus.channelBuffers32[0];
			const float* right = bus.channelBuffers32[1];
			for (int32 f = 0; f < numSamples; ++f)
			{
				out[f][0] = left ? left[f] : 0.f;
				out[f][1] = right ? right[f] : 0.f;
			}
		}
		else if (bus.numChannels == 1)
		{
			const float* mono = bus.channelBuffers32[0];
			for (int32 f = 0; f < numSamples; ++f)
			{
				const float v = mono ? mono[f] : 0.f;
				out[f][0] = v;
				out[f][1] = v;
			}
		}
	}

	// collect parameter updates for the controller (GUI) side
	const int32 outParamCount = m_outputParamChanges.getParameterCount();
	if (outParamCount > 0)
	{
		QMutexLocker lock(&m_outputParamMutex);
		for (int32 i = 0; i < outParamCount; ++i)
		{
			auto* queue = m_outputParamChanges.getParameterData(i);
			if (queue == nullptr || queue->getPointCount() == 0) { continue; }
			int32 sampleOffset = 0;
			Vst::ParamValue value = 0.;
			if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) == kResultTrue)
			{
				m_outputParams[queue->getParameterId()] = value;
			}
		}
	}

	m_inputEvents.clear();
	m_outputEvents.clear();
	m_inputParamChanges.clearQueue();
	m_outputParamChanges.clearQueue();

	m_projectTimeSamples += numSamples;
}




void Vst3Plugin::queueMidiEvent(const MidiEvent& event, f_cnt_t offset)
{
	using namespace Steinberg;

	if (m_failed) { return; }

	const auto sampleOffset = static_cast<int32>(offset);
	const auto channel = static_cast<int16>(event.channel());

	switch (event.type())
	{
		case MidiNoteOn:
		{
			Vst::Event e = {};
			e.busIndex = 0;
			e.sampleOffset = sampleOffset;
			e.type = Vst::Event::kNoteOnEvent;
			e.noteOn.channel = channel;
			e.noteOn.pitch = static_cast<int16>(event.key());
			e.noteOn.velocity = event.velocity() / 127.f;
			e.noteOn.noteId = -1;
			QMutexLocker lock(&m_queueMutex);
			m_pendingEvents.push_back(e);
			break;
		}
		case MidiNoteOff:
		{
			Vst::Event e = {};
			e.busIndex = 0;
			e.sampleOffset = sampleOffset;
			e.type = Vst::Event::kNoteOffEvent;
			e.noteOff.channel = channel;
			e.noteOff.pitch = static_cast<int16>(event.key());
			e.noteOff.velocity = event.velocity() / 127.f;
			e.noteOff.noteId = -1;
			QMutexLocker lock(&m_queueMutex);
			m_pendingEvents.push_back(e);
			break;
		}
		case MidiKeyPressure:
		{
			Vst::Event e = {};
			e.busIndex = 0;
			e.sampleOffset = sampleOffset;
			e.type = Vst::Event::kPolyPressureEvent;
			e.polyPressure.channel = channel;
			e.polyPressure.pitch = static_cast<int16>(event.key());
			e.polyPressure.pressure = event.velocity() / 127.f;
			e.polyPressure.noteId = -1;
			QMutexLocker lock(&m_queueMutex);
			m_pendingEvents.push_back(e);
			break;
		}
		case MidiControlChange:
		case MidiPitchBend:
		case MidiChannelPressure:
		{
			if (!m_midiMapping) { break; }
			Vst::CtrlNumber ctrl;
			double value;
			if (event.type() == MidiControlChange)
			{
				ctrl = event.controllerNumber();
				value = event.controllerValue() / 127.;
			}
			else if (event.type() == MidiPitchBend)
			{
				ctrl = Vst::kPitchBend;
				value = static_cast<uint16_t>(event.pitchBend()) / 16383.;
			}
			else
			{
				ctrl = Vst::kAfterTouch;
				value = event.channelPressure() / 127.;
			}
			Vst::ParamID paramId = Vst::kNoParamId;
			if (m_midiMapping->getMidiControllerAssignment(0, channel, ctrl, paramId) == kResultTrue
					&& paramId != Vst::kNoParamId)
			{
				QMutexLocker lock(&m_queueMutex);
				m_pendingParams[paramId] = value;
			}
			break;
		}
		default:
			break;
	}
}




void Vst3Plugin::saveState(QDomDocument& doc, QDomElement& element)
{
	using namespace Steinberg;
	Q_UNUSED(doc)

	if (m_failed || !m_component) { return; }

	element.setAttribute("uid", m_uid);

	MemoryStream componentState;
	if (m_component->getState(&componentState) == kResultTrue)
	{
		const auto data = QByteArray(componentState.getData(),
				static_cast<int>(componentState.getSize()));
		element.setAttribute("chunk", QString(data.toBase64()));
	}
	if (m_controller)
	{
		MemoryStream controllerState;
		if (m_controller->getState(&controllerState) == kResultTrue)
		{
			const auto data = QByteArray(controllerState.getData(),
					static_cast<int>(controllerState.getSize()));
			element.setAttribute("controllerchunk", QString(data.toBase64()));
		}
	}
}




void Vst3Plugin::loadState(const QDomElement& element)
{
	using namespace Steinberg;

	if (m_failed || !m_component) { return; }

	const auto chunk = QByteArray::fromBase64(element.attribute("chunk").toUtf8());
	if (!chunk.isEmpty())
	{
		MemoryStream stream;
		int32 written = 0;
		stream.write(const_cast<char*>(chunk.constData()), chunk.size(), &written);
		stream.seek(0, IBStream::kIBSeekSet, nullptr);
		m_component->setState(&stream);

		if (m_controller)
		{
			stream.seek(0, IBStream::kIBSeekSet, nullptr);
			m_controller->setComponentState(&stream);
		}
	}

	const auto ctrlChunk = QByteArray::fromBase64(element.attribute("controllerchunk").toUtf8());
	if (!ctrlChunk.isEmpty() && m_controller)
	{
		MemoryStream stream;
		int32 written = 0;
		stream.write(const_cast<char*>(ctrlChunk.constData()), ctrlChunk.size(), &written);
		stream.seek(0, IBStream::kIBSeekSet, nullptr);
		m_controller->setState(&stream);
	}
}




bool Vst3Plugin::hasEditor()
{
	if (m_failed || !m_controller) { return false; }
	if (m_view) { return true; }
	if (m_editorCreationTried) { return false; }
	// probe without keeping the view
	auto* view = m_controller->createView(Steinberg::Vst::ViewType::kEditor);
	if (view == nullptr)
	{
		m_editorCreationTried = true;
		return false;
	}
	view->release();
	return true;
}




void Vst3Plugin::showEditor()
{
	using namespace Steinberg;

	if (m_failed || !m_controller) { return; }

	if (m_editorWidget != nullptr)
	{
		m_editorWidget->show();
		m_editorWidget->raise();
		return;
	}

	m_view = owned(m_controller->createView(Vst::ViewType::kEditor));
	m_editorCreationTried = true;
	if (!m_view) { return; }

	auto widget = new Vst3EditorWidget(this, m_view.get());

	ViewRect size = {};
	int w = 640, h = 480;
	if (m_view->getSize(&size) == kResultTrue)
	{
		w = size.getWidth();
		h = size.getHeight();
	}
	widget->setPluginSize(w, h, m_view->canResize() != kResultTrue);

	m_view->setFrame(this);
	if (m_view->attached(reinterpret_cast<void*>(widget->container()->winId()),
			kPlatformTypeHWND) != kResultTrue)
	{
		m_view->setFrame(nullptr);
		m_view = nullptr;
		delete widget;
		return;
	}

	m_editorWidget = widget;
	m_editorWidget->show();
}




void Vst3Plugin::hideEditor()
{
	if (m_editorWidget != nullptr) { m_editorWidget->hide(); }
}




void Vst3Plugin::toggleEditor()
{
	if (editorVisible()) { hideEditor(); }
	else { showEditor(); }
}




bool Vst3Plugin::editorVisible() const
{
	return m_editorWidget != nullptr && m_editorWidget->isVisible();
}




void Vst3Plugin::destroyEditor()
{
	if (m_view)
	{
		m_view->removed();
		m_view->setFrame(nullptr);
		m_view = nullptr;
	}
	if (m_editorWidget != nullptr)
	{
		delete m_editorWidget;
		m_editorWidget = nullptr;
	}
}




void Vst3Plugin::syncOutputParameters()
{
#ifdef LMMS_HAVE_ARA
	// let the ARA plug-in process deferred model updates (analysis etc.)
	if (m_araDocument)
	{
		m_araDocument->notifyModelUpdates();
		// log audio-read progress roughly every 2s (40 * 50ms timer)
		static int s_tick = 0;
		if (++s_tick >= 40)
		{
			s_tick = 0;
			araDebugLog(QString("audio reads so far: %1").arg(araAudioReadCount()));
		}
	}
#endif

	std::map<Steinberg::Vst::ParamID, double> params;
	{
		QMutexLocker lock(&m_outputParamMutex);
		params.swap(m_outputParams);
	}
	if (!m_controller) { return; }
	for (const auto& [id, value] : params)
	{
		m_controller->setParamNormalized(id, value);
	}
}




Steinberg::tresult PLUGIN_API Vst3Plugin::beginEdit(Steinberg::Vst::ParamID)
{
	return Steinberg::kResultOk;
}




Steinberg::tresult PLUGIN_API Vst3Plugin::performEdit(Steinberg::Vst::ParamID id,
		Steinberg::Vst::ParamValue valueNormalized)
{
	QMutexLocker lock(&m_queueMutex);
	m_pendingParams[id] = valueNormalized;
	return Steinberg::kResultOk;
}




Steinberg::tresult PLUGIN_API Vst3Plugin::endEdit(Steinberg::Vst::ParamID)
{
	return Steinberg::kResultOk;
}




Steinberg::tresult PLUGIN_API Vst3Plugin::restartComponent(Steinberg::int32 flags)
{
	using namespace Steinberg;

	if ((flags & Vst::kParamValuesChanged) && m_controller)
	{
		// push all current controller values to the processor
		const int32 count = m_controller->getParameterCount();
		QMutexLocker lock(&m_queueMutex);
		for (int32 i = 0; i < count; ++i)
		{
			Vst::ParameterInfo info;
			if (m_controller->getParameterInfo(i, info) != kResultTrue) { continue; }
			if (info.flags & Vst::ParameterInfo::kIsReadOnly) { continue; }
			m_pendingParams[info.id] = m_controller->getParamNormalized(info.id);
		}
	}
	return kResultOk;
}




Steinberg::tresult PLUGIN_API Vst3Plugin::resizeView(Steinberg::IPlugView* view,
		Steinberg::ViewRect* newSize)
{
	using namespace Steinberg;

	if (view != m_view.get() || newSize == nullptr || m_editorWidget == nullptr)
	{
		return kInvalidArgument;
	}
	static_cast<Vst3EditorWidget*>(m_editorWidget)->setPluginSize(
			newSize->getWidth(), newSize->getHeight(),
			m_view->canResize() != kResultTrue);
	view->onSize(newSize);
	return kResultTrue;
}




Steinberg::tresult PLUGIN_API Vst3Plugin::queryInterface(const Steinberg::TUID _iid, void** obj)
{
	QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Steinberg::Vst::IComponentHandler)
	QUERY_INTERFACE(_iid, obj, Steinberg::Vst::IComponentHandler::iid, Steinberg::Vst::IComponentHandler)
	QUERY_INTERFACE(_iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
	*obj = nullptr;
	return Steinberg::kNoInterface;
}




std::vector<std::string> Vst3Plugin::modulePaths()
{
	return VST3::Hosting::Module::getModulePaths();
}




bool Vst3Plugin::enableAra(const std::vector<AraSource>& sources)
{
#ifdef LMMS_HAVE_ARA
	if (!m_hasAra || m_araFactory == nullptr || !m_component || sources.empty()) { return false; }

	// the audio processor component also implements the ARA VST3 entry point
	auto entryPoint = U::cast<ARA::IPlugInEntryPoint2>(m_component);
	if (!entryPoint) { return false; }

	// rebuild any previous ARA document
	m_araDocument.reset();

	auto* song = Engine::getSong();
	const double tempo = song ? static_cast<double>(song->getTempo()) : 120.0;
	const int tsNum = song ? song->getTimeSigModel().getNumerator() : 4;
	const int tsDen = song ? song->getTimeSigModel().getDenominator() : 4;

	auto doc = std::make_unique<Vst3AraDocument>();
	if (!doc->setup(static_cast<const ARA::ARAFactory*>(m_araFactory),
			entryPoint.get(), sources, tempo, tsNum, tsDen))
	{
		return false;
	}
	m_araDocument = std::move(doc);
	return true;
#else
	Q_UNUSED(sources)
	return false;
#endif
}




bool Vst3Plugin::araActive() const
{
#ifdef LMMS_HAVE_ARA
	return m_araDocument != nullptr && m_araDocument->isValid();
#else
	return false;
#endif
}


} // namespace lmms
