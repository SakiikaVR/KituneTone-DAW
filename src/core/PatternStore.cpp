/*
 * PatternStore.cpp - model-component of Pattern Editor
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


#include "PatternStore.h"

#include "Clip.h"
#include "Engine.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "Song.h"

namespace lmms
{


PatternStore::PatternStore() :
	TrackContainer(),
	m_patternComboBoxModel(this)
{
	setType(Type::Pattern);
}




bool PatternStore::play(TimePos start, f_cnt_t frames, f_cnt_t offset, int clipNum)
{
	bool notePlayed = false;

	if (lengthOfPattern(clipNum) <= 0)
	{
		return false;
	}

	start = start % (lengthOfPattern(clipNum) * TimePos::ticksPerBar());

	const TrackList& tl = tracks();
	for (Track * t : tl)
	{
		if (t->play(start, frames, offset, clipNum))
		{
			notePlayed = true;
		}
	}

	return notePlayed;
}




void PatternStore::updateAfterTrackAdd()
{
	if (numOfPatterns() == 0 && !Engine::getSong()->isLoadingProject())
	{
		Engine::getSong()->addPatternTrack();
	}
}




bar_t PatternStore::lengthOfPattern(int pattern) const
{
	TimePos maxLength = TimePos::ticksPerBar();

	const TrackList & tl = tracks();
	for (Track * t : tl)
	{
		// Do not create clips here if they do not exist. Instrument and sample
		// clips both define the audible pattern length; automation follows it.
		if (pattern < t->numOfClips() && t->type() == Track::Type::Instrument)
		{
			maxLength = std::max(maxLength, t->getClip(pattern)->length());
		}
		else if (t->type() == Track::Type::Sample)
		{
			for (Clip* clip : t->getClips())
			{
				auto sample = static_cast<SampleClip*>(clip);
				if (sample->patternIndex() != pattern) { continue; }
				const TimePos relativeEnd(sample->patternOffset() + sample->length());
				maxLength = std::max(maxLength, relativeEnd);
			}
		}
	}

	return maxLength.nextFullBar();
}




int PatternStore::numOfPatterns() const
{
	return Engine::getSong()->countTracks(Track::Type::Pattern);
}




void PatternStore::removePattern(int pattern)
{
	const TrackList& tl = tracks();
	for (Track * t : tl)
	{
		if (t->type() == Track::Type::Sample)
		{
			const auto clips = t->getClips();
			for (Clip* clip : clips)
			{
				auto sample = static_cast<SampleClip*>(clip);
				if (clip != t->getClip(pattern) && sample->patternIndex() == pattern)
				{
					delete clip;
				}
			}
		}
		if (t->type() == Track::Type::Sample)
		{
			delete t->getClip(pattern);
			for (Clip* clip : t->getClips())
			{
				auto sample = static_cast<SampleClip*>(clip);
				if (sample->patternIndex() > pattern)
				{
					sample->setPatternIndex(sample->patternIndex() - 1);
					sample->movePosition(sample->startPosition() - TimePos::ticksPerBar());
				}
			}
		}
		else
		{
			delete t->getClip(pattern);
			t->removeBar(pattern * DefaultTicksPerBar);
		}
	}
	if (pattern <= currentPattern())
	{
		setCurrentPattern(std::max(currentPattern() - 1, 0));
	}
}




void PatternStore::swapPattern(int pattern1, int pattern2)
{
	const TrackList& tl = tracks();
	for (Track * t : tl)
	{
		if (t->type() == Track::Type::Sample)
		{
			Clip* anchor1 = t->getClip(pattern1);
			Clip* anchor2 = t->getClip(pattern2);
			for (Clip* clip : t->getClips())
			{
				if (clip == anchor1 || clip == anchor2) { continue; }
				auto sample = static_cast<SampleClip*>(clip);
				const int clipPattern = sample->patternIndex();
				if (clipPattern == pattern1)
				{
					clip->movePosition(clip->startPosition()
							+ (pattern2 - pattern1) * TimePos::ticksPerBar());
					sample->setPatternIndex(pattern2);
				}
				else if (clipPattern == pattern2)
				{
					clip->movePosition(clip->startPosition()
							+ (pattern1 - pattern2) * TimePos::ticksPerBar());
					sample->setPatternIndex(pattern1);
				}
			}
		}
		t->swapPositionOfClips(pattern1, pattern2);
		if (t->type() == Track::Type::Sample)
		{
			static_cast<SampleClip*>(t->getClip(pattern1))->setPatternIndex(pattern1);
			static_cast<SampleClip*>(t->getClip(pattern2))->setPatternIndex(pattern2);
		}
	}
	updateComboBox();
}




void PatternStore::updatePatternTrack(Clip* clip)
{
	const int pattern = clip->getTrack()->type() == Track::Type::Sample
			? static_cast<SampleClip*>(clip)->patternIndex()
			: clip->startPosition() / DefaultTicksPerBar;
	PatternTrack * t = PatternTrack::findPatternTrack(pattern);
	if (t != nullptr)
	{
		t->dataChanged();
	}
	emit trackUpdated();
}




void PatternStore::fixIncorrectPositions()
{
	const TrackList& tl = tracks();
	for (Track * t : tl)
	{
		for (int i = 0; i < numOfPatterns(); ++i)
		{
			t->getClip(i)->movePosition(TimePos(i, 0));
			if (t->type() == Track::Type::Sample)
			{
				static_cast<SampleClip*>(t->getClip(i))->setPatternIndex(i);
			}
		}
	}
}




void PatternStore::play()
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




void PatternStore::stop()
{
	Engine::getSong()->stop();
}




void PatternStore::updateComboBox()
{
	const int curPattern = currentPattern();

	m_patternComboBoxModel.clear();

	for (int i = 0; i < numOfPatterns(); ++i)
	{
		PatternTrack* pt = PatternTrack::findPatternTrack(i);
		m_patternComboBoxModel.addItem(pt->name());
	}
	setCurrentPattern(curPattern);
}




void PatternStore::createClipsForPattern(int pattern)
{
	const TrackList& tl = tracks();
	for (Track * t : tl)
	{
		t->createClipsForPattern(pattern);
	}
}

AutomatedValueMap PatternStore::automatedValuesAt(TimePos time, int clipNum) const
{
	Q_ASSERT(clipNum >= 0);
	Q_ASSERT(time.getTicks() >= 0);

	auto lengthBars = lengthOfPattern(clipNum);
	auto lengthTicks = lengthBars * TimePos::ticksPerBar();
	if (time > lengthTicks)
	{
		time = lengthTicks;
	}

	return TrackContainer::automatedValuesAt(time + (TimePos::ticksPerBar() * clipNum), clipNum);
}


} // namespace lmms
