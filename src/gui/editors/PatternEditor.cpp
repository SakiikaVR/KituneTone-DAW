/*
 * PatternEditor.cpp - basic main-window for editing patterns
 *
 * Copyright (c) 2004-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "PatternEditor.h"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QLabel>
#include <QSpinBox>
#include <QSlider>

#include <algorithm>
#include <set>
#include <QVBoxLayout>

#include "ActionGroup.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "ClipView.h"
#include "Clipboard.h"
#include "ComboBox.h"
#include "DataFile.h"
#include "embed.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectJournal.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "TimeLineWidget.h"
#include "TrackView.h"

#include "MidiClip.h"


namespace lmms::gui
{


PatternEditor::PatternEditor(PatternStore* ps) :
	TrackContainerView(ps),
	m_ps(ps),
	m_trackHeadWidth(ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt() == 1
		? DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + TRACK_OP_WIDTH_COMPACT
		: DEFAULT_SETTINGS_WIDGET_WIDTH + TRACK_OP_WIDTH),
	m_maxClipLength(TimePos::ticksPerBar())
{
	setModel(ps);

	m_timeLine = new TimeLineWidget(m_trackHeadWidth, 32, pixelsPerBar(),
		Engine::getSong()->getTimeline(Song::PlayMode::Pattern),
		m_currentPosition, this
	);
	m_timeLine->setLoopControlsEnabled(false);
	m_timeLine->timeline()->setLoopEnabled(false);
	connect(this, &TrackContainerView::positionChanged,
			m_timeLine, qOverload<>(&QWidget::update));
	connect(m_timeLine->timeline(), &Timeline::positionChanged, this, &PatternEditor::updatePosition);
	connect(getGUI()->songEditor()->m_editor->snappingModel(), &Model::dataChanged,
			this, [this]() {
				m_timeLine->setSnapSize(getGUI()->songEditor()->m_editor->getSnapSize());
			});
	m_timeLine->setSnapSize(getGUI()->songEditor()->m_editor->getSnapSize());
	static_cast<QVBoxLayout*>(layout())->insertWidget(0, m_timeLine);
	m_leftRightScroll = new QScrollBar(Qt::Horizontal, this);
	m_leftRightScroll->setMinimum(0);
	m_leftRightScroll->setMaximum(0);
	m_leftRightScroll->setSingleStep(1);
	m_leftRightScroll->setPageStep(4 * TimePos::ticksPerBar());
	static_cast<QVBoxLayout*>(layout())->addWidget(m_leftRightScroll);
	connect(m_leftRightScroll, &QScrollBar::valueChanged, this, [this](int value) {
		m_currentPosition = TimePos(value);
		m_timeLine->setXOffset(m_trackHeadWidth);
		emit positionChanged(m_currentPosition);
	});

	connect(m_ps, &PatternStore::trackUpdated,
		this, &PatternEditor::updateMaxSteps);
	connect(this, &TrackContainerView::tracksRealigned,
			this, &PatternEditor::updateMaxSteps);

	setFocusPolicy(Qt::StrongFocus);
	setFocus();
	updateMaxSteps();
}




void PatternEditor::addSteps()
{
	makeSteps( false );
}

void PatternEditor::cloneSteps()
{
	makeSteps( true );
}




void PatternEditor::removeSteps()
{
	const TrackContainer::TrackList& tl = model()->tracks();

	for (const auto& track : tl)
	{
		if (track->type() == Track::Type::Instrument)
		{
			MidiClip* p = nullptr;
			for (Clip* clip : track->getClips())
			{
				if (clip->patternIndex() == m_ps->currentPattern())
				{
					p = static_cast<MidiClip*>(clip);
					break;
				}
			}
			if (!p)
			{
				p = static_cast<MidiClip*>(track->createClip(TimePos(0)));
				p->setPatternIndex(m_ps->currentPattern());
			}
			p->removeSteps();
		}
	}
	updateMaxSteps();
}




void PatternEditor::addSampleTrack()
{
	(void) Track::create( Track::Type::Sample, model() );
}




void PatternEditor::addAutomationTrack()
{
	(void) Track::create( Track::Type::Automation, model() );
}




void PatternEditor::removeViewsForPattern(int pattern)
{
	for( TrackView* view : trackViews() )
	{
		view->getTrackContentWidget()->removeClipViewsForPattern(pattern);
	}
}



void PatternEditor::saveSettings(QDomDocument& doc, QDomElement& element)
{
	MainWindow::saveWidgetState( parentWidget(), element );
}

void PatternEditor::loadSettings(const QDomElement& element)
{
	MainWindow::restoreWidgetState(parentWidget(), element);
	updateMaxSteps();
}




void PatternEditor::dropEvent(QDropEvent* de)
{
	QString type = StringPairDrag::decodeKey( de );
	QString value = StringPairDrag::decodeValue( de );

	if( type.left( 6 ) == "track_" )
	{
		DataFile dataFile( value.toUtf8() );
		Track * t = Track::create( dataFile.content().firstChild().toElement(), model() );
		if (t == nullptr)
		{
			de->ignore();
			return;
		}

		for (Clip* clip : t->getClips())
		{
			clip->setPatternIndex(m_ps->currentPattern());
		}
		m_ps->updateAfterTrackAdd();

		de->accept();
	}
	else
	{
		TrackContainerView::dropEvent( de );
	}
	updateMaxSteps();
}


void PatternEditor::resizeEvent(QResizeEvent* re)
{
	updatePixelsPerBar();
}


void PatternEditor::updatePosition()
{
	for (const auto& trackView : trackViews())
	{
		trackView->update();
	}
	emit positionChanged(m_currentPosition);
}

void PatternEditor::showCurrentPattern()
{
	selectAllClips(false);
	m_leftRightScroll->setValue(0);
	updatePosition();
	updateMaxSteps();
}

void PatternEditor::updatePixelsPerBar()
{
	m_timeLine->setPixelsPerBar(pixelsPerBar());
	m_timeLine->setXOffset(m_trackHeadWidth);
}


void PatternEditor::setZoom(int percent)
{
	const int clampedPercent = std::clamp(percent, 25, 400);
	if (m_zoomPercent == clampedPercent) { return; }
	m_zoomPercent = clampedPercent;
	setPixelsPerBar(std::clamp(128 * m_zoomPercent / 100, 16, 512));
	updatePixelsPerBar();
	updateMaxSteps();
	realignTracks();
	emit zoomChanged(m_zoomPercent);
}

void PatternEditor::wheelEvent(QWheelEvent* event)
{
	const int cursorX = event->position().toPoint().x();
	if (!(event->modifiers() & Qt::ControlModifier) || cursorX <= m_trackHeadWidth)
	{
		event->ignore();
		return;
	}

	const int timelineX = cursorX - m_trackHeadWidth;
	const tick_t anchorTick = m_leftRightScroll->value()
			+ static_cast<tick_t>(timelineX * TimePos::ticksPerBar() / pixelsPerBar());
	const int delta = event->angleDelta().y() + event->angleDelta().x();
	if (delta == 0)
	{
		event->ignore();
		return;
	}

	const int step = event->modifiers() & Qt::ShiftModifier ? 1 : 5;
	const int requestedZoom = std::clamp(
			m_zoomPercent + (delta > 0 ? step : -step), 25, 400);
	if (requestedZoom == m_zoomPercent)
	{
		event->accept();
		return;
	}
	setZoom(requestedZoom);
	const tick_t newScroll = anchorTick
			- static_cast<tick_t>(timelineX * TimePos::ticksPerBar() / pixelsPerBar());
	m_leftRightScroll->setValue(std::max<tick_t>(0, newScroll));
	event->accept();
}


void PatternEditor::mousePressEvent(QMouseEvent* event)
{
	if (allowRubberband())
	{
		m_selectionOrigin = contentWidget()->mapFrom(this, event->position().toPoint());
		m_selectionMouse = m_selectionOrigin;
		rubberBand()->setEnabled(true);
		rubberBand()->setGeometry(QRect(m_selectionOrigin, QSize()));
		rubberBand()->show();
	}
	QWidget::mousePressEvent(event);
}


void PatternEditor::mouseMoveEvent(QMouseEvent* event)
{
	if (rubberBandActive())
	{
		m_selectionMouse = contentWidget()->mapFrom(this, event->position().toPoint());
		const QRect selection(m_selectionOrigin, m_selectionMouse);
		rubberBand()->setGeometry(selection.normalized());
		for (selectableObject* object : findChildren<selectableObject*>())
		{
			auto* clipView = dynamic_cast<ClipView*>(object);
			if (!clipView || clipView->getClip()->patternIndex() != m_ps->currentPattern())
			{
				continue;
			}
			const QRect clipRect(
					contentWidget()->mapFromGlobal(clipView->mapToGlobal(QPoint(0, 0))),
					clipView->size());
			object->setSelected(selection.normalized().intersects(clipRect));
		}
	}
	QWidget::mouseMoveEvent(event);
}


void PatternEditor::mouseReleaseEvent(QMouseEvent* event)
{
	rubberBand()->hide();
	rubberBand()->setEnabled(false);
	QWidget::mouseReleaseEvent(event);
}


void PatternEditor::copySelectedClips()
{
	QVector<ClipView*> clips;
	ClipView* leftmost = nullptr;
	for (selectableObject* object : selectedObjects())
	{
		auto* view = dynamic_cast<ClipView*>(object);
		if (!view) { continue; }
		clips.push_back(view);
		if (!leftmost || view->getClip()->startPosition()
				< leftmost->getClip()->startPosition())
		{
			leftmost = view;
		}
	}
	if (leftmost) { leftmost->copy(clips); }
}


void PatternEditor::selectAllClips(bool select)
{
	for (selectableObject* object : rubberBand()->selectableObjects())
	{
		auto* view = dynamic_cast<ClipView*>(object);
		if (!select || (view && view->getClip()->patternIndex() == m_ps->currentPattern()))
		{
			object->setSelected(select);
		}
	}
}


void PatternEditor::pasteSelectedClips()
{
	using namespace Clipboard;
	const QMimeData* mime = getMimeData();
	if (!decodeKey(mime).startsWith("clip_")) { return; }
	DataFile dataFile(decodeValue(mime).toUtf8());
	const auto nodes = dataFile.content().firstChildElement("clips").childNodes();
	const TimePos anchor = dataFile.content().firstChildElement("copyMetadata")
			.attribute("grabbedClipPos").toInt();
	const TimePos playhead = TimePos(Engine::getSong()
			->getPlayPos(Song::PlayMode::Pattern).getTicks())
			.quantize(getGUI()->songEditor()->m_editor->getSnapSize());
	const TimePos offset = playhead - anchor;
	const auto& tracks = m_ps->tracks();
	selectAllClips(false);

	std::set<Track*> targetTracks;
	for (int i = 0; i < nodes.length(); ++i)
	{
		const QDomElement outer = nodes.item(i).toElement();
		const int index = outer.attribute("trackIndex").toInt();
		if (index < 0 || index >= static_cast<int>(tracks.size())) { continue; }
		const auto sourceType = static_cast<Track::Type>(
				outer.attribute("trackType", "-1").toInt());
		if (tracks.at(index)->type() == sourceType) { targetTracks.insert(tracks.at(index)); }
	}
	auto* journal = Engine::projectJournal();
	const bool wasJournalling = journal->isJournalling();
	for (Track* track : targetTracks) { track->addJournalCheckPoint(); }
	journal->setJournalling(false);

	Engine::audioEngine()->requestChangeInModel();
	for (int i = 0; i < nodes.length(); ++i)
	{
		const QDomElement outer = nodes.item(i).toElement();
		const int index = outer.attribute("trackIndex").toInt();
		if (index < 0 || index >= static_cast<int>(tracks.size())) { continue; }
		const auto sourceType = static_cast<Track::Type>(
				outer.attribute("trackType", "-1").toInt());
		if (tracks.at(index)->type() != sourceType) { continue; }
		const QDomElement saved = outer.firstChildElement();
		TimePos position = TimePos(saved.attribute("pos").toInt()) + offset;
		if (position < 0) { position = 0; }
		Clip* clip = tracks.at(index)->createClip(position);
		clip->restoreState(saved);
		clip->movePosition(position);
		clip->setPatternIndex(m_ps->currentPattern());
		clip->selectViewOnCreate(true);
	}
	AutomationClip::resolveAllIDs();
	Engine::audioEngine()->doneChangeInModel();
	journal->setJournalling(wasJournalling);
	updateMaxSteps();
}


void PatternEditor::deleteSelectedClips()
{
	const auto objects = selectedObjects();
	std::set<Track*> tracks;
	QVector<ClipView*> clips;
	for (selectableObject* object : objects)
	{
		if (auto* view = dynamic_cast<ClipView*>(object))
		{
			clips.push_back(view);
			tracks.insert(view->getClip()->getTrack());
		}
	}
	if (clips.isEmpty()) { return; }
	auto* journal = Engine::projectJournal();
	const bool journalling = journal->isJournalling();
	for (Track* track : tracks) { track->addJournalCheckPoint(); }
	journal->setJournalling(false);
	for (ClipView* clip : clips) { clip->remove(); }
	journal->setJournalling(journalling);
	updateMaxSteps();
}


void PatternEditor::duplicateSelectedClips()
{
	QVector<ClipView*> views;
	TimePos first = -1;
	TimePos last = 0;
	for (selectableObject* object : selectedObjects())
	{
		auto* view = dynamic_cast<ClipView*>(object);
		if (!view) { continue; }
		views.push_back(view);
		if (first < 0 || view->getClip()->startPosition() < first)
		{
			first = view->getClip()->startPosition();
		}
		last = std::max(last, view->getClip()->endPosition());
	}
	if (views.isEmpty() || last <= first) { return; }
	const TimePos shift = last - first;
	selectAllClips(false);
	Engine::audioEngine()->requestChangeInModel();
	for (ClipView* view : views)
	{
		Clip* source = view->getClip();
		Clip* destination = source->getTrack()->createClip(source->startPosition() + shift);
		Clip::copyStateTo(source, destination);
		destination->movePosition(source->startPosition() + shift);
		destination->setPatternIndex(m_ps->currentPattern());
		destination->selectViewOnCreate(true);
	}
	Engine::audioEngine()->doneChangeInModel();
	updateMaxSteps();
}


void PatternEditor::keyPressEvent(QKeyEvent* event)
{
	const bool control = event->modifiers() & Qt::ControlModifier;
	if (control && event->key() == Qt::Key_A) { selectAllClips(true); }
	else if (control && event->key() == Qt::Key_C) { copySelectedClips(); }
	else if (control && event->key() == Qt::Key_V) { pasteSelectedClips(); }
	else if (control && event->key() == Qt::Key_B) { duplicateSelectedClips(); }
	else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
	{
		deleteSelectedClips();
	}
	else if (event->key() == Qt::Key_Escape) { selectAllClips(false); }
	else { QWidget::keyPressEvent(event); }
}

void PatternEditor::updateMaxSteps()
{
	const TrackContainer::TrackList& tl = model()->tracks();

	const auto patternLength = Engine::patternStore()->lengthOfCurrentPattern()
			* TimePos::ticksPerBar();
	m_maxClipLength = patternLength;
	m_timeLine->setRangeHighlight(TimePos{0}, TimePos{patternLength});
	for (const auto& track : tl)
	{
		for (Clip* clip : track->getClips())
		{
			if (clip->patternIndex() != m_ps->currentPattern()) { continue; }
			m_maxClipLength = std::max(m_maxClipLength,
					static_cast<tick_t>(clip->startPosition().getTicks()
							+ clip->length().getTicks()));
		}
	}
	updatePixelsPerBar();
	m_leftRightScroll->setMaximum(std::max<tick_t>(TimePos::ticksPerBar(), m_maxClipLength));
}


void PatternEditor::makeSteps( bool clone )
{
	const TrackContainer::TrackList& tl = model()->tracks();

	for (const auto& track : tl)
	{
		if (track->type() == Track::Type::Instrument)
		{
			MidiClip* p = nullptr;
			for (Clip* clip : track->getClips())
			{
				if (clip->patternIndex() == m_ps->currentPattern())
				{
					p = static_cast<MidiClip*>(clip);
					break;
				}
			}
			if (!p)
			{
				p = static_cast<MidiClip*>(track->createClip(TimePos(0)));
				p->setPatternIndex(m_ps->currentPattern());
			}
			if( clone )
			{
				p->cloneSteps();
			} else
			{
				p->addSteps();
			}
		}
	}
	updateMaxSteps();
}

// Creates a clone of the current pattern track with the same content, but no clips in the song editor
// TODO: Avoid repeated code from cloneTrack and clearTrack in TrackOperationsWidget somehow
void PatternEditor::cloneClip()
{
	// Get the current PatternTrack id
	auto ps = static_cast<PatternStore*>(model());
	const int currentPattern = ps->currentPattern();

	PatternTrack* pt = PatternTrack::findPatternTrack(currentPattern);

	if (pt)
	{
		// Clone the track
		Track* newTrack = pt->clone();
		ps->setCurrentPattern(static_cast<PatternTrack*>(newTrack)->patternIndex());

		// Track still have the clips which is undesirable in this case, clear the track
		newTrack->lock();
		newTrack->deleteClips();
		newTrack->unlock();
	}
}




PatternEditorWindow::PatternEditorWindow(PatternStore* ps) :
	Editor(false),
	m_editor(new PatternEditor(ps))
{
	setWindowIcon(embed::getIconPixmap("pattern_track_btn"));
	setWindowTitle(tr("パターンエディター"));
	setCentralWidget(m_editor);

	setAcceptDrops(true);
	m_toolBar->setAcceptDrops(true);
	connect(m_toolBar, SIGNAL(dragEntered(QDragEnterEvent*)), m_editor, SLOT(dragEnterEvent(QDragEnterEvent*)));
	connect(m_toolBar, SIGNAL(dropped(QDropEvent*)), m_editor, SLOT(dropEvent(QDropEvent*)));

	// TODO: Use style sheet
	if (ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt())
	{
		setMinimumWidth(TRACK_OP_WIDTH_COMPACT + DEFAULT_SETTINGS_WIDGET_WIDTH_COMPACT + 2 * ClipView::BORDER_WIDTH + 384);
	}
	else
	{
		setMinimumWidth(TRACK_OP_WIDTH + DEFAULT_SETTINGS_WIDGET_WIDTH + 2 * ClipView::BORDER_WIDTH + 384);
	}

	m_playAction->setToolTip(tr("現在のパターンを再生／一時停止（Space）"));
	m_stopAction->setToolTip(tr("パターンの再生を停止（Space）"));


	// Pattern selector
	DropToolBar* patternSelectionToolBar = addDropToolBarToTop(tr("パターン選択"));

	m_patternComboBox = new ComboBox(m_toolBar);
	m_patternComboBox->setFixedSize(200, ComboBox::DEFAULT_HEIGHT);
	m_patternComboBox->setModel(&ps->m_patternComboBoxModel);

	patternSelectionToolBar->addWidget(m_patternComboBox);
	patternSelectionToolBar->addSeparator();
	auto* lengthLabel = new QLabel(tr("小節数:"), m_toolBar);
	patternSelectionToolBar->addWidget(lengthLabel);
	m_patternLengthSpinBox = new QSpinBox(m_toolBar);
	m_patternLengthSpinBox->setRange(1, 999);
	m_patternLengthSpinBox->setSuffix(tr(" 小節"));
	m_patternLengthSpinBox->setToolTip(tr("パターンの再生範囲と標準クリップ長"));
	patternSelectionToolBar->addWidget(m_patternLengthSpinBox);
	connect(m_patternLengthSpinBox, qOverload<int>(&QSpinBox::valueChanged),
			this, &PatternEditorWindow::setPatternLength);

	// Song Editorと同じ三つの編集モードを使う。
	auto* editToolBar = addDropToolBarToTop(tr("編集モード"));
	auto* editModes = new ActionGroup(this);
	auto* drawAction = editModes->addAction(embed::getIconPixmap("edit_draw"), tr("描画モード"));
	auto* knifeAction = editModes->addAction(embed::getIconPixmap("edit_knife"), tr("分割モード"));
	auto* selectAction = editModes->addAction(embed::getIconPixmap("edit_select"), tr("選択・移動モード"));
	drawAction->setChecked(true);
	connect(drawAction, &QAction::triggered, m_editor, &PatternEditor::setEditModeDraw);
	connect(knifeAction, &QAction::triggered, m_editor, &PatternEditor::setEditModeKnife);
	connect(selectAction, &QAction::triggered, m_editor, &PatternEditor::setEditModeSelect);
	editToolBar->addAction(drawAction);
	editToolBar->addAction(knifeAction);
	editToolBar->addAction(selectAction);

	auto* timelineToolBar = addDropToolBarToTop(tr("タイムライン"));
	m_editor->timeLine()->addToolButtons(timelineToolBar);

	auto* zoomToolBar = addDropToolBarToTop(tr("ズーム"));
	auto* zoomLabel = new QLabel(tr("ズーム:"), m_toolBar);
	zoomToolBar->addWidget(zoomLabel);
	auto* zoomSlider = new QSlider(Qt::Horizontal, m_toolBar);
	zoomSlider->setRange(25, 400);
	zoomSlider->setValue(100);
	zoomSlider->setFixedWidth(110);
	zoomSlider->setToolTip(tr("タイムラインの横ズーム"));
	zoomToolBar->addWidget(zoomSlider);
	connect(zoomSlider, &QSlider::valueChanged, m_editor, &PatternEditor::setZoom);
	connect(m_editor, &PatternEditor::zoomChanged, zoomSlider, &QSlider::setValue);

	auto* snapToolBar = addDropToolBarToTop(tr("スナップ"));
	auto* snapLabel = new QLabel(tr("スナップ:"), m_toolBar);
	snapToolBar->addWidget(snapLabel);
	auto* snapCombo = new ComboBox(m_toolBar);
	snapCombo->setFixedSize(90, ComboBox::DEFAULT_HEIGHT);
	snapCombo->setModel(getGUI()->songEditor()->m_editor->snappingModel());
	snapCombo->setToolTip(tr("クリップの移動・サイズ変更・分割の刻み"));
	snapToolBar->addWidget(snapCombo);


	// Track actions
	DropToolBar *trackAndStepActionsToolBar = addDropToolBarToTop(tr("トラック操作"));


	trackAndStepActionsToolBar->addAction(embed::getIconPixmap("add_pattern_track"), tr("新しいパターン"),
						Engine::getSong(), SLOT(addPatternTrack()));
	trackAndStepActionsToolBar->addAction(embed::getIconPixmap("clone_pattern_track_clip"), tr("パターンを複製"),
						m_editor, SLOT(cloneClip()));
	trackAndStepActionsToolBar->addAction(embed::getIconPixmap("add_sample_track"),	tr("サンプルトラックを追加"),
						m_editor, SLOT(addSampleTrack()));
	trackAndStepActionsToolBar->addAction(embed::getIconPixmap("add_automation"), tr("オートメーショントラックを追加"),
						m_editor, SLOT(addAutomationTrack()));

	auto stretch = new QWidget(m_toolBar);
	stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	trackAndStepActionsToolBar->addWidget(stretch);


	connect(&ps->m_patternComboBoxModel, &ComboBoxModel::dataChanged,
			m_editor, &PatternEditor::showCurrentPattern);
	connect(&ps->m_patternComboBoxModel, &ComboBoxModel::dataChanged,
			this, &PatternEditorWindow::syncPatternLength);
	// The combo's value does not change when the first pattern track appears
	// (or when tracks are renamed/removed), so dataChanged alone leaves the
	// spin box at its default while the pattern is really 4 bars long. Track
	// list changes arrive as propertiesChanged, length changes from other
	// sources (project load, undo) as trackUpdated.
	connect(&ps->m_patternComboBoxModel, &ComboBoxModel::propertiesChanged,
			this, &PatternEditorWindow::syncPatternLength);
	connect(ps, &PatternStore::trackUpdated,
			this, &PatternEditorWindow::syncPatternLength);
	syncPatternLength();

	auto viewNext = new QAction(this);
	connect(viewNext, SIGNAL(triggered()), m_patternComboBox, SLOT(selectNext()));
	viewNext->setShortcut(Qt::Key_Plus);
	addAction(viewNext);

	auto viewPrevious = new QAction(this);
	connect(viewPrevious, SIGNAL(triggered()), m_patternComboBox, SLOT(selectPrevious()));
	viewPrevious->setShortcut(Qt::Key_Minus);
	addAction(viewPrevious);
}


QSize PatternEditorWindow::sizeHint() const
{
	return {minimumWidth() + 10, 300};
}


void PatternEditorWindow::play()
{
	if (Engine::getSong()->playMode() != Song::PlayMode::Pattern)
	{
		Engine::getSong()->playPattern();
	}
	else
	{
		Engine::getSong()->togglePause();
	}
}


void PatternEditorWindow::stop()
{
	Engine::getSong()->stop();
}


void PatternEditorWindow::syncPatternLength()
{
	auto* track = PatternTrack::findPatternTrack(
			Engine::patternStore()->currentPattern());
	if (!track) { return; }
	const QSignalBlocker blocker(m_patternLengthSpinBox);
	m_patternLengthSpinBox->setValue(track->patternLengthBars());
}


void PatternEditorWindow::setPatternLength(int bars)
{
	auto* track = PatternTrack::findPatternTrack(
			Engine::patternStore()->currentPattern());
	if (!track) { return; }
	track->setPatternLengthBars(bars);
	m_editor->updateMaxSteps();
	Engine::getSong()->setModified();
}


} // namespace lmms::gui
