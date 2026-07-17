/*
 * TrackContainerView.cpp - view-component for TrackContainer
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "TrackContainerView.h"


#include <QFileInfo>
#include "SampleClip.h"
#include "SampleTrack.h"
#include <QLayout>
#include <QMessageBox>
#include <QMimeData>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#include <set>
#include <vector>

#include "TrackContainer.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "DataFile.h"
#include "MainWindow.h"
#include "FileBrowser.h"
#include "ImportFilter.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "Song.h"
#include "StringPairDrag.h"
#include "TrackView.h"
#include "GuiApplication.h"
#include "PluginFactory.h"

namespace lmms
{

using namespace std;


InstrumentLoaderThread::InstrumentLoaderThread( QObject *parent, InstrumentTrack *it, QString name ) :
	QThread( parent ),
	m_it( it ),
	m_name( name )
{
	m_containerThread = thread();
}




void InstrumentLoaderThread::run()
{
	Instrument *i = m_it->loadInstrument(m_name, nullptr,
										 true /*always DnD*/);
	QObject *parent = i->parent();
	i->setParent( 0 );
	i->moveToThread( m_containerThread );
	i->setParent( parent );
}

namespace gui
{

TrackContainerView::TrackContainerView( TrackContainer * _tc ) :
	QWidget(),
	ModelView( nullptr, this ),
	JournallingObject(),
	SerializingObjectHook(),
	m_currentPosition( 0, 0 ),
	m_tc( _tc ),
	m_trackViews(),
	m_scrollArea( new scrollArea( this ) ),
	m_ppb( DEFAULT_PIXELS_PER_BAR ),
	m_rubberBand( new RubberBand( m_scrollArea ) )
{
	m_tc->setHook( this );
	//keeps the direction of the widget, undepended on the locale
	setLayoutDirection( Qt::LeftToRight );

	// The main layout - by default it only contains the scroll area,
	// but SongEditor uses the layout to add a TimeLineWidget on top
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing( 0 );
	layout->addWidget( m_scrollArea );

	// The widget that will contain all TrackViews
	auto scrollContent = new QWidget;
	m_scrollLayout = new QVBoxLayout( scrollContent );
	m_scrollLayout->setContentsMargins(0, 0, 0, 0);
	m_scrollLayout->setSpacing( 0 );
	m_scrollLayout->setSizeConstraint( QLayout::SetMinAndMaxSize );

	m_scrollArea->setWidget( scrollContent );
	m_scrollArea->setWidgetResizable(true);

	m_scrollArea->show();
	m_rubberBand->hide();
	m_rubberBand->setEnabled( false );

	setAcceptDrops( true );

	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
						this, SLOT(realignTracks()));
	connect( m_tc, SIGNAL(trackAdded(lmms::Track*)),
			this, SLOT(createTrackView(lmms::Track*)),
			Qt::QueuedConnection );
}




TrackContainerView::~TrackContainerView()
{
	while( !m_trackViews.empty() )
	{
		delete m_trackViews.takeLast();
	}
}





void TrackContainerView::saveSettings( QDomDocument & _doc,
							QDomElement & _this )
{
	MainWindow::saveWidgetState( this, _this );
}




void TrackContainerView::loadSettings( const QDomElement & _this )
{
	MainWindow::restoreWidgetState( this, _this );
}




TrackView * TrackContainerView::addTrackView( TrackView * _tv )
{
	m_trackViews.push_back( _tv );
	m_scrollLayout->addWidget( _tv );
	connect( this, SIGNAL( positionChanged( const lmms::TimePos& ) ),
				_tv->getTrackContentWidget(),
				SLOT( changePosition( const lmms::TimePos& ) ) );
	realignTracks();
	return( _tv );
}




void TrackContainerView::removeTrackView( TrackView * _tv )
{
	int index = m_trackViews.indexOf( _tv );
	if( index != -1 )
	{
		m_trackViews.removeAt( index );

		disconnect( _tv );
		m_scrollLayout->removeWidget( _tv );

		realignTracks();
		if( Engine::getSong() )
		{
			Engine::getSong()->setModified();
		}
	}
}




