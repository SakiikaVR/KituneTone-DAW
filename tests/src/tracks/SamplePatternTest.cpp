#include <QtTest>
#include <QDomDocument>

#include "Engine.h"
#include "PatternStore.h"
#include "SampleClip.h"
#include "SampleTrack.h"

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
		SampleTrack track(patternStore);
		track.createClipsForPattern(1);

		QCOMPARE(track.getClip(0)->startPosition(), TimePos(0, 0));
		QCOMPARE(track.getClip(1)->startPosition(), TimePos(1, 0));

		auto extra = static_cast<SampleClip*>(track.createClip(
				TimePos(2, TimePos::ticksPerBar() / 2)));
		extra->setPatternIndex(1);
		extra->setAutoResize(false);
		extra->changeLength(TimePos(TimePos::ticksPerBar() / 2));

		QCOMPARE(extra->patternIndex(), 1);
		QCOMPARE(extra->patternOffset(), TimePos(1, TimePos::ticksPerBar() / 2));
		QCOMPARE(patternStore->lengthOfPattern(1), static_cast<bar_t>(2));

		QDomDocument document;
		QDomElement saved = document.createElement(extra->nodeName());
		document.appendChild(saved);
		extra->saveSettings(document, saved);
		auto restored = new SampleClip(&track);
		restored->loadSettings(saved);
		QCOMPARE(restored->patternIndex(), 1);
		QCOMPARE(restored->patternOffset(), extra->patternOffset());
		delete restored;

		auto earlierPatternLongOffset = static_cast<SampleClip*>(track.createClip(TimePos(2, 0)));
		earlierPatternLongOffset->setPatternIndex(0);
		earlierPatternLongOffset->setAutoResize(false);
		earlierPatternLongOffset->changeLength(TimePos(TimePos::ticksPerBar() / 4));
		const TimePos earlierPosition = earlierPatternLongOffset->startPosition();

		// Adding another pattern inserts its fixed anchor before extra samples,
		// preserving the getClip(pattern) contract.
		track.createClipsForPattern(2);
		QCOMPARE(track.getClip(2)->startPosition(), TimePos(2, 0));
		QCOMPARE(extra->patternIndex(), 1);

		// Removing pattern 1 must not shift a long-offset sample that belongs
		// to pattern 0 merely because its encoded timeline position is later.
		patternStore->removePattern(1);
		QCOMPARE(earlierPatternLongOffset->patternIndex(), 0);
		QCOMPARE(earlierPatternLongOffset->startPosition(), earlierPosition);
		QCOMPARE(track.getClip(1)->startPosition(), TimePos(1, 0));
	}
};

QTEST_GUILESS_MAIN(SamplePatternTest)

#include "SamplePatternTest.moc"
