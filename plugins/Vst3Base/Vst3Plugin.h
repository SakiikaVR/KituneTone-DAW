/*
 * Vst3Plugin.h - in-process VST3 hosting for LMMS
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

#ifndef LMMS_VST3_PLUGIN_H
#define LMMS_VST3_PLUGIN_H

#include <QObject>
#include <QString>
#include <QMutex>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/gui/iplugview.h"

#include "LmmsTypes.h"
#include "vst3base_export.h"

class QDomDocument;
class QDomElement;
class QTimer;
class QWidget;

namespace lmms
{

class FloatModel;
class MidiEvent;
class Model;
class SampleFrame;

//! In-process host for a single VST3 plug-in instance (effect or instrument).
class VST3BASE_EXPORT Vst3Plugin :
	public QObject,
	public Steinberg::Vst::IComponentHandler,
	public Steinberg::IPlugFrame
{
	Q_OBJECT
public:
	enum class Kind { Instrument, Effect };

	Vst3Plugin(const QString& path, Kind kind, const QString& uid = QString());
	~Vst3Plugin() override;

	bool failed() const { return m_failed; }
	QString errorString() const { return m_error; }

	QString name() const { return m_name; }
	QString vendor() const { return m_vendor; }
	QString uidString() const { return m_uid; }
	QString path() const { return m_path; }

	//! whether the plug-in exposes an ARA factory (e.g. Melodyne).
	bool hasAra() const { return m_hasAra; }
	QString araName() const { return m_araName; }

	//! One audio region to expose to ARA: an audio file placed on the song
	//! timeline. offsetInSourceSeconds is where in the file the region starts.
	struct AraSource
	{
		QString file;
		double startInSongSeconds = 0.0;
		double durationSeconds = 0.0;      //!< 0 = whole file
		double offsetInSourceSeconds = 0.0;
	};

	//! Bind this plug-in instance to an ARA document exposing the given audio
	//! sources/regions (typically the sample clips of the track the effect is
	//! on). Rebuilds any previous ARA document. Returns true on success.
	bool enableAra(const std::vector<AraSource>& sources);
	bool araActive() const;

	//! Serialize the plug-in's ARA state (analysis/edits) for the project.
	//! Empty if ARA is not active.
	QByteArray saveAraArchive();
	//! Provide an ARA archive to restore the next time the document is bound
	//! (used on project load, before the sources exist).
	void setPendingAraArchive(const QByteArray& archive) { m_pendingAraArchive = archive; }

	//! Process one block on the audio thread. For instruments pass in = nullptr.
	//! in/out may alias. frames <= MAXIMUM_BUFFER_SIZE.
	void process(const SampleFrame* in, SampleFrame* out, f_cnt_t frames);

	//! Queue a MIDI event (note on/off, CC, pitch bend, ...) for the next block.
	void queueMidiEvent(const MidiEvent& event, f_cnt_t offset);

	//! Called on the audio thread for every MIDI event the plug-in outputs
	//! (notes, CCs, pitch bend, aftertouch). Set once right after loading,
	//! before any processing happens.
	void setMidiOutputHandler(std::function<void(const MidiEvent&)> handler)
	{
		m_midiOutputHandler = std::move(handler);
	}

	void saveState(QDomDocument& doc, QDomElement& element);
	void loadState(const QDomElement& element);

	//! Editor handling (GUI thread only)
	bool hasEditor();
	void showEditor();
	void hideEditor();
	void toggleEditor();
	bool editorVisible() const;

	//! Expose the plug-in's automatable parameters as LMMS FloatModels
	//! (parented to the given LMMS instrument/effect model) so they can be
	//! automated and controller-mapped. Call once after loading.
	void createParameterModels(Model* parent);
	bool hasParameters() const { return !m_params.empty(); }
	//! Show/hide a window of LMMS knobs bound to the parameter models.
	void toggleParameterWindow();
	//! Persist parameter automation/controller connections into the project.
	void saveParameterModels(QDomDocument& doc, QDomElement& element);
	void loadParameterModels(const QDomElement& element);

	// --- Steinberg::Vst::IComponentHandler ---
	Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
			Steinberg::Vst::ParamValue valueNormalized) override;
	Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
	Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

	// --- Steinberg::IPlugFrame ---
	Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
			Steinberg::ViewRect* newSize) override;

	// --- Steinberg::FUnknown ---
	Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
	Steinberg::uint32 PLUGIN_API addRef() override { return 100; }
	Steinberg::uint32 PLUGIN_API release() override { return 100; }

	//! Discover all VST3 modules in the standard system locations.
	static std::vector<std::string> modulePaths();

signals:
	void editorClosed();

private:
	bool load(const QString& path, const QString& uid);
	void unload();
	bool setupProcessing(double sampleRate, Steinberg::int32 maxBlockSize);
	void applyPendingParameters();
	void syncOutputParameters();
	void destroyEditor();

	Kind m_kind;
	bool m_failed = true;
	QString m_error;
	QString m_path;
	QString m_name;
	QString m_vendor;
	QString m_uid;
	bool m_hasAra = false;
	QString m_araName;
	const void* m_araFactory = nullptr;       //!< const ARA::ARAFactory*
	std::unique_ptr<class Vst3AraDocument> m_araDocument;
	QByteArray m_pendingAraArchive;           //!< restored on the next bind

	// automatable parameters exposed to LMMS
	struct ParamModel
	{
		Steinberg::Vst::ParamID id;
		FloatModel* model;
		QString title;
	};
	std::vector<ParamModel> m_params;
	bool m_applyingParamFromPlugin = false;    //!< guard against feedback loops
	QWidget* m_paramWindow = nullptr;
	class FlowLayout* m_paramFlow = nullptr;   //!< reflowing layout of param knobs
	std::vector<QWidget*> m_paramKnobs;        //!< one knob per m_params entry
	void sendParameterToPlugin(Steinberg::Vst::ParamID id, float normalized);
	//! move a parameter's knob to the front of the param window (last touched)
	void bringParamToFront(Steinberg::Vst::ParamID id);

	VST3::Hosting::Module::Ptr m_module;
	Steinberg::IPtr<Steinberg::Vst::PlugProvider> m_provider;
	Steinberg::IPtr<Steinberg::Vst::IComponent> m_component;
	Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> m_processor;
	Steinberg::IPtr<Steinberg::Vst::IEditController> m_controller;
	Steinberg::IPtr<Steinberg::Vst::IMidiMapping> m_midiMapping;

	Steinberg::Vst::HostProcessData m_processData;
	Steinberg::Vst::ProcessContext m_processContext = {};
	Steinberg::Vst::EventList m_inputEvents;
	Steinberg::Vst::EventList m_outputEvents;
	Steinberg::Vst::ParameterChanges m_inputParamChanges;
	Steinberg::Vst::ParameterChanges m_outputParamChanges;
	std::function<void(const MidiEvent&)> m_midiOutputHandler;
	bool m_lastAraPlaying = false;

	bool m_processingActive = false;
	double m_sampleRate = 0.;
	Steinberg::int32 m_maxBlockSize = 0;
	Steinberg::int64 m_projectTimeSamples = 0;
	bool m_hasInputBus = false;
	bool m_hasOutputBus = false;

	// events/parameters queued from other threads, consumed by process()
	struct QueuedEvent { Steinberg::Vst::Event event; };
	QMutex m_queueMutex;
	std::vector<Steinberg::Vst::Event> m_pendingEvents;
	std::map<Steinberg::Vst::ParamID, double> m_pendingParams;

	// parameter updates produced by process(), forwarded to the controller
	QMutex m_outputParamMutex;
	std::map<Steinberg::Vst::ParamID, double> m_outputParams;
	QTimer* m_outputSyncTimer = nullptr;

	// editor
	Steinberg::IPtr<Steinberg::IPlugView> m_view;
	QWidget* m_editorWidget = nullptr;
	bool m_editorCreationTried = false;
};


} // namespace lmms

#endif // LMMS_VST3_PLUGIN_H