void TrackContainerView::moveTrackView( TrackView * trackView, int indexTo )
{
	// Can't move out of bounds
	if ( indexTo >= m_trackViews.size() || indexTo < 0 ) { return; }

	// Does not need to move to itself
	int indexFrom = m_trackViews.indexOf( trackView );
	if ( indexFrom == indexTo ) { return; }

	PatternTrack::swapPatternTracks( trackView->getTrack(),
			m_trackViews[indexTo]->getTrack() );

	m_tc->moveTrack(trackView->getTrack(), indexTo);

	m_scrollLayout->removeWidget( trackView );
	m_scrollLayout->insertWidget( indexTo, trackView );

	m_trackViews.move( indexFrom, indexTo );

	realignTracks();
}




void TrackContainerView::moveTrackViewUp( TrackView * trackView )
{
	int index = m_trackViews.indexOf( trackView );

	moveTrackView( trackView, index - 1 );
}




void TrackContainerView::moveTrackViewDown( TrackView * trackView )
{
	int index = m_trackViews.indexOf( trackView );

	moveTrackView( trackView, index + 1 );
}

void TrackContainerView::scrollToTrackView( TrackView * _tv )
{
	if (!m_trackViews.contains(_tv))
	{
		qWarning("TrackContainerView::scrollToTrackView: TrackView is not owned by this");
	}
	else
	{
		int currentScrollTop = m_scrollArea->verticalScrollBar()->value();
		int scrollAreaHeight = m_scrollArea->size().height();
		int trackViewTop = _tv->pos().y();
		int trackViewBottom = trackViewTop + _tv->size().height();

		// displayed_location = widget_location - currentScrollTop
		// want to make sure that the widget top has displayed location > 0,
		// and widget bottom < scrollAreaHeight
		// trackViewTop - scrollY > 0 && trackViewBottom - scrollY < scrollAreaHeight
		// therefore scrollY < trackViewTop && scrollY > trackViewBottom - scrollAreaHeight
		int newScroll = std::max( trackViewBottom-scrollAreaHeight, std::min(currentScrollTop, trackViewTop) );
		m_scrollArea->verticalScrollBar()->setValue(newScroll);
	}
}




void TrackContainerView::realignTracks()
{
	for (const auto& trackView : m_trackViews)
	{
		trackView->show();
		trackView->update();
	}

	emit tracksRealigned();
}




TrackView * TrackContainerView::createTrackView( Track * _t )
{
	//m_tc->addJournalCheckPoint();

	// Avoid duplicating track views
	for (const auto& trackView : m_trackViews)
	{
		if (trackView->getTrack() == _t) { return trackView; }
	}

	return _t->createView( this );
}




void TrackContainerView::deleteTrackView( TrackView * _tv )
{
	//m_tc->addJournalCheckPoint();

	Track * t = _tv->getTrack();
	removeTrackView( _tv );
	delete _tv;

	Engine::audioEngine()->requestChangeInModel();
	delete t;
	Engine::audioEngine()->doneChangeInModel();
}




const TrackView * TrackContainerView::trackViewAt( const int _y ) const
{
	const int abs_y = _y + m_scrollArea->verticalScrollBar()->value();
	int y_cnt = 0;

//	debug code
//	qDebug( "abs_y %d", abs_y );

	for (const auto& trackView : m_trackViews)
	{
		const int y_cnt1 = y_cnt;
		y_cnt += trackView->height();
		if (abs_y >= y_cnt1 && abs_y < y_cnt) { return trackView; }
	}
	return( nullptr );
}




bool TrackContainerView::allowRubberband() const
{
	return( false );
}




bool TrackContainerView::knifeMode() const
{
	return false;
}




void TrackContainerView::setPixelsPerBar( int ppb )
{
	m_ppb = ppb;

	// tell all TrackContentWidgets to update their background tile pixmap
	for (const auto& trackView : m_trackViews)
	{
		trackView->getTrackContentWidget()->updateBackground();
	}
}




void TrackContainerView::clearAllTracks()
{
	while( !m_trackViews.empty() )
	{
		TrackView * tv = m_trackViews.takeLast();
		Track * t = tv->getTrack();
		delete tv;
		delete t;
	}
}




