#include <QtTest>
#include <QDomDocument>

#include <algorithm>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

class SamplePatternTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		lmms::Engine::init(true);
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}

	void supportsMultiplePositionedSamplesPerPattern()
	{
		using namespace lmms;

		auto patternStore = Engine::patternStore();
		auto song = Engine::getSong();
		auto pattern0 = new PatternTrack(song);
		auto pattern1 = new PatternTrack(song);
		patternStore->setCurrentPattern(1);
		pattern1->setPatternLengthBars(3);
		QDomDocument patternDocument;
		QDomElement patternSettings = patternDocument.createElement("patterntrack");
		pattern1->saveTrackSpecificSettings(patternDocument, patternSettings, false);
		QCOMPARE(patternSettings.attribute("bars").toInt(), 3);
		SampleTrack track(patternStore);

		auto extra = static_cast<SampleClip*>(track.createClip(
				TimePos(2, TimePos::ticksPerBar() / 2)));
		// A local bar position is not a pattern identifier.
		QCOMPARE(extra->patternIndex(), 1);
		extra->setAutoResize(false);
		extra->changeLength(TimePos(TimePos::ticksPerBar() / 2));

		QCOMPARE(extra->patternIndex(), 1);
		QCOMPARE(extra->patternOffset(), TimePos(2, TimePos::ticksPerBar() / 2));
		QCOMPARE(patternStore->lengthOfPattern(1), static_cast<bar_t>(3));

		QDomDocument document;
		QDomElement saved = document.createElement(extra->nodeName());
		document.appendChild(saved);
		extra->saveSettings(document, saved);
		auto restored = new SampleClip(&track);
		restored->loadSettings(saved);
		QCOMPARE(restored->patternIndex(), 1);
		QCOMPARE(restored->patternOffset(), extra->patternOffset());
		delete restored;

		InstrumentTrack midiTrack(patternStore);
		auto midi = static_cast<MidiClip*>(midiTrack.createClip(TimePos(2, 0)));
		midi->setPatternIndex(1);
		QCOMPARE(midi->length(), TimePos(1, 0));
		QDomElement midiSaved = document.createElement(midi->nodeName());
		midi->saveSettings(document, midiSaved);
		auto midiRestored = static_cast<MidiClip*>(midiTrack.createClip(TimePos(0)));
		midiRestored->loadSettings(midiSaved);
		QCOMPARE(midiRestored->patternIndex(), 1);
		QCOMPARE(midiRestored->startPosition(), TimePos(2, 0));

		auto earlierPatternLongOffset = static_cast<SampleClip*>(track.createClip(TimePos(2, 0)));
		earlierPatternLongOffset->setPatternIndex(0);
		earlierPatternLongOffset->setAutoResize(false);
		earlierPatternLongOffset->changeLength(TimePos(TimePos::ticksPerBar() / 4));
		const TimePos earlierPosition = earlierPatternLongOffset->startPosition();

		// Removing a pattern deletes only its clips and leaves positions in the
		// independent local timeline of every other pattern unchanged.
		delete pattern1;
		QCOMPARE(earlierPatternLongOffset->patternIndex(), 0);
		QCOMPARE(earlierPatternLongOffset->startPosition(), earlierPosition);
		QVERIFY(std::find(track.getClips().begin(), track.getClips().end(), extra)
				== track.getClips().end());
		delete pattern0;
	}
};

QTEST_GUILESS_MAIN(SamplePatternTest)

#include "SamplePatternTest.moc"
