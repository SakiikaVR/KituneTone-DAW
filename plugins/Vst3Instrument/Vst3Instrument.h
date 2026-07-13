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

class QLabel;
class QPushButton;

namespace lmms
{

class Vst3Plugin;

namespace gui
{
class Vst3InstrumentView;
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

class Vst3InstrumentView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent);

protected slots:
	void openPlugin();
	void toggleGui();
	void toggleParams();
	void updateName();

protected:
	void modelChanged() override;

private:
	QLabel* m_nameLabel;
	QPushButton* m_openButton;
	QPushButton* m_guiButton;
	QPushButton* m_paramsButton;
};

} // namespace gui


} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_H