//! true if the drop carries a file from a file manager with one of the given
//! (case-insensitive) extensions
static bool hasExternalFileWithExtension(const QMimeData* mime, const QStringList& extensions)
{
	if (mime == nullptr || !mime->hasUrls()) { return false; }
	for (const QUrl& url : mime->urls())
	{
		const QString path = url.toLocalFile();
		for (const QString& ext : extensions)
		{
			if (path.endsWith(ext, Qt::CaseInsensitive)) { return true; }
		}
	}
	return false;
}


void TrackContainerView::dragEnterEvent( QDragEnterEvent * _dee )
{
	// MIDI files dragged in from a file manager (Explorer, Finder, ...)
	if (hasExternalFileWithExtension(_dee->mimeData(), {".mid", ".midi"}))
	{
		_dee->acceptProposedAction();
		return;
	}

	StringPairDrag::processDragEnterEvent( _dee,
		QString( "presetfile,pluginpresetfile,samplefile,instrument,"
				"importedproject,soundfontfile,patchfile,vstpluginfile,projectfile,"
				"track_%1,track_%2" ).
						arg( static_cast<int>(Track::Type::Instrument) ).
						arg( static_cast<int>(Track::Type::Sample) ) );
}




void TrackContainerView::stopRubberBand()
{
	m_rubberBand->hide();
	m_rubberBand->setEnabled( false );
}




void TrackContainerView::dropEvent( QDropEvent * _de )
{
	// external MIDI file(s) dropped from a file manager: import them as tracks
	if (hasExternalFileWithExtension(_de->mimeData(), {".mid", ".midi"}))
	{
		QStringList midiFiles;
		for (const QUrl& url : _de->mimeData()->urls())
		{
			const QString path = url.toLocalFile();
			if (path.endsWith(".mid", Qt::CaseInsensitive)
					|| path.endsWith(".midi", Qt::CaseInsensitive))
			{
				midiFiles << path;
			}
		}
		_de->accept();

		// if the file was dropped onto an existing instrument track, play the
		// MIDI with that instrument instead of creating new tracks
		InstrumentTrack* target = nullptr;
		TrackView* hitView = nullptr;
		const QPoint global = mapToGlobal(_de->position().toPoint());
		for (TrackView* tv : trackViews())
		{
			const QRect r(tv->mapToGlobal(QPoint(0, 0)), tv->size());
			if (r.contains(global))
			{
				hitView = tv;
				target = dynamic_cast<InstrumentTrack*>(tv->getTrack());
				break;
			}
		}

		// translate the horizontal drop position into a song position so the
		// imported notes land where the file was dropped (quantised to a bar)
		TimePos dropStart(0);
		TrackView* refView = hitView != nullptr
				? hitView
				: (trackViews().isEmpty() ? nullptr : trackViews().first());
		if (refView != nullptr)
		{
			const int localX = refView->getTrackContentWidget()->mapFromGlobal(global).x();
			if (localX > 0)
			{
				dropStart = TimePos(currentPosition()
						+ localX * TimePos::ticksPerBar() / static_cast<int>(pixelsPerBar()));
				dropStart = TimePos(dropStart.getBar(), 0);
			}
		}

		// defer the actual import: doing heavy model changes synchronously
		// inside the drop event (while Qt's drag loop is still unwinding)
		// crashes in Qt's event handling
		QTimer::singleShot(0, this, [this, midiFiles, target, dropStart]() {
			importMidiFiles(midiFiles, target, dropStart);
		});
		return;
	}

	QString type = StringPairDrag::decodeKey( _de );
	QString value = StringPairDrag::decodeValue( _de );
	if( type == "instrument" )
	{
		auto it = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, m_tc));
		auto ilt = new InstrumentLoaderThread(this, it, value);
		ilt->start();
		//it->toggledInstrumentTrackButton( true );
		_de->accept();
	}
	else if( type == "samplefile" )
	{
		// dropping an audio sample onto the song editor makes a new sample track
		// with the sample as its first clip (there is no native sample-player
		// instrument in this VST3-only build)
		auto st = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, m_tc));
		if( st != nullptr )
		{
			SampleClip* clip = nullptr;
			if (fixedClips())
			{
				const int pattern = Engine::patternStore()->currentPattern();
				clip = dynamic_cast<SampleClip*>(st->getClip(pattern));
				if (clip != nullptr) { clip->setPatternIndex(pattern); }
			}
			else
			{
				clip = dynamic_cast<SampleClip*>(st->createClip(TimePos(0)));
			}
			if (clip != nullptr)
			{
				clip->setSampleFile( value );
			}
			st->setName( QFileInfo( value ).completeBaseName() );
		}
		_de->accept();
	}
	else if( type == "pluginpresetfile"
		|| type == "soundfontfile" || type == "vstpluginfile"
		|| type == "patchfile" )
	{
		auto it = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, m_tc));
		PluginFactory::PluginInfoAndKey piakn =
			getPluginFactory()->pluginSupportingExtension(FileItem::extension(value));
		Instrument * i = it->loadInstrument(piakn.info.name(), &piakn.key);
		i->loadFile( value );
		// name the track after the plug-in file (e.g. "Vital") instead of the
		// generic host name ("VST3" / "VeSTige")
		if( type == "vstpluginfile" )
		{
			it->setName( QFileInfo( value ).completeBaseName() );
		}
		//it->toggledInstrumentTrackButton( true );
		_de->accept();
	}
	else if( type == "presetfile" )
	{
		DataFile dataFile( value );
		auto it = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, m_tc));
		it->loadPreset(dataFile.content().toElement());

		//it->toggledInstrumentTrackButton( true );
		_de->accept();
	}
	else if( type == "importedproject" )
	{
		ImportFilter::import( value, m_tc );
		_de->accept();
	}

	else if( type == "projectfile")
	{
		if( getGUI()->mainWindow()->mayChangeProject(true) )
		{
			Engine::getSong()->loadProject( value );
		}
		_de->accept();
	}

	else if( type.left( 6 ) == "track_" )
	{
		DataFile dataFile( value.toUtf8() );
		Track::create( dataFile.content().firstChild().toElement(), m_tc );
		_de->accept();
	}
}




