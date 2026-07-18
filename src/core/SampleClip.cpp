/*
 * SampleClip.cpp
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
 
#include "SampleClip.h"

#include <QDomElement>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "PatternStore.h"
#include "PathUtil.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimeStretch.h"

namespace lmms
{

SampleClip::SampleClip(Track* _track, Sample sample, bool isPlaying):
	Clip(_track),
	m_sample(std::move(sample)),
	m_isPlaying(false),
	m_startFrameOffset(0)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}

SampleClip::SampleClip(Track* track)
	: SampleClip(track, Sample(), false)
{
}

SampleClip::SampleClip(const SampleClip& orig) :
	Clip(orig),
	m_sample(orig.m_sample),
	m_isPlaying(orig.m_isPlaying),
	m_startFrameOffset(orig.m_startFrameOffset)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// preserve the time-stretch state (setSampleFile cleared it); the copied
	// m_sample already holds the stretched audio, so no re-stretch is needed
	m_originalBuffer = orig.m_originalBuffer;
	m_stretchFactor = orig.m_stretchFactor;
	m_sourceBpm = orig.m_sourceBpm;
	m_sample = orig.m_sample;

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}




SampleClip::~SampleClip()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if ( sampletrack )
	{
		sampletrack->updateClips();
	}
}




void SampleClip::changeLength( const TimePos & _length )
{
	Clip::changeLength(std::max(static_cast<int>(_length), 1));
}



const QString& SampleClip::sampleFile() const
{
	return m_sample.sampleFile();
}

bool SampleClip::hasSampleFileLoaded(const QString & filename) const
{
	return m_sample.sampleFile() == filename;
}

void SampleClip::setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)
{
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_originalBuffer = sb;
		m_sample = Sample(std::move(sb));
	}
	applyStretch();
	updateLength();

	emit sampleChanged();

	Engine::getSong()->setModified();
}

void SampleClip::setSampleFile(const QString& sf)
{
	// Remove any prior offset in the clip
	setStartTimeOffset(0);
	if (!sf.isEmpty())
	{
		m_originalBuffer = SampleBuffer::fromFile(sf);
		m_sample = Sample(m_originalBuffer);
		applyStretch();
		updateLength();
	}
	else
	{
		m_originalBuffer = nullptr;
		// If there is no sample, make the clip a bar long
		float nom = Engine::getSong()->getTimeSigModel().getNumerator();
		float den = Engine::getSong()->getTimeSigModel().getDenominator();
		changeLength(DefaultTicksPerBar * (nom / den));
	}

	emit sampleChanged();
	emit playbackPositionChanged();
}




void SampleClip::applyStretch()
{
	if (m_originalBuffer == nullptr) { return; }

	const int rate = m_originalBuffer->sampleRate();
	auto rebuild = [&](std::shared_ptr<const SampleBuffer> buf) {
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_sample = Sample(std::move(buf));
	};

	if (std::abs(m_stretchFactor - 1.0f) < 1e-3f || m_originalBuffer->size() == 0)
	{
		rebuild(m_originalBuffer);
		return;
	}

	auto stretched = timeStretch(m_originalBuffer->data(), m_originalBuffer->size(), m_stretchFactor);
	auto buf = std::make_shared<SampleBuffer>(std::move(stretched), rate);
	rebuild(buf);
}




void SampleClip::setStretchFactor(float factor)
{
	factor = std::clamp(factor, 0.25f, 4.0f);
	if (std::abs(factor - m_stretchFactor) < 1e-4f) { return; }
	m_stretchFactor = factor;
	applyStretch();
	updateLength();
	emit sampleChanged();
	Engine::getSong()->setModified();
}




void SampleClip::setSourceBpm(int bpm)
{
	m_sourceBpm = std::max(0, bpm);
	if (m_sourceBpm > 0)
	{
		// stretch so the material plays at the current song tempo: material
		// faster than the song (higher BPM) must be lengthened (slowed down)
		setStretchFactor(static_cast<float>(m_sourceBpm) / Engine::getSong()->getTempo());
	}
	else
	{
		setStretchFactor(1.0f);
	}
}




void SampleClip::toggleRecord()
{
	m_recordModel.setValue( !m_recordModel.value() );
	emit dataChanged();
}




void SampleClip::playbackPositionChanged()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( getTrack(), PlayHandle::Type::SamplePlayHandle );
	auto st = dynamic_cast<SampleTrack*>(getTrack());
	st->setPlayingClips( false );
}




void SampleClip::updateTrackClips()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if( sampletrack)
	{
		sampletrack->updateClips();
	}
}




bool SampleClip::isPlaying() const
{
	return m_isPlaying;
}




void SampleClip::setIsPlaying(bool isPlaying)
{
	m_isPlaying = isPlaying;
}




void SampleClip::updateLength()
{
	// If the clip has already been manually resized, don't automatically resize it.
	if (getAutoResize())
	{
		if (getTrack()->trackContainer() == Engine::patternStore())
		{
			// A pattern sample is displayed and played at its actual duration.
			// PatternStore rounds the complete pattern to a full bar for looping.
			changeLength(sampleLength());
			return;
		}
		changeLength(sampleLength());
		setStartTimeOffset(0);
	}

	emit sampleChanged();
}


void SampleClip::tempoChanged()
{
	Clip::setStartTimeOffset(std::round(1.0f * m_startFrameOffset / Engine::framesPerTick()));
	updateLength();
	emit sampleChanged();

	// Keep BPM-synced clips (a source BPM was set) matched to the new song
	// tempo, pitch preserved. Defer + debounce the WSOLA re-stretch: it is far
	// too heavy to run on every intermediate value while dragging the tempo, and
	// tempoChanged() may fire on the audio thread (tempo automation) where the
	// rebuild must never run. The singleShot always runs on this object's (GUI)
	// thread.
	if (m_sourceBpm > 0 && !m_tempoRestretchPending)
	{
		m_tempoRestretchPending = true;
		QTimer::singleShot(150, this, [this]() {
			m_tempoRestretchPending = false;
			if (m_sourceBpm <= 0) { return; }
			setStretchFactor(static_cast<float>(m_sourceBpm) / Engine::getSong()->getTempo());
			updateLength();
			emit sampleChanged();
		});
	}
}

void SampleClip::setStartTimeOffset(const TimePos& startTimeOffset)
{
	m_startFrameOffset = startTimeOffset * Engine::framesPerTick();
	Clip::setStartTimeOffset(startTimeOffset);
}


TimePos SampleClip::sampleLength() const
{
	return static_cast<int>(m_sample.sampleSize() / Engine::framesPerTick(m_sample.sampleRate()));
}




void SampleClip::setSampleStartFrame(f_cnt_t startFrame)
{
	m_sample.setStartFrame(startFrame);
}




void SampleClip::setSamplePlayLength(f_cnt_t length)
{
	m_sample.setEndFrame(length);
}




void SampleClip::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	if( _this.parentNode().nodeName() == "clipboard" )
	{
		_this.setAttribute( "pos", -1 );
	}
	else
	{
		_this.setAttribute( "pos", startPosition() );
	}
	// persist the ORIGINAL (un-stretched) source, so reloading re-applies the
	// stretch from the original instead of stretching an already-stretched
	// sample (which would compound on copy/paste)
	const QString origFile = m_originalBuffer != nullptr
			? m_originalBuffer->audioFile() : sampleFile();
	_this.setAttribute( "len", length() );
	_this.setAttribute( "muted", isMuted() );
	_this.setAttribute( "src", origFile );
	_this.setAttribute( "off", startTimeOffset() );
	_this.setAttribute("autoresize", QString::number(getAutoResize()));
	if (origFile.isEmpty())
	{
		_this.setAttribute("data", m_originalBuffer != nullptr
				? m_originalBuffer->toBase64() : m_sample.toBase64());
	}

	_this.setAttribute( "sample_rate", m_sample.sampleRate());
	if (m_stretchFactor != 1.0f) { _this.setAttribute("stretch", QString::number(m_stretchFactor)); }
	if (m_sourceBpm > 0) { _this.setAttribute("srcbpm", QString::number(m_sourceBpm)); }
	if (patternIndex() >= 0) { _this.setAttribute("patternindex", patternIndex()); }
	if (const auto& c = color())
	{
		_this.setAttribute("color", c->name());
	}
	if (m_sample.reversed())
	{
		_this.setAttribute("reversed", "true");
	}
	// TODO: start- and end-frame
}




void SampleClip::loadSettings( const QDomElement & _this )
{
	if( _this.attribute( "pos" ).toInt() >= 0 )
	{
		movePosition( _this.attribute( "pos" ).toInt() );
	}

	if (const auto srcFile = _this.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			setSampleFile(srcFile);
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile)); }
	}

	if( sampleFile().isEmpty() && _this.hasAttribute( "data" ) )
	{
		auto sampleRate = _this.hasAttribute("sample_rate") ? _this.attribute("sample_rate").toInt() :
			Engine::audioEngine()->outputSampleRate();

		m_originalBuffer = SampleBuffer::fromBase64(_this.attribute("data"), sampleRate);
		m_sample = Sample(m_originalBuffer);
	}

	// restore time-stretch settings and apply them to the loaded sample
	m_stretchFactor = _this.hasAttribute("stretch") ? _this.attribute("stretch").toFloat() : 1.0f;
	m_sourceBpm = _this.attribute("srcbpm").toInt();
	setPatternIndex(_this.attribute("patternindex", "-1").toInt());
	if (m_stretchFactor != 1.0f) { applyStretch(); }

	changeLength( _this.attribute( "len" ).toInt() );
	setMuted( _this.attribute( "muted" ).toInt() );
	setStartTimeOffset( _this.attribute( "off" ).toInt() );
	setAutoResize(_this.attribute("autoresize", "1").toInt());

	if (_this.hasAttribute("color"))
	{
		setColor(QColor{_this.attribute("color")});
	}

	if(_this.hasAttribute("reversed"))
	{
		m_sample.setReversed(true);
		emit wasReversed(); // tell SampleClipView to update the view
	}
}




gui::ClipView * SampleClip::createView( gui::TrackView * _tv )
{
	return new gui::SampleClipView( this, _tv );
}


} // namespace lmms
