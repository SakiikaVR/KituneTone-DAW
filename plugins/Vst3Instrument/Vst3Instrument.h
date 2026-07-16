/*
 * Vst3Instrument.h - instrument plugin hosting native VST3 instruments
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

#ifndef LMMS_VST3_INSTRUMENT_H
#define LMMS_VST3_INSTRUMENT_H

#include <QMutex>
#include <memory>

#include "Instrument.h"
#include "InstrumentView.h"

class QShowEvent;

namespace lmms
{

class Vst3Plugin;

namespace gui
{
class Vst3InstrumentView;
class Vst3PluginView;
}


class Vst3Instrument : public Instrument
{
	Q_OBJECT
public:
	Vst3Instrument(InstrumentTrack* track);
	~Vst3Instrument() override;

	void play(SampleFrame* workingBuffer) override;
	bool handleMidiEvent(const MidiEvent& event, const TimePos& time = TimePos(),
			f_cnt_t offset = 0) override;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override;

	void loadFile(const QString& file) override;

	gui::PluginView* instantiateView(QWidget* parent) override;

	Vst3Plugin* plugin() const { return m_plugin.get(); }

signals:
	void pluginChanged();

private:
	void closePlugin();

	std::unique_ptr<Vst3Plugin> m_plugin;
	QMutex m_pluginMutex;
	QString m_pluginFile;

	friend class gui::Vst3InstrumentView;
};


namespace gui
{

class Vst3InstrumentView : public InstrumentView
{
	Q_OBJECT
public:
	Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent);

	// resizable so the embedded plug-in GUI can grow the instrument window
	bool isResizable() const override { return m_resizable; }
	bool usesUnifiedTrackWindow() const override { return true; }

protected:
	void modelChanged() override;
	void showEvent(QShowEvent* event) override;

private:
	//! Rebuild the shared VST3 tabs after the hosted plug-in changes.
	void buildTabs();
	//! Build track-level controls which do not exist in an effect window.
	QWidget* createTrackTab(InstrumentTrack* track);
	void saveTrackPreset(InstrumentTrack* track);
	//! Embed the plug-in's native GUI lazily.
	void ensureEmbedded();
	//! grow the instrument window to fit the embedded GUI
	void fitWindow();

	Vst3PluginView* m_pluginView = nullptr;
	bool m_resizable = false;
};

} // namespace gui


} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