void TrackContainerView::importMidiFiles( const QStringList& files,
		InstrumentTrack* target, TimePos dropStart )
{
	if (files.isEmpty()) { return; }

	// Serialise imports: the automation dialog below spins a nested event loop,
	// during which another queued drop's importMidiFiles could re-enter and
	// corrupt the track bookkeeping. If one is already running, retry shortly.
	static bool s_importing = false;
	if (s_importing)
	{
		QTimer::singleShot(50, this, [this, files, target, dropStart]() {
			importMidiFiles(files, target, dropStart);
		});
		return;
	}
	s_importing = true;
	struct Guard { ~Guard() { s_importing = false; } } guard;

	// Ask whether to also keep the automation (tempo/BPM, time signature, CC,
	// pitch bend) the importer generates -- shown whether or not the file is
	// dropped onto an existing instrument track.
	auto answer = QMessageBox::question(this, tr("MIDIインポート"),
			tr("テンポ（BPM）やオートメーション（CC・ピッチベンドなど）も取り込みますか？"),
			QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
	if (answer == QMessageBox::Cancel) { return; }
	const bool keepAutomation = (answer == QMessageBox::Yes);

	// remember the existing tracks so we can identify what the import adds
	const auto existing = std::set<Track*>(m_tc->tracks().begin(), m_tc->tracks().end());

	for (const QString& path : files) { ImportFilter::import(path, m_tc); }

	std::vector<Track*> addedInstruments;
	std::vector<Track*> addedAutomation;
	for (Track* t : m_tc->tracks())
	{
		if (existing.find(t) != existing.end()) { continue; }
		if (t->type() == Track::Type::Instrument) { addedInstruments.push_back(t); }
		else if (t->type() == Track::Type::Automation
				|| t->type() == Track::Type::HiddenAutomation)
		{
			addedAutomation.push_back(t);
		}
	}

	// earliest note position across the import, so the whole sequence can be
	// shifted to the drop point while keeping its internal timing intact
	TimePos minStart = -1;
	for (Track* t : addedInstruments)
	{
		for (Clip* c : t->getClips())
		{
			auto mc = dynamic_cast<MidiClip*>(c);
			if (mc == nullptr || mc->notes().empty()) { continue; }
			if (minStart < 0 || mc->startPosition() < minStart)
			{
				minStart = mc->startPosition();
			}
		}
	}
	if (minStart < 0) { minStart = 0; }

	Engine::audioEngine()->requestChangeInModel();
	if (target != nullptr)
	{
		// play the imported notes with the existing instrument: copy each clip
		// (the whole Note is copied, so velocity/panning/length are preserved)
		// to the drop position, preserving the relative timing between clips
		for (Track* t : addedInstruments)
		{
			auto src = dynamic_cast<InstrumentTrack*>(t);
			if (src == nullptr) { continue; }
			for (Clip* c : src->getClips())
			{
				auto mc = dynamic_cast<MidiClip*>(c);
				if (mc == nullptr || mc->notes().empty()) { continue; }
				const TimePos pos = dropStart + (mc->startPosition() - minStart);
				auto dst = dynamic_cast<MidiClip*>(target->createClip(pos));
				if (dst == nullptr) { continue; }
				for (const Note* n : mc->notes()) { dst->addNote(*n, false); }
				dst->updateLength();
			}
		}
	}
	else
	{
		// keep the imported instrument tracks; shift their clips to the drop
		// point (automation clips stay on the timeline where they belong)
		for (Track* t : addedInstruments)
		{
			for (Clip* c : t->getClips())
			{
				c->movePosition(dropStart + (c->startPosition() - minStart));
			}
		}
	}
	Engine::audioEngine()->doneChangeInModel();

	// Decide which import-created tracks to drop. When dropping onto an
	// instrument track its notes now live on the target, so the imported
	// instrument tracks are always removed; automation is removed unless the
	// user chose to keep it.
	std::vector<Track*> toRemove;
	if (!keepAutomation)
	{
		toRemove.insert(toRemove.end(), addedAutomation.begin(), addedAutomation.end());
	}
	if (target != nullptr)
	{
		toRemove.insert(toRemove.end(), addedInstruments.begin(), addedInstruments.end());
	}
	if (toRemove.empty()) { return; }

	// Defer the removal one more event cycle: the importer just built these
	// tracks' views, whose queued initialisation events would dangle (and
	// crash) if the widgets were deleted synchronously from this deferred call.
	QTimer::singleShot(0, this, [this, toRemove]() {
		for (Track* t : toRemove) { removeTrackSafely(t); }
	});
}




void TrackContainerView::removeTrackSafely( Track* track )
{
	// prefer the view-safe removal (destroys the TrackView before the model)
	for (TrackView* tv : trackViews())
	{
		if (tv->getTrack() == track) { deleteTrackView(tv); return; }
	}
	// no view (e.g. headless): delete the model directly
	Engine::audioEngine()->requestChangeInModel();
	delete track;
	Engine::audioEngine()->doneChangeInModel();
}




RubberBand *TrackContainerView::rubberBand() const
{
	return m_rubberBand;
}




TrackContainerView::scrollArea::scrollArea( TrackContainerView * _parent ) :
	QScrollArea( _parent ),
	m_trackContainerView( _parent )
{
	setFrameStyle( QFrame::NoFrame );
	setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
}




void TrackContainerView::scrollArea::wheelEvent( QWheelEvent * _we )
{
	// always pass wheel-event to parent-widget (song-editor
	// pattern-editor etc.) because they might want to use it for zooming
	// or scrolling left/right if a modifier-key is pressed, otherwise
	// they do not accept it and we pass it up to QScrollArea
	m_trackContainerView->wheelEvent( _we );
	if( !_we->isAccepted() )
	{
		QScrollArea::wheelEvent( _we );
	}
}




unsigned int TrackContainerView::totalHeightOfTracks() const
{
	unsigned int heightSum = 0;
	for (auto & trackView : m_trackViews)
	{
		heightSum += trackView->getTrack()->getHeight();
	}
	return heightSum;
}

} // namespace gui


} // namespace lmms
