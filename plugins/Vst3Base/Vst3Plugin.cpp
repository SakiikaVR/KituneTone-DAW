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
#include <QBoxLayout>
#include <QDomDocument>
#include <QDomElement>
#include <QLayout>
#include <QList>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <cmath>
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
#include "AutomatableModel.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "MainWindow.h"
#include "MidiEvent.h"
#include "SampleFrame.h"
#include "Song.h"
#include "SubWindow.h"

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


//! Widget hosting the plug-in's native editor view; lives inside an LMMS
//! workspace subwindow like every other editor window (and can be torn off
//! via the subwindow's detach feature)
class Vst3EditorWidget : public QWidget
{
public:
	Vst3EditorWidget(Vst3Plugin* plugin, Steinberg::IPlugView* view) :
		QWidget(),
		m_plugin(plugin),
		m_view(view)
	{
		setWindowTitle(plugin->name());

		auto layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		m_container = new QWidget(this);
		m_container->setAttribute(Qt::WA_NativeWindow);
		layout->addWidget(m_container, 1);
	}

	//! native window the plug-in view gets attached to
	QWidget* container() const { return m_container; }

	//! the workspace subwindow wrapping this widget (self before it is added)
	QWidget* windowWidget()
	{
		return parentWidget() != nullptr ? parentWidget() : this;
	}

	//! resize widget and subwindow so the plug-in area has the given size.
	//! \a w and \a h are the plug-in's size in physical pixels (as reported by
	//! IPlugView::getSize / resizeView).
	void setPluginSize(int w, int h, bool fixed)
	{
		// VST3 reports sizes in physical pixels, while Qt widget geometry is in
		// logical (device-independent) pixels. On a scaled display (e.g. 150%
		// on a WQHD monitor) Qt makes the native child window devicePixelRatio
		// times larger, so using the raw plug-in size left the window frame
		// that much bigger than the plug-in's drawing. Convert to logical
		// pixels here. On an unscaled display (FullHD at 100%) dpr == 1, so
		// nothing changes.
		const qreal dpr = devicePixelRatioF();
		const int lw = dpr > 0. ? qRound(w / dpr) : w;
		const int lh = dpr > 0. ? qRound(h / dpr) : h;

		// remember the plug-in's preferred size so sizeHint() reports it even
		// for resizable views (whose container has no fixed size afterwards);
		// an embedding container relies on this to size itself correctly
		m_pluginSize = QSize(lw, lh);

		// adopt the exact plug-in size while the container is pinned to it
		m_container->setFixedSize(lw, lh);
		adjustSize();

		if (!fixed)
		{
			// resizable view: free the constraints again *after* the size
			// has been adopted, otherwise the layout collapses
			m_container->setMinimumSize(64, 64);
			m_container->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
			setMinimumSize(160, 90);
			setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		}

		if (auto win = qobject_cast<gui::SubWindow*>(parentWidget()))
		{
			// the contents margins are exactly the window decoration, both
			// attached and detached; use the plug-in size directly instead
			// of a size hint, which is unreliable for resizable views
			const auto m = win->contentsMargins();
			const auto decoration = QSize(m.left() + m.right(), m.top() + m.bottom());
			win->setMinimumSize(0, 0);
			win->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
			win->resize(decoration + QSize(lw, lh));
			if (fixed) { win->setFixedSize(win->size()); }
		}

		// let an embedding container (e.g. a tab in the effect dialog) resize
		// itself to the new plug-in size; harmless for the standalone window
		emit m_plugin->editorResized();
	}

	//! forget the plug-in view (it has been detached/destroyed elsewhere) so
	//! later resize events don't dereference a dangling pointer
	void clearView() { m_view = nullptr; }

