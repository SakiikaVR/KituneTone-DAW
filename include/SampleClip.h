/*
 * SampleClip.h
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

#ifndef LMMS_SAMPLE_CLIP_H
#define LMMS_SAMPLE_CLIP_H

#include <memory>
#include "Clip.h"
#include "lmms_export.h"
#include "Sample.h"

namespace lmms
{

class SampleBuffer;

namespace gui
{

class SampleClipView;

} // namespace gui


class LMMS_EXPORT SampleClip : public Clip
{
	Q_OBJECT
	mapPropertyFromModel(bool,isRecord,setRecord,m_recordModel);
public:
	SampleClip(Track* track, Sample sample, bool isPlaying);
	SampleClip(Track* track);
	~SampleClip() override;

	SampleClip& operator=( const SampleClip& that ) = delete;

	void changeLength( const TimePos & _length ) override;
	const QString& sampleFile() const;
	bool hasSampleFileLoaded(const QString & filename) const;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	inline QString nodeName() const override
	{
		return "sampleclip";
	}

	Sample& sample()
	{
		return m_sample;
	}

	TimePos sampleLength() const;
	void setSampleStartFrame( f_cnt_t startFrame );
	void setSamplePlayLength( f_cnt_t length );
	void setStartTimeOffset(const TimePos& startTimeOffset) override;
	gui::ClipView * createView( gui::TrackView * _tv ) override;


	bool isPlaying() const;
	void setIsPlaying(bool isPlaying);
	void setSampleBuffer(std::shared_ptr<const SampleBuffer> sb);

	//! Time-stretch factor applied to the audio (1.0 = original length,
	//! pitch preserved). Values > 1 lengthen, < 1 shorten.
	float stretchFactor() const { return m_stretchFactor; }
	void setStretchFactor(float factor);
	//! Original tempo of the material in BPM (0 = disabled). When set, the
	//! clip is auto-stretched to the current song tempo.
	int sourceBpm() const { return m_sourceBpm; }
	void setSourceBpm(int bpm);

	SampleClip* clone() override
	{
		return new SampleClip(*this);
	}

public slots:
	void setSampleFile(const QString& sf);
	void updateLength();
	void toggleRecord();
	void playbackPositionChanged();
	void updateTrackClips();
	void tempoChanged();

private:
	//! rebuild m_sample from m_originalBuffer applying the current stretch
	void applyStretch();

protected:
	SampleClip( const SampleClip& orig );

private:
	Sample m_sample;
	//! the un-stretched source, kept so re-stretching starts from the original
	std::shared_ptr<const SampleBuffer> m_originalBuffer;
	float m_stretchFactor = 1.0f;
	int m_sourceBpm = 0;
	BoolModel m_recordModel;
	bool m_isPlaying;
	int m_startFrameOffset;

	friend class gui::SampleClipView;


signals:
	void sampleChanged();
	void wasReversed();
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_CLIP_H
