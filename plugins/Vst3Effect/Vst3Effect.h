/*
 * Vst3Effect.h - effect plugin hosting native VST3 effects
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

#ifndef LMMS_VST3_EFFECT_H
#define LMMS_VST3_EFFECT_H

#include <QMutex>
#include <QPointer>
#include <memory>

#include "Effect.h"
#include "EffectControls.h"
#include "EffectControlDialog.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"

class QLabel;
class QPushButton;
class QShowEvent;

namespace lmms
{

class Clip;
class SampleTrack;
class Vst3Effect;
class Vst3Plugin;

namespace gui
{
class Vst3EffectControlDialog;
class Vst3PluginView;
}


class Vst3EffectControls : public EffectControls
{
	Q_OBJECT
public:
	Vst3EffectControls(Vst3Effect* effect);

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return "Vst3EffectControls"; }

	// report one control so the effect chain shows the "Controls" button,
	// which opens the dialog with access to the plugin's native GUI
	int controlCount() override { return 1; }
	gui::EffectControlDialog* createView() override;

private:
	Vst3Effect* m_effect;

	friend class gui::Vst3EffectControlDialog;
};


class Vst3Effect : public Effect, public MidiEventProcessor
{
public:
	Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~Vst3Effect() override = default;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override { return &m_controls; }

	Vst3Plugin* plugin() const { return m_plugin.get(); }

	//! MIDI in/out routing port for the hosted plug-in (disabled by default).
	//! Enabling input feeds MIDI from the internal bus / devices into the
	//! effect's plug-in; enabling output routes MIDI the plug-in emits back out.
	MidiPort* midiPort() { return &m_midiPort; }

	//! MidiEventProcessor: deliver an incoming MIDI event to the plug-in.
	void processInEvent(const MidiEvent& event, const TimePos& time = TimePos(),
			f_cnt_t offset = 0) override;
	//! MidiEventProcessor: the plug-in's own output is routed through the port
	//! directly, so this end is a no-op.
	void processOutEvent(const MidiEvent& event, const TimePos& time = TimePos(),
			f_cnt_t offset = 0) override;

	//! Locate the sample track this effect is on and expose its clips to the
	//! plug-in via ARA. Returns the number of audio regions set up (0 = failed).
	int syncAraFromTrack();

private:
	bool openPlugin(const QString& file, const QString& uid);

	//! find the sample track whose effect chain contains this effect
	SampleTrack* findOwnerTrack() const;
	//! start following the owner track's clips, re-syncing ARA on changes
	void watchOwnerTrack();
	void watchClip(Clip* clip);
	//! debounced, GUI-thread ARA re-sync
	void scheduleAraSync();

	std::unique_ptr<Vst3Plugin> m_plugin;
	QMutex m_pluginMutex;
	EffectKey m_key;
	Vst3EffectControls m_controls;
	MidiPort m_midiPort;
	QPointer<SampleTrack> m_watchedTrack;
	bool m_araSyncPending = false;

	friend class Vst3EffectControls;
};


namespace gui
{

class Vst3EffectControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	Vst3EffectControlDialog(Vst3EffectControls* controls);

	// this is a normal (tabbed) dialog, not an external UI toggle
	bool togglesExternalUi() const override;
	void toggleExternalUi() override;

protected:
	void showEvent(QShowEvent* event) override;

private:
	Vst3Plugin* plugin() const;
	//! lazily embed the plug-in's native editor into the GUI tab on first show
	void ensureEmbeddedEditor();
	//! grow the enclosing workspace window to fit the current tab contents
	void fitWindowToContents();

	Vst3PluginView* m_pluginView = nullptr;
};

} // namespace gui


} // namespace lmms

#endif // LMMS_VST3_EFFECT_H