	//! report the plug-in's preferred size so layouts / embedding containers
	//! size to the actual editor even when the container is not fixed-size
	QSize sizeHint() const override
	{
		return m_pluginSize.isValid() ? m_pluginSize : QWidget::sizeHint();
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		// keep the editor alive, just hide its window
		event->ignore();
		windowWidget()->hide();
		emit m_plugin->editorClosed();
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		if (m_view && m_view->canResize() == Steinberg::kResultTrue)
		{
			// the plug-in works in physical pixels; convert Qt's logical
			// widget size back up by the device-pixel ratio (see setPluginSize)
			const qreal dpr = devicePixelRatioF();
			Steinberg::ViewRect rect(0, 0,
					qRound(m_container->width() * dpr), qRound(m_container->height() * dpr));
			m_view->onSize(&rect);
		}
	}

private:
	Vst3Plugin* m_plugin;
	Steinberg::IPlugView* m_view;
	QWidget* m_container = nullptr;
	QSize m_pluginSize;   //!< plug-in's preferred size in logical pixels
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
	// activate event buses (MIDI input and output)
	for (int32 i = 0; i < m_component->getBusCount(Vst::kEvent, Vst::kInput); ++i)
	{
		m_component->activateBus(Vst::kEvent, Vst::kInput, i, i == 0);
	}
	for (int32 i = 0; i < m_component->getBusCount(Vst::kEvent, Vst::kOutput); ++i)
	{
		m_component->activateBus(Vst::kEvent, Vst::kOutput, i, i == 0);
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
	if (m_outputSyncTimer) { m_outputSyncTimer->stop(); }
	destroyEditor();

	if (m_processingActive)
	{
		m_processor->setProcessing(false);
		m_component->setActive(false);
		m_processingActive = false;
	}
	m_processData.unprepare();

	// ARA objects can retain plug-in roles and worker resources. They must be
	// released before the controller/provider and DLL module disappear.
	m_araDocument.reset();
	m_pendingAraArchive.clear();

	m_midiMapping = nullptr;
	m_processor = nullptr;
	if (m_controller) { m_controller->setComponentHandler(nullptr); }
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
	// Follow the real song timeline (in output-rate frames) for the transport
	// position. Tempo-synced plug-ins (e.g. Kickstart's host-sync ducking) lock
	// their cycle to this position, so a free-running counter would drift out of
	// alignment with the song's bars depending on where playback started. ARA
	// playback rendering needs it for the same reason (mapping playback regions
	// to the correct position).
	//
	// Song::processNextBuffer() advances the transport by a whole period BEFORE
	// the tracks/mixer render, so by the time an effect processes, getFrames()
	// already points at the END of this block. Subtract the block size so the
	// context describes the START of the block being processed - otherwise the
	// sync is one period (e.g. ~6 ms) early, which is audible as a mis-aligned
	// ducking start on a mixer/effect chain.
	if (song != nullptr)
	{
		const auto blockStart = static_cast<Steinberg::int64>(song->getFrames()) - frames;
		m_processContext.projectTimeSamples = blockStart > 0 ? blockStart : 0;
	}
	else
	{
		m_processContext.projectTimeSamples = m_projectTimeSamples;
	}
	m_processContext.state |= Vst::ProcessContext::kTempoValid | Vst::ProcessContext::kTimeSigValid;
	// continuous (free-running) time always advances, even when stopped
	// (note: the SDK field is spelled "continous")
	m_processContext.continousTimeSamples = m_projectTimeSamples;
	m_processContext.state |= Vst::ProcessContext::kContTimeValid;
	if (song)
	{
		m_processContext.tempo = song->getTempo();
		m_processContext.timeSigNumerator = song->getTimeSigModel().getNumerator();
		m_processContext.timeSigDenominator = song->getTimeSigModel().getDenominator();
		if (song->isPlaying()) { m_processContext.state |= Vst::ProcessContext::kPlaying; }

		// musical position in quarter notes (needed for the plug-in's beat-based
		// ruler / playhead, e.g. Vovious "Use DAW Tempo / Beat")
		const double seconds = static_cast<double>(m_processContext.projectTimeSamples) / m_sampleRate;
		const double quartersPerBar = 4.0 * m_processContext.timeSigNumerator
				/ m_processContext.timeSigDenominator;
		const double quarters = seconds * m_processContext.tempo / 60.0;
		m_processContext.projectTimeMusic = quarters;
		m_processContext.barPositionMusic = quartersPerBar > 0.0
				? std::floor(quarters / quartersPerBar) * quartersPerBar : 0.0;
		m_processContext.state |= Vst::ProcessContext::kProjectTimeMusicValid
				| Vst::ProcessContext::kBarPositionValid;
	}
	else
	{
		m_processContext.tempo = 120.;
		m_processContext.timeSigNumerator = 4;
		m_processContext.timeSigDenominator = 4;
	}

#ifdef LMMS_HAVE_ARA
	if (araActive())
	{
		const bool playing = (m_processContext.state & Vst::ProcessContext::kPlaying) != 0;
		if (playing != m_lastAraPlaying)
		{
			m_lastAraPlaying = playing;
			araDebugLog(QString("transport -> %1 (projectTimeSamples=%2)")
					.arg(playing ? "PLAY" : "STOP")
					.arg(static_cast<qlonglong>(m_processContext.projectTimeSamples)));
		}
	}
#endif

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

	// forward MIDI events generated by the plug-in (arpeggiators, MIDI FX,
	// drum maps ...) to the host, e.g. for the track's MIDI output port
	if (m_midiOutputHandler)
	{
		const int32 eventCount = m_outputEvents.getEventCount();
		for (int32 i = 0; i < eventCount; ++i)
		{
			Vst::Event e = {};
			if (m_outputEvents.getEvent(i, e) != kResultOk) { continue; }
			switch (e.type)
			{
				case Vst::Event::kNoteOnEvent:
					m_midiOutputHandler(MidiEvent(MidiNoteOn, e.noteOn.channel, e.noteOn.pitch,
							static_cast<uint16_t>(std::clamp(e.noteOn.velocity, 0.f, 1.f) * 127.f)));
					break;
				case Vst::Event::kNoteOffEvent:
					m_midiOutputHandler(MidiEvent(MidiNoteOff, e.noteOff.channel, e.noteOff.pitch, 0));
					break;
				case Vst::Event::kLegacyMIDICCOutEvent:
					switch (e.midiCCOut.controlNumber)
					{
						case Vst::kAfterTouch:
							m_midiOutputHandler(MidiEvent(MidiChannelPressure,
									e.midiCCOut.channel, e.midiCCOut.value, 0));
							break;
						case Vst::kPitchBend:
							m_midiOutputHandler(MidiEvent(MidiPitchBend, e.midiCCOut.channel,
									e.midiCCOut.value | (e.midiCCOut.value2 << 7)));
							break;
						default:
							if (e.midiCCOut.controlNumber >= 0 && e.midiCCOut.controlNumber < 128)
							{
								m_midiOutputHandler(MidiEvent(MidiControlChange, e.midiCCOut.channel,
										e.midiCCOut.controlNumber, e.midiCCOut.value));
							}
							break;
					}
					break;
				default:
					break;
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

	if (m_failed || !m_controller || gui::getGUI() == nullptr) { return; }

	if (m_editorWidget != nullptr)
	{
		auto win = static_cast<Vst3EditorWidget*>(m_editorWidget)->windowWidget();
		win->show();
		win->raise();
		m_editorWidget->setFocus();
		return;
	}

	m_view = owned(m_controller->createView(Vst::ViewType::kEditor));
	m_editorCreationTried = true;
	if (!m_view) { return; }

	auto widget = new Vst3EditorWidget(this, m_view.get());
	auto subWindow = gui::getGUI()->mainWindow()->addWindowedWidget(widget);

	ViewRect size = {};
	int w = 640, h = 480;
	if (m_view->getSize(&size) == kResultTrue)
	{
		w = size.getWidth();
		h = size.getHeight();
	}
	widget->setPluginSize(w, h, m_view->canResize() != kResultTrue);

	m_view->setFrame(this);
	// set before attached(): plug-ins like Vital report their real size via
	// resizeView() while attaching, which must not be rejected
	m_editorWidget = widget;
	if (m_view->attached(reinterpret_cast<void*>(widget->container()->winId()),
			kPlatformTypeHWND) != kResultTrue)
	{
		m_view->setFrame(nullptr);
		m_view = nullptr;
		m_editorWidget = nullptr;
		delete subWindow;	// deletes the widget as well
		return;
	}

	subWindow->show();
	widget->show();

	// showing detaches the window (reparenting it); re-apply the plug-in
	// size afterwards, asking the view again since it may have changed
	// during attached()
	if (m_view->getSize(&size) == kResultTrue)
	{
		w = size.getWidth();
		h = size.getHeight();
	}
	widget->setPluginSize(w, h, m_view->canResize() != kResultTrue);
}




QWidget* Vst3Plugin::createEmbeddedEditor(QWidget* parent)
{
	using namespace Steinberg;

	if (m_failed || !m_controller || gui::getGUI() == nullptr) { return nullptr; }
	// only support embedding when no editor is open yet
	if (m_editorWidget != nullptr) { return nullptr; }

	m_view = owned(m_controller->createView(Vst::ViewType::kEditor));
	m_editorCreationTried = true;
	if (!m_view) { return nullptr; }

	auto widget = new Vst3EditorWidget(this, m_view.get());
	widget->setParent(parent);

	ViewRect size = {};
	int w = 640, h = 480;
	if (m_view->getSize(&size) == kResultTrue)
	{
		w = size.getWidth();
		h = size.getHeight();
	}

	widget->setPluginSize(w, h, m_view->canResize() != kResultTrue);

	m_view->setFrame(this);
	// set before attached(): plug-ins report their real size via resizeView()
	// while attaching, which must not be rejected
	m_editorWidget = widget;
	m_editorEmbedded = true;
	if (m_view->attached(reinterpret_cast<void*>(widget->container()->winId()),
			kPlatformTypeHWND) != kResultTrue)
	{
		m_view->setFrame(nullptr);
		m_view = nullptr;
		m_editorWidget = nullptr;
		m_editorEmbedded = false;
		delete widget;
		return nullptr;
	}

	// show the native window so the plug-in actually paints (some plug-ins,
	// e.g. Ozone, only render once their window is visible), then re-apply the
	// size since it may have changed while attaching
	widget->show();
	if (m_view->getSize(&size) == kResultTrue)
	{
		w = size.getWidth();
		h = size.getHeight();
	}
	widget->setPluginSize(w, h, m_view->canResize() != kResultTrue);

	// when the embedding widget goes away (dialog/effect closed), detach the
	// plug-in view so we never touch a dead HWND
	connect(widget, &QObject::destroyed, this, [this]()
	{
		if (m_view)
		{
			m_view->removed();
			m_view->setFrame(nullptr);
			m_view = nullptr;
		}
		m_editorWidget = nullptr;
		m_editorEmbedded = false;
	});

	return widget;
}




void Vst3Plugin::hideEditor()
{
	if (m_editorWidget != nullptr)
	{
		static_cast<Vst3EditorWidget*>(m_editorWidget)->windowWidget()->hide();
	}
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
		auto* editor = static_cast<Vst3EditorWidget*>(m_editorWidget);
		if (m_editorEmbedded)
		{
			// the embedding container (e.g. the effect dialog) owns the widget;
			// just tell it the view is gone so its resize events stay safe
			editor->clearView();
		}
		else
		{
			// delete the wrapping subwindow (which owns the widget), or the bare
			// widget if it was never added to the workspace
			delete editor->windowWidget();
		}
		m_editorWidget = nullptr;
	}
	m_editorEmbedded = false;
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
	{
		QMutexLocker lock(&m_queueMutex);
		m_pendingParams[id] = valueNormalized;
	}

	// mirror the change into the LMMS parameter model so automation display,
	// controller connections and project saving stay in sync when the user
	// turns a knob in the plug-in's own GUI
	for (const auto& p : m_params)
	{
		if (p.id == id)
		{
			m_applyingParamFromPlugin = true;
			p.model->setValue(static_cast<float>(valueNormalized));
			m_applyingParamFromPlugin = false;
			break;
		}
	}

	// surface the parameter the user just edited to the top of the list
	bringParamToFront(id);
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

	auto* song = Engine::getSong();
	const double tempo = song ? static_cast<double>(song->getTempo()) : 120.0;
	const int tsNum = song ? song->getTimeSigModel().getNumerator() : 4;
	const int tsDen = song ? song->getTimeSigModel().getDenominator() : 4;

	// A VST3 instance can be bound to an ARA document controller only once.
	// If we already have a document, apply clip edits by updating its regions
	// in place instead of tearing it down and rebinding (which would fail).
	if (m_araDocument && m_araDocument->isValid())
	{
		return m_araDocument->updateRegions(sources, tempo, tsNum, tsDen);
	}

	// First bind: bindToDocumentControllerWithRoles only succeeds while no
	// editor view (IPlugView) exists. If audio was dropped onto the track
	// while the plug-in window was already open, tear the view down, bind,
	// then bring it back (mirrors the normal "bind, then create view" order).
	m_araDocument.reset();
	const bool restoreEditor = (m_editorWidget != nullptr);
	const bool restoreEmbeddedEditor = restoreEditor && m_editorEmbedded;
	QWidget* oldEmbeddedEditor = restoreEmbeddedEditor ? m_editorWidget : nullptr;
	QWidget* embeddedParent = oldEmbeddedEditor != nullptr
			? oldEmbeddedEditor->parentWidget() : nullptr;
	auto* embeddedLayout = embeddedParent != nullptr
			? qobject_cast<QBoxLayout*>(embeddedParent->layout()) : nullptr;
	const int embeddedLayoutIndex = embeddedLayout != nullptr
			? embeddedLayout->indexOf(oldEmbeddedEditor) : -1;
	destroyEditor();

	auto doc = std::make_unique<Vst3AraDocument>();
	const bool ok = doc->setup(static_cast<const ARA::ARAFactory*>(m_araFactory),
			entryPoint.get(), sources, tempo, tsNum, tsDen);
	if (ok)
	{
		m_araDocument = std::move(doc);
		// restore any ARA state saved with the project (analysis, edits);
		// the sources now exist with the persistent IDs the archive expects
		if (!m_pendingAraArchive.isEmpty())
		{
			m_araDocument->restoreArchive(m_pendingAraArchive);
			m_pendingAraArchive.clear();
		}
	}

	if (restoreEmbeddedEditor)
	{
		// Binding an ARA document must happen before creating IPlugView. If the
		// common VST3 dialog was already open, destroy its now-detached native
		// host widget and recreate the editor at the same position. Calling
		// showEditor() here would incorrectly create a separate workspace
		// window and leave an empty widget behind in the GUI tab.
		delete oldEmbeddedEditor;
		QWidget* editor = createEmbeddedEditor(embeddedParent);
		if (editor != nullptr && embeddedLayout != nullptr)
		{
			embeddedLayout->insertWidget(
				embeddedLayoutIndex >= 0 ? embeddedLayoutIndex : 0, editor, 1);
		}
	}
	else if (restoreEditor)
	{
		showEditor();
	}
	return ok;
#else
	Q_UNUSED(sources)
	return false;
#endif
}




//! A layout that arranges its widgets left-to-right and wraps to the next row
//! based on the available width (like the standard Qt FlowLayout example),
//! with support for reordering items. No signals/slots -> no Q_OBJECT needed.
class FlowLayout : public QLayout
{
public:
	explicit FlowLayout(QWidget* parent, int margin = 6, int spacing = 6)
		: QLayout(parent), m_hSpace(spacing), m_vSpace(spacing)
	{
		setContentsMargins(margin, margin, margin, margin);
	}
	~FlowLayout() override { QLayoutItem* item; while ((item = takeAt(0))) { delete item; } }

	void addItem(QLayoutItem* item) override { m_items.append(item); }
	int count() const override { return m_items.size(); }
	QLayoutItem* itemAt(int index) const override { return m_items.value(index); }
	QLayoutItem* takeAt(int index) override
	{
		return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
	}
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
	void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); doLayout(rect, false); }
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override
	{
		QSize size;
		for (QLayoutItem* item : m_items) { size = size.expandedTo(item->minimumSize()); }
		const QMargins m = contentsMargins();
		return size + QSize(m.left() + m.right(), m.top() + m.bottom());
	}

	//! move the widget to the front (top-left) of the flow
	void moveToFront(QWidget* w)
	{
		for (int i = 0; i < m_items.size(); ++i)
		{
			if (m_items[i]->widget() == w)
			{
				if (i == 0) { return; }
				m_items.move(i, 0);
				invalidate();
				return;
			}
		}
	}

private:
	int doLayout(const QRect& rect, bool testOnly) const
	{
		const QMargins m = contentsMargins();
		const QRect effective = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
		int x = effective.x();
		int y = effective.y();
		int lineHeight = 0;
		for (QLayoutItem* item : m_items)
		{
			const QSize hint = item->sizeHint();
			int next = x + hint.width() + m_hSpace;
			if (next - m_hSpace > effective.right() && lineHeight > 0)
			{
				x = effective.x();
				y = y + lineHeight + m_vSpace;
				next = x + hint.width() + m_hSpace;
				lineHeight = 0;
			}
			if (!testOnly) { item->setGeometry(QRect(QPoint(x, y), hint)); }
			x = next;
			lineHeight = qMax(lineHeight, hint.height());
		}
		return y + lineHeight - rect.y() + m.bottom();
	}

	QList<QLayoutItem*> m_items;
	int m_hSpace;
	int m_vSpace;
};




void Vst3Plugin::createParameterModels(Model* parent)
{
	using namespace Steinberg;
	if (m_failed || m_controller == nullptr || !m_params.empty()) { return; }

	const int32 count = m_controller->getParameterCount();
	for (int32 i = 0; i < count; ++i)
	{
		Vst::ParameterInfo info;
		if (m_controller->getParameterInfo(i, info) != kResultTrue) { continue; }
		// only expose parameters the plug-in allows to be automated, and skip
		// read-only / hidden / list-separator "parameters"
		if ((info.flags & Vst::ParameterInfo::kIsReadOnly) != 0) { continue; }
		if ((info.flags & Vst::ParameterInfo::kCanAutomate) == 0) { continue; }

		auto title = QString::fromStdString(VST3::StringConvert::convert(info.title));
		if (title.isEmpty()) { title = QStringLiteral("Param %1").arg(info.id); }

		const auto current = static_cast<float>(m_controller->getParamNormalized(info.id));
		// VST3 parameters are normalized to 0..1; a fine step keeps automation smooth
		auto* model = new FloatModel(current, 0.f, 1.f, 0.001f, parent, title);
		const auto id = info.id;
		connect(model, &FloatModel::dataChanged, this, [this, id, model]() {
			if (m_applyingParamFromPlugin) { return; }
			sendParameterToPlugin(id, model->value());
		}, Qt::DirectConnection);

		m_params.push_back({info.id, model, title});
	}
}




void Vst3Plugin::sendParameterToPlugin(Steinberg::Vst::ParamID id, float normalized)
{
	{
		QMutexLocker lock(&m_queueMutex);
		m_pendingParams[id] = normalized;
	}
	// keep the edit controller (and thus the plug-in GUI) in sync
	if (m_controller) { m_controller->setParamNormalized(id, normalized); }
}




void Vst3Plugin::saveParameterModels(QDomDocument& doc, QDomElement& element)
{
	for (const auto& p : m_params)
	{
		// only persist parameters that carry automation or a controller
		// connection; plain values are already covered by the plug-in state
		if (p.model->isAutomated() || p.model->controllerConnection() != nullptr)
		{
			p.model->saveSettings(doc, element, "vst3param_" + QString::number(p.id));
		}
	}
}




void Vst3Plugin::loadParameterModels(const QDomElement& element)
{
	for (const auto& p : m_params)
	{
		const QString name = "vst3param_" + QString::number(p.id);
		if (!element.firstChildElement(name).isNull()
				|| element.hasAttribute(name))
		{
			p.model->loadSettings(element, name);
		}
	}
}




QWidget* Vst3Plugin::createParameterWidget(QWidget* parent)
{
	if (m_params.empty()) { return nullptr; }

	auto* content = new QWidget(parent);
	auto* outer = new QVBoxLayout(content);
	outer->setContentsMargins(0, 0, 0, 0);

	auto* scroll = new QScrollArea(content);
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	outer->addWidget(scroll);

	// knobs in a reflowing layout: columns follow the width, and the last
	// touched parameter is moved to the front
	auto* grid = new QWidget;
	m_paramFlow = new FlowLayout(grid);
	m_paramKnobs.assign(m_params.size(), nullptr);
	for (size_t i = 0; i < m_params.size(); ++i)
	{
		auto* knob = new gui::Knob(gui::KnobType::Small17, m_params[i].title, grid);
		knob->setModel(m_params[i].model);
		knob->setHintText(m_params[i].title + ":", "");
		m_paramFlow->addWidget(knob);
		m_paramKnobs[i] = knob;
	}
	scroll->setWidget(grid);

	// forget the layout/knobs when this widget is destroyed so bringParamToFront
	// does not touch dangling pointers
	connect(content, &QObject::destroyed, this, [this]() {
		m_paramFlow = nullptr;
		m_paramKnobs.clear();
	});
	return content;
}




void Vst3Plugin::toggleParameterWindow()
{
	if (gui::getGUI() == nullptr || m_params.empty()) { return; }

	if (m_paramWindow != nullptr)
	{
		auto win = m_paramWindow->parentWidget() ? m_paramWindow->parentWidget() : m_paramWindow;
		win->setVisible(!win->isVisible());
		if (win->isVisible()) { win->raise(); }
		return;
	}

	auto* content = createParameterWidget();
	if (content == nullptr) { return; }
	content->setWindowTitle(name() + tr(" - Parameters"));

	auto* subWindow = gui::getGUI()->mainWindow()->addWindowedWidget(content);
	subWindow->resize(560, 420);
	m_paramWindow = content;
	// forget the window when it is destroyed (the knob cleanup is handled by
	// createParameterWidget's own destroyed handler)
	connect(content, &QObject::destroyed, this, [this]() {
		m_paramWindow = nullptr;
	});
	subWindow->show();
	content->show();
}




void Vst3Plugin::bringParamToFront(Steinberg::Vst::ParamID id)
{
	if (m_paramFlow == nullptr) { return; }
	for (size_t i = 0; i < m_params.size(); ++i)
	{
		if (m_params[i].id == id)
		{
			if (i < m_paramKnobs.size() && m_paramKnobs[i] != nullptr)
			{
				m_paramFlow->moveToFront(m_paramKnobs[i]);
			}
			return;
		}
	}
}




QByteArray Vst3Plugin::saveAraArchive()
{
#ifdef LMMS_HAVE_ARA
	if (m_araDocument && m_araDocument->isValid())
	{
		return m_araDocument->storeArchive();
	}
	// not bound yet (e.g. project saved without opening the editor): keep any
	// archive that is still pending restore so it survives a resave
	return m_pendingAraArchive;
#else
	return {};
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
