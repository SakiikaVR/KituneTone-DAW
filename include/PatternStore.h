/*
 * PatternStore.h - model-component of Pattern Editor
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

#ifndef LMMS_PATTERN_STORE_H
#define LMMS_PATTERN_STORE_H

#include "TrackContainer.h"
#include "ComboBoxModel.h"

namespace lmms
{

namespace gui
{
	class PatternEditorWindow;
}

/*
 * PatternStore is the backend of Pattern Editor:
 *
 * +----------------------------------+
 * | PatternStore (TrackContainer)    |
 * |                                  |
 * | +------------------------------+ |
 * | | Track 1   [Clip A]  [Clip B] | |
 * | +------------------------------+ |
 * |                                  |
 * | +------------------------------+ |
 * | | Track 2   [Clip C]  [Clip D] | |
 * | +------------------------------+ |
 * +----------------------------------+
 *
 * There is only one PatternStore, but each pattern is an independent local
 * timeline. Every clip stores its owning pattern index separately from its
 * start position, so MIDI, sample and automation tracks can all contain
 * multiple clips at arbitrary positions inside a pattern. The Pattern Editor
 * filters the shared track list by the selected pattern.
 *
 * Do not confuse tracks and clips in the PatternStore with PatternTracks and PatternClips.
 * - PatternTracks are used in the Song Editor. Each one reference a "pattern" in the PatternStore.
 * - PatternClips are stored inside PatternTracks and place/repeat one complete
 *   pattern timeline in the Song Editor.
 */
class LMMS_EXPORT PatternStore : public TrackContainer
{
	Q_OBJECT
	mapPropertyFromModel(int, currentPattern, setCurrentPattern, m_patternComboBoxModel);
public:
	PatternStore();
	~PatternStore() override = default;

	virtual bool play(TimePos start, const f_cnt_t frames, const f_cnt_t frameBase, int clipNum = -1);

	void updateAfterTrackAdd() override;

	inline QString nodeName() const override
	{
		return "patternstore";
	}

	bar_t lengthOfPattern(int pattern) const;
	inline bar_t lengthOfCurrentPattern()
	{
		return lengthOfPattern(currentPattern());
	}
	int numOfPatterns() const;
	void removePattern(int pattern);

	void swapPattern(int p1, int p2);

	void updatePatternTrack(Clip* clip);
	void fixIncorrectPositions();
	void createClipsForPattern(int pattern);

	AutomatedValueMap automatedValuesAt(TimePos time, int clipNum) const override;

public slots:
	void play();
	void stop();
	void updateComboBox();

signals:
	void trackUpdated();

private:
	ComboBoxModel m_patternComboBoxModel;


	// Where the pattern selection combo box is
	friend class gui::PatternEditorWindow;

} ;


} // namespace lmms

#endif // LMMS_PATTERN_STORE_H
