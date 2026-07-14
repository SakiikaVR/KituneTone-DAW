/*
 * MixerView.cpp - effect-mixer-view for LMMS
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

#include "MixerView.h"

#include <algorithm>
#include <array>

#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QKeyEvent>
#include <QStackedLayout>
#include <QStackedWidget>

#include "AutomatableButton.h"
#include "EffectRackView.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "PatternStore.h"
#include "SampleTrack.h"
#include "SendButtonIndicator.h"
#include "Song.h"
#include "SubWindow.h"
#include "TrackContainer.h" // For TrackContainer::TrackList typedef
#include "embed.h"
#include "lmms_math.h"

namespace lmms::gui
{

// --- CurrentChannelMeter: stereo dB peak meter for the selected channel ------

namespace
{
	// dB range shown by the current-channel meter
	constexpr float kMeterTopDb = 3.f;
	constexpr float kMeterBottomDb = -48.f;
	// scale labels (dBFS) drawn next to the bars
	constexpr std::array<int, 6> kMeterScaleDb = {3, 0, -6, -12, -24, -48};
}

CurrentChannelMeter::CurrentChannelMeter(QWidget* parent)
	: QWidget(parent)
{
	setAttribute(Qt::WA_OpaquePaintEvent);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Click to add monitoring-only effects (not exported)"));
}

void CurrentChannelMeter::mousePressEvent(QMouseEvent*)
{
	if (m_clickHandler) { m_clickHandler(); }
}

void CurrentChannelMeter::setSelected(bool selected)
{
	if (selected == m_selected) { return; }
	m_selected = selected;
	update();
}

void CurrentChannelMeter::setPeaks(float left, float right)
{
	if (left == m_peakL && right == m_peakR) { return; }
	m_peakL = left;
	m_peakR = right;
	update();
}

void CurrentChannelMeter::setChannelName(const QString& name)
{
	if (name == m_name) { return; }
	m_name = name;
	update();
}

void CurrentChannelMeter::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(20, 20, 20));

	const int labelH = 16;
	const int top = labelH + 4;
	const int bottom = height() - 4;
	const int meterH = bottom - top;
	if (meterH <= 0) { return; }

	// channel name / header
	p.setPen(QColor(220, 220, 220));
	QFont f = p.font();
	f.setPointSizeF(std::max(6., f.pointSizeF() - 1.));
	p.setFont(f);
	p.drawText(QRect(0, 0, width(), labelH), Qt::AlignCenter, tr("Current"));

	// maps a dBFS value to a y coordinate within the meter
	const auto dbToY = [&](float db) -> float {
		const float clamped = std::clamp(db, kMeterBottomDb, kMeterTopDb);
		const float t = (kMeterTopDb - clamped) / (kMeterTopDb - kMeterBottomDb);
		return top + t * meterH;
	};

	const int barW = 16;
	const int gap = 3;
	const int scaleW = 22;
	// centre the two bars + scale block
	const int block = barW * 2 + gap + scaleW + 4;
	int x = (width() - block) / 2;
	if (x < 1) { x = 1; }
	const QRect lRect(x, top, barW, meterH);
	const QRect rRect(x + barW + gap, top, barW, meterH);

	// gradient: red at the top (>0 dB), yellow around 0, green below
	const auto fillMeter = [&](const QRect& r, float peak) {
		p.fillRect(r, QColor(0, 0, 0));
		const float db = ampToDbfs(std::max(0.0001f, peak));
		const float y = dbToY(db);
		QRect fill(r.left(), static_cast<int>(y), r.width(), r.bottom() - static_cast<int>(y) + 1);
		QLinearGradient grad(0, top, 0, bottom);
		grad.setColorAt(0.0, QColor(255, 70, 70));    // +3 dB
		grad.setColorAt((kMeterTopDb - 0.f) / (kMeterTopDb - kMeterBottomDb), QColor(255, 210, 60)); // 0 dB
		grad.setColorAt((kMeterTopDb - (-6.f)) / (kMeterTopDb - kMeterBottomDb), QColor(120, 220, 70)); // -6 dB
		grad.setColorAt(1.0, QColor(40, 150, 60));    // -48 dB
		p.fillRect(fill, grad);
	};
	fillMeter(lRect, m_peakL);
	fillMeter(rRect, m_peakR);

	// meter outlines
	p.setPen(QColor(80, 80, 80));
	p.drawRect(lRect);
	p.drawRect(rRect);

	// dB scale labels + tick lines to the right of the bars
	const int scaleX = rRect.right() + 3;
	p.setPen(QColor(170, 170, 170));
	QFont sf = p.font();
	sf.setPointSizeF(std::max(5.5, sf.pointSizeF() - 0.5));
	p.setFont(sf);
	for (int db : kMeterScaleDb)
	{
		const int y = static_cast<int>(dbToY(static_cast<float>(db)));
		p.setPen(QColor(70, 70, 70));
		p.drawLine(lRect.left(), y, rRect.right(), y);
		p.setPen(QColor(180, 180, 180));
		const QString txt = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
		p.drawText(QRect(scaleX, y - 7, width() - scaleX, 14), Qt::AlignLeft | Qt::AlignVCenter, txt);
	}

	// highlight when selected (its monitoring effect chain is shown in the rack)
	if (m_selected)
	{
		p.setRenderHint(QPainter::Antialiasing, false);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(90, 170, 255), 2));
		p.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}



MixerView::MixerView(Mixer* mixer) :
	QWidget(),
	ModelView(nullptr, this),
	SerializingObjectHook(),
	m_mixer(mixer)
{
	mixer->setHook(this);

	//QPalette pal = palette();
	//pal.setColor(QPalette::Window, QColor(72, 76, 88));
	//setPalette(pal);

	setAutoFillBackground(true);

	setWindowTitle(tr("Mixer"));
	setWindowIcon(embed::getIconPixmap("mixer"));

	// main-layout
	auto ml = new QHBoxLayout{this};

	// Set margins
	ml->setContentsMargins(0, 4, 0, 0);

	// Channel area
	m_channelAreaWidget = new QWidget;
	chLayout = new QHBoxLayout(m_channelAreaWidget);
	chLayout->setSizeConstraint(QLayout::SetMinimumSize);
	chLayout->setSpacing(0);
	chLayout->setContentsMargins(0, 0, 0, 0);
	chLayout->setAlignment(Qt::AlignLeft);
	m_channelAreaWidget->setLayout(chLayout);

	// create rack layout before creating the first channel
	m_racksWidget = new QWidget;
	m_racksLayout = new QStackedLayout(m_racksWidget);
	m_racksLayout->setContentsMargins(0, 0, 0, 0);
	m_racksWidget->setLayout(m_racksLayout);
	// keep the effect rack from collapsing when it sits next to the current
	// channel meter (the QStackedLayout does not always propagate the page's
	// fixed width to the container)
	m_racksWidget->setMinimumWidth(EffectRackView::DEFAULT_WIDTH);

	// add master channel
	m_mixerChannelViews.resize(mixer->numChannels());
	MixerChannelView * masterView = new MixerChannelView(this, this, 0);
	connectToSoloAndMute(0);
	m_mixerChannelViews[0] = masterView;

	m_racksLayout->addWidget(m_mixerChannelViews[0]->m_effectRackView);

	ml->addWidget(masterView, 0);

	auto mixerChannelSize = masterView->sizeHint();

	// "current channel" meter at the very left of the mixer: a stereo peak
	// meter with a dB scale that always shows the selected channel's level
	m_currentMeter = new CurrentChannelMeter(this);
	m_currentMeter->setFixedWidth(74);
	m_currentMeter->setMinimumHeight(mixerChannelSize.height());
	ml->insertWidget(0, m_currentMeter, 0);

	// monitoring-only effect chain: its rack is stacked with the channel racks
	// and shown when the current-channel meter is clicked. Effects added here
	// are heard on the master output but are not part of the exported mix.
	m_monitorRackView = new EffectRackView(getMixer()->monitorFxChain(), m_racksWidget);
	m_monitorRackView->setFixedWidth(EffectRackView::DEFAULT_WIDTH);
	m_racksLayout->addWidget(m_monitorRackView);
	m_currentMeter->setClickHandler([this]() {
		m_racksLayout->setCurrentWidget(m_monitorRackView);
		m_currentMeter->setSelected(true);
	});

	// add mixer channels
	for (int i = 1; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i] = new MixerChannelView(m_channelAreaWidget, this, i);
		connectToSoloAndMute(i);
		chLayout->addWidget(m_mixerChannelViews[i]);
	}

	// add the scrolling section to the main layout
	// class solely for scroll area to pass key presses down
	class ChannelArea : public QScrollArea
	{
		public:
			ChannelArea(QWidget* parent, MixerView* mv) :
				QScrollArea(parent), m_mv(mv) {}
			~ChannelArea() override = default;
			void keyPressEvent(QKeyEvent* e) override
			{
				m_mv->keyPressEvent(e);
			}
		private:
			MixerView* m_mv;
	};
	channelArea = new ChannelArea(this, this);
	channelArea->setWidget(m_channelAreaWidget);
	channelArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	channelArea->setFrameStyle(QFrame::NoFrame);
	channelArea->setMinimumWidth(mixerChannelSize.width() * 6);
	channelArea->setWidgetResizable(true);

	int const scrollBarExtent = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	channelArea->setMinimumHeight(mixerChannelSize.height() + scrollBarExtent);

	ml->addWidget(channelArea, 1);

	// show the add new mixer channel button
	auto newChannelBtn = new QPushButton(embed::getIconPixmap("new_channel"), QString(), this);
	newChannelBtn->setObjectName("newChannelBtn");
	newChannelBtn->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
	newChannelBtn->setFixedWidth(mixerChannelSize.width());
	newChannelBtn->setFocusPolicy(Qt::NoFocus);
	connect(newChannelBtn, SIGNAL(clicked()), this, SLOT(addNewChannel()));
	ml->addWidget(newChannelBtn, 0);


	// effect rack of the selected channel stays at the right; it edits the
	// current channel's effect chain (the channel's audio passes through it).
	// The current-channel meter on the left pairs with this to form the
	// "current channel" view.
	ml->addWidget(m_racksWidget);

	setCurrentMixerChannel(m_mixerChannelViews[0]);

	updateGeometry();

	auto* mainWindow = getGUI()->mainWindow();

	// timer for updating faders
	connect(mainWindow, &MainWindow::periodicUpdate, this, &MixerView::updateFaders);

	layout()->setSizeConstraint(QLayout::SetMinimumSize);

	// add ourself to workspace
	mainWindow->addWindowedWidget(this);

	parentWidget()->setAttribute(Qt::WA_DeleteOnClose, false);
	parentWidget()->move(5, 310);

	// we want to receive dataChanged-signals in order to update
	setModel(mixer);
}




int MixerView::addNewChannel()
{
	// add new mixer channel and redraw the form.
	Mixer * mix = getMixer();

	int newChannelIndex = mix->createChannel();
	m_mixerChannelViews.push_back(new MixerChannelView(m_channelAreaWidget, this, newChannelIndex));
	connectToSoloAndMute(newChannelIndex);
	chLayout->addWidget(m_mixerChannelViews[newChannelIndex]);
	m_racksLayout->addWidget(m_mixerChannelViews[newChannelIndex]->m_effectRackView);

	updateMixerChannel(newChannelIndex);

	updateMaxChannelSelector();

	return newChannelIndex;
}


void MixerView::refreshDisplay()
{
	// delete all views and re-add them
	for (int i = 1; i<m_mixerChannelViews.size(); ++i)
	{
		// First disconnect from the solo/mute models.
		disconnectFromSoloAndMute(i);

		auto * mixerChannelView = m_mixerChannelViews[i];
		chLayout->removeWidget(mixerChannelView);
		m_racksLayout->removeWidget(mixerChannelView->m_effectRackView);

		delete mixerChannelView;
	}
	m_channelAreaWidget->adjustSize();

	// re-add the views
	m_mixerChannelViews.resize(getMixer()->numChannels());
	for (int i = 1; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i] = new MixerChannelView(m_channelAreaWidget, this, i);
		connectToSoloAndMute(i);

		chLayout->addWidget(m_mixerChannelViews[i]);
		m_racksLayout->addWidget(m_mixerChannelViews[i]->m_effectRackView);
	}

	// set selected mixer channel to 0
	setCurrentMixerChannel(0);

	// update all mixer lines
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		updateMixerChannel(i);
	}

	updateMaxChannelSelector();
}


// update the and max. channel number for every instrument
void MixerView::updateMaxChannelSelector()
{
	const TrackContainer::TrackList& songTracks = Engine::getSong()->tracks();
	const TrackContainer::TrackList& patternStoreTracks = Engine::patternStore()->tracks();

	for (const auto& trackList : {songTracks, patternStoreTracks})
	{
		for (const auto& track : trackList)
		{
			if (track->type() == Track::Type::Instrument)
			{
				auto inst = (InstrumentTrack*)track;
				inst->mixerChannelModel()->setRange(0,
					m_mixerChannelViews.size()-1,1);
			}
			else if (track->type() == Track::Type::Sample)
			{
				auto strk = (SampleTrack*)track;
				strk->mixerChannelModel()->setRange(0,
					m_mixerChannelViews.size()-1,1);
			}
		}
	}
}


void MixerView::saveSettings(QDomDocument& doc, QDomElement& domElement)
{
	MainWindow::saveWidgetState(this, domElement);
}




void MixerView::loadSettings(const QDomElement& domElement)
{
	MainWindow::restoreWidgetState(this, domElement);
}





void MixerView::toggledSolo()
{
	getMixer()->toggledSolo();

	updateAllMixerChannels();
}


void MixerView::toggledMute()
{
	updateAllMixerChannels();
}

Mixer* MixerView::getMixer() const
{
	return m_mixer;
}

void MixerView::updateAllMixerChannels()
{
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i]->update();
	}
}

void MixerView::connectToSoloAndMute(int channelIndex)
{
	auto * mixerChannel = getMixer()->mixerChannel(channelIndex);

	connect(&mixerChannel->m_muteModel, &BoolModel::dataChanged, this, &MixerView::toggledMute, Qt::DirectConnection);
	connect(&mixerChannel->m_soloModel, &BoolModel::dataChanged, this, &MixerView::toggledSolo, Qt::DirectConnection);
}

void MixerView::disconnectFromSoloAndMute(int channelIndex)
{
	auto * mixerChannel = getMixer()->mixerChannel(channelIndex);

	disconnect(&mixerChannel->m_muteModel, &BoolModel::dataChanged, this, &MixerView::toggledMute);
	disconnect(&mixerChannel->m_soloModel, &BoolModel::dataChanged, this, &MixerView::toggledSolo);
}


void MixerView::setCurrentMixerChannel(MixerChannelView* channel)
{
	// select
	m_currentMixerChannel = channel;
	m_racksLayout->setCurrentWidget(m_mixerChannelViews[channel->channelIndex()]->m_effectRackView);

	// route the selected channel's audio through the single monitor chain
	getMixer()->setMonitorChannelIndex(channel->channelIndex());

	if (m_currentMeter != nullptr)
	{
		m_currentMeter->setChannelName(getMixer()->mixerChannel(channel->channelIndex())->m_name);
		// a real channel is now selected, so the monitor chain is no longer active
		m_currentMeter->setSelected(false);
	}

	// set up send knob
	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		updateMixerChannel(i);
	}
}


void MixerView::updateMixerChannel(int index)
{
	const auto mixer = getMixer();

	const auto currentIndex = m_currentMixerChannel->channelIndex();
	const auto thisLine = m_mixerChannelViews[index];
	thisLine->setToolTip(getMixer()->mixerChannel(index)->m_name);

	const auto sendModelCurrentToThis = mixer->channelSendModel(currentIndex, index);
	if (sendModelCurrentToThis == nullptr)
	{
		thisLine->m_sendKnob->setVisible(false);
		thisLine->m_sendArrow->setVisible(false);
	}
	else
	{
		thisLine->m_sendKnob->setVisible(true);
		thisLine->m_sendKnob->setModel(sendModelCurrentToThis);
		thisLine->m_sendArrow->setVisible(true);
	}

	const auto sendModelThisToCurrent = mixer->channelSendModel(index, currentIndex);
	if (sendModelThisToCurrent)
	{
		thisLine->m_receiveArrowOrSendButton->setVisible(true);
		thisLine->m_receiveArrowOrSendButton->setCurrentIndex(thisLine->m_receiveArrowStackedIndex);
	}
	else
	{
		thisLine->m_receiveArrowOrSendButton->setVisible(!mixer->isInfiniteLoop(currentIndex, index));
		thisLine->m_receiveArrowOrSendButton->setCurrentIndex(thisLine->m_sendButtonStackedIndex);
	}

	thisLine->m_sendButton->updateLightStatus();
	thisLine->m_renameLineEdit->setText(thisLine->elideName(thisLine->mixerChannel()->m_name));
	thisLine->update();
}


void MixerView::deleteChannel(int index)
{
	// can't delete master
	if (index == 0) return;

	// Disconnect from the solo/mute models of the channel we are about to delete
	disconnectFromSoloAndMute(index);

	// remember selected line
	int selLine = m_currentMixerChannel->channelIndex();

	Mixer* mixer = getMixer();
	// in case the deleted channel is soloed or the remaining
	// channels will be left in a muted state
	mixer->clearChannel(index);

	// delete the real channel
	mixer->deleteChannel(index);

	chLayout->removeWidget(m_mixerChannelViews[index]);
	m_racksLayout->removeWidget(m_mixerChannelViews[index]);
	// delete MixerChannelView later to prevent a crash when deleting from context menu
	m_mixerChannelViews[index]->hide();
	m_mixerChannelViews[index]->deleteLater();
	m_channelAreaWidget->adjustSize();

	// make sure every channel knows what index it is
	for (int i = index + 1; i < m_mixerChannelViews.size(); ++i)
	{
		m_mixerChannelViews[i]->setChannelIndex(i - 1);
	}
	m_mixerChannelViews.remove(index);

	// select the next channel
	if (selLine >= m_mixerChannelViews.size())
	{
		selLine = m_mixerChannelViews.size() - 1;
	}
	setCurrentMixerChannel(selLine);

	updateMaxChannelSelector();
}

void MixerView::deleteUnusedChannels()
{
	Mixer* mix = getMixer();

	// Check all channels except master, delete those with no incoming sends
	for (int i = m_mixerChannelViews.size() - 1; i > 0; --i)
	{
		if (!mix->isChannelInUse(i))
		{
			deleteChannel(i);
		}
	}
}

void MixerView::moveChannelLeft(int index)
{
	// can't move master or first channel left or last channel right
	if (index <= 1 || index >= m_mixerChannelViews.size()) { return; }

	m_mixer->moveChannelLeft(index);

	const auto layoutIndex = chLayout->indexOf(m_mixerChannelViews[index]);
	assert(layoutIndex >= 1);

	chLayout->removeWidget(m_mixerChannelViews[index]);
	chLayout->insertWidget(layoutIndex - 1, m_mixerChannelViews[index]);

	m_mixerChannelViews[index]->setChannelIndex(index - 1);
	m_mixerChannelViews[index - 1]->setChannelIndex(index);
	std::swap(m_mixerChannelViews[index - 1], m_mixerChannelViews[index]);
}



void MixerView::moveChannelRight(int index)
{
	moveChannelLeft(index + 1);
}


void MixerView::renameChannel(int index)
{
	m_mixerChannelViews[index]->renameChannel();
}



void MixerView::keyPressEvent(QKeyEvent * e)
{
	auto adjustCurrentFader = [this](const Qt::KeyboardModifiers& modifiers, Fader::AdjustmentDirection direction)
	{
		auto* mixerChannel = currentMixerChannel();

		if (mixerChannel)
		{
			mixerChannel->fader()->adjust(modifiers, direction);
		}
	};

	switch(e->key())
	{
		case Qt::Key_Delete:
			deleteChannel(m_currentMixerChannel->channelIndex());
			break;
		case Qt::Key_Left:
			if (e->modifiers() & Qt::AltModifier)
			{
				moveChannelLeft(m_currentMixerChannel->channelIndex());
			}
			else
			{
				// select channel to the left
				setCurrentMixerChannel(m_currentMixerChannel->channelIndex() - 1);
			}
			break;
		case Qt::Key_Right:
			if (e->modifiers() & Qt::AltModifier)
			{
				moveChannelRight(m_currentMixerChannel->channelIndex());
			}
			else
			{
				// select channel to the right
				setCurrentMixerChannel(m_currentMixerChannel->channelIndex() + 1);
			}
			break;
		case Qt::Key_Up:
		case Qt::Key_Plus:
			adjustCurrentFader(e->modifiers(), Fader::AdjustmentDirection::Up);
			break;
		case Qt::Key_Down:
		case Qt::Key_Minus:
			adjustCurrentFader(e->modifiers(), Fader::AdjustmentDirection::Down);
			break;
		case Qt::Key_Insert:
			if (e->modifiers() & Qt::ShiftModifier)
			{
				addNewChannel();
			}
			break;
		case Qt::Key_Enter:
		case Qt::Key_Return:
		case Qt::Key_F2:
			renameChannel(m_currentMixerChannel->channelIndex());
			break;
		default:
			e->ignore();
			break;
	}
}



void MixerView::setCurrentMixerChannel(int channel)
{
	if (channel >= 0 && channel < m_mixerChannelViews.size())
	{
		setCurrentMixerChannel(m_mixerChannelViews[channel]);
	}
}



void MixerView::clear()
{
	for (auto i = m_mixerChannelViews.size() - 1; i > 0; --i) { deleteChannel(i); }
	getMixer()->clearChannel(0);

	m_mixerChannelViews[0]->reset();

	refreshDisplay();
}




void MixerView::updateFaders()
{
	Mixer * m = getMixer();

	for (int i = 0; i < m_mixerChannelViews.size(); ++i)
	{
		const float opl = m_mixerChannelViews[i]->m_fader->getPeak_L();
		const float opr = m_mixerChannelViews[i]->m_fader->getPeak_R();
		const float fallOff = 1.25;
		if (m->mixerChannel(i)->m_peakLeft >= opl/fallOff)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_L(m->mixerChannel(i)->m_peakLeft);
			// Set to -1 so later we'll know if this value has been refreshed yet.
			m->mixerChannel(i)->m_peakLeft = -1;
		}
		else if (m->mixerChannel(i)->m_peakLeft != -1)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_L(opl/fallOff);
		}

		if (m->mixerChannel(i)->m_peakRight >= opr/fallOff)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_R(m->mixerChannel(i)->m_peakRight);
			// Set to -1 so later we'll know if this value has been refreshed yet.
			m->mixerChannel(i)->m_peakRight = -1;
		}
		else if (m->mixerChannel(i)->m_peakRight != -1)
		{
			m_mixerChannelViews[i]->m_fader->setPeak_R(opr/fallOff);
		}
	}

	// mirror the selected channel's (already smoothed) peak onto the left meter
	if (m_currentMeter != nullptr && m_currentMixerChannel != nullptr)
	{
		auto* fader = m_currentMixerChannel->m_fader;
		m_currentMeter->setPeaks(fader->getPeak_L(), fader->getPeak_R());
	}
}


} // namespace lmms::gui
