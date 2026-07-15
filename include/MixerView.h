/*
 * MixerView.h - effect-mixer-view for LMMS
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_MIXER_VIEW_H
#define LMMS_GUI_MIXER_VIEW_H

#include <functional>

#include <QWidget>

#include "MixerChannelView.h"
#include "ModelView.h"
#include "SerializingObject.h"

class QDomDocument;  // IWYU pragma: keep
class QDomElement;  // IWYU pragma: keep
class QHBoxLayout;
class QLabel;
class QMouseEvent;  // IWYU pragma: keep
class QStackedLayout;
class QScrollArea;

namespace lmms
{
	class Mixer;
	class MixerChannel;
}

namespace lmms::gui
{

class AutomatableButton;
class EffectRackView;
class Fader;

//! Vertical stereo peak meter for the currently selected mixer channel, shown
//! at the left edge of the mixer with a dB scale (+3 ... -48 dBFS).
//! Display-only for the level; clicking it selects the monitoring effect chain.
class CurrentChannelMeter : public QWidget
{
public:
	explicit CurrentChannelMeter(QWidget* parent = nullptr);
	void setPeaks(float left, float right);
	void setChannelName(const QString& name);
	//! called when the meter is clicked (used to select the monitor chain)
	void setClickHandler(std::function<void()> handler) { m_clickHandler = std::move(handler); }
	void setSelected(bool selected);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	float m_peakL = 0.f;
	float m_peakR = 0.f;
	QString m_name;
	std::function<void()> m_clickHandler;
	bool m_selected = false;
};

class LMMS_EXPORT MixerView
	: public QWidget
	, public ModelView
	, public SerializingObjectHook
{
	Q_OBJECT
public:
	MixerView(Mixer* mixer);
	void keyPressEvent(QKeyEvent* e) override;

	void saveSettings(QDomDocument& doc, QDomElement& domElement) override;
	void loadSettings(const QDomElement& domElement) override;

	inline MixerChannelView* currentMixerChannel()
	{
		return m_currentMixerChannel;
	}

	//! true when the "Current" monitor is the active selection instead of a
	//! mixer channel; used so the two selections are mutually exclusive
	inline bool monitorSelected() const
	{
		return m_monitorSelected;
	}

	inline MixerChannelView* channelView(int index)
	{
		return m_mixerChannelViews[index];
	}


	void setCurrentMixerChannel(MixerChannelView* channel);
	void setCurrentMixerChannel(int channel);

	void clear();


	// display the send button and knob correctly
	void updateMixerChannel(int index);

	// notify the view that a mixer channel was deleted
	void deleteChannel(int index);

	// delete all unused channels
	void deleteUnusedChannels();

	// move the channel to the left or right
	void moveChannelLeft(int index);
	void moveChannelRight(int index);

	void renameChannel(int index);

	// make sure the display syncs up with the mixer.
	// useful for loading projects
	void refreshDisplay();

public slots:
	int addNewChannel();

private slots:
	void updateFaders();
	// TODO This should be improved. Currently the solo and mute models are connected via
	// the MixerChannelView's constructor with the MixerView. It would already be an improvement
	// if the MixerView connected itself to each new MixerChannel that it creates/handles.
	void toggledSolo();
	void toggledMute();

private:
	Mixer* getMixer() const;
	void updateAllMixerChannels();
	void connectToSoloAndMute(int channelIndex);
	void disconnectFromSoloAndMute(int channelIndex);

private:
	QVector<MixerChannelView*> m_mixerChannelViews;

	MixerChannelView* m_currentMixerChannel;
	bool m_monitorSelected = false;

	CurrentChannelMeter* m_currentMeter = nullptr;
	EffectRackView* m_monitorRackView = nullptr;

	QScrollArea* channelArea;
	QHBoxLayout* chLayout;
	QWidget* m_channelAreaWidget;
	QStackedLayout* m_racksLayout;
	QWidget* m_racksWidget;
	Mixer* m_mixer;

	void updateMaxChannelSelector();

	friend class MixerChannelView;
} ;


} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_VIEW_H
