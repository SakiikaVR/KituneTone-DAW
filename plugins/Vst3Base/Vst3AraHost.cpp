/*
 * Vst3AraHost.cpp - experimental ARA 2 host integration for LMMS
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

#ifdef LMMS_HAVE_ARA

#include "Vst3AraHost.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <atomic>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ConfigManager.h"
#include "Engine.h"
#include "SampleDecoder.h"
#include "Song.h"

using namespace ARA;

namespace lmms
{

namespace
{

//! zero-initialise an ARA property struct and stamp its structSize
template <typename T>
T makeProps()
{
	T props;
	std::memset(&props, 0, sizeof(T));
	props.structSize = sizeof(T);
	return props;
}

std::atomic<long> g_audioReadCount{0};

} // anonymous namespace




void araDebugLog(const QString& message)
{
	const QString path = ConfigManager::inst()->workingDir() + "lmms_ara_debug.log";
	QFile file(path);
	if (file.open(QIODevice::Append | QIODevice::Text))
	{
		QTextStream ts(&file);
		ts << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "  " << message << "\n";
	}
}




long araAudioReadCount()
{
	return g_audioReadCount.load();
}




void araEnsureFactoryInitialized(const ARAFactory* factory)
{
	static std::set<std::string> s_initialized;
	if (factory == nullptr || factory->factoryID == nullptr) { return; }

	const std::string id = factory->factoryID;
	if (s_initialized.count(id)) { return; }
	s_initialized.insert(id);

	ARAAPIGeneration desired = kARAAPIGeneration_2_0_Final;
	if (desired > factory->highestSupportedApiGeneration)
	{
		desired = factory->highestSupportedApiGeneration;
	}
	if (desired < factory->lowestSupportedApiGeneration)
	{
		desired = factory->lowestSupportedApiGeneration;
	}

	auto config = makeProps<ARAInterfaceConfiguration>();
	config.desiredApiGeneration = desired;
	config.assertFunctionAddress = nullptr; // asserts disabled in this build
	factory->initializeARAWithConfiguration(&config);
}




// ---------------------------------------------------------------------------
// Host controller interface implementations
// ---------------------------------------------------------------------------

//! Decoded audio for one ARA audio source; the host ref of the audio source
//! points to an instance of this so readers can find their data.
struct AraAudioData
{
	std::vector<float> left;
	std::vector<float> right;
	double sampleRate = 44100.0;
	ARASampleCount frames = 0;
	std::string persistentID;
};


//! Serves decoded audio samples to the plug-in (random access).
class AraAudioAccessController : public Host::AudioAccessControllerInterface
{
public:
	struct Reader { AraAudioData* data; bool use64; };

	ARAAudioReaderHostRef createAudioReaderForSource(
			ARAAudioSourceHostRef sourceRef, bool use64BitSamples) noexcept override
	{
		auto data = reinterpret_cast<AraAudioData*>(sourceRef);
		araDebugLog(QString("createAudioReaderForSource: use64=%1 frames=%2")
				.arg(use64BitSamples).arg(data ? static_cast<long long>(data->frames) : -1));
		auto reader = new Reader{data, use64BitSamples};
		return reinterpret_cast<ARAAudioReaderHostRef>(reader);
	}

	bool readAudioSamples(ARAAudioReaderHostRef readerRef, ARASamplePosition samplePosition,
			ARASampleCount samplesPerChannel, void* const buffers[]) noexcept override
	{
		g_audioReadCount.fetch_add(1);
		auto reader = reinterpret_cast<Reader*>(readerRef);
		if (reader == nullptr || reader->data == nullptr) { return false; }
		const AraAudioData* d = reader->data;
		const auto total = static_cast<ARASampleCount>(d->left.size());
		for (ARASampleCount i = 0; i < samplesPerChannel; ++i)
		{
			const ARASamplePosition pos = samplePosition + i;
			const bool valid = pos >= 0 && pos < total;
			const float l = valid ? d->left[static_cast<size_t>(pos)] : 0.f;
			const float r = valid ? d->right[static_cast<size_t>(pos)] : 0.f;
			if (reader->use64)
			{
				static_cast<double*>(buffers[0])[i] = l;
				if (buffers[1]) { static_cast<double*>(buffers[1])[i] = r; }
			}
			else
			{
				static_cast<float*>(buffers[0])[i] = l;
				if (buffers[1]) { static_cast<float*>(buffers[1])[i] = r; }
			}
		}
		return true;
	}

	void destroyAudioReader(ARAAudioReaderHostRef readerRef) noexcept override
	{
		delete reinterpret_cast<Reader*>(readerRef);
	}
};


//! Minimal archiving controller (no persistency support).
class AraArchivingController : public Host::ArchivingControllerInterface
{
public:
	ARASize getArchiveSize(ARAArchiveReaderHostRef) noexcept override { return 0; }
	bool readBytesFromArchive(ARAArchiveReaderHostRef, ARASize, ARASize,
			ARAByte[]) noexcept override { return false; }
	bool writeBytesToArchive(ARAArchiveWriterHostRef, ARASize, ARASize,
			const ARAByte[]) noexcept override { return true; }
	void notifyDocumentArchivingProgress(float) noexcept override {}
	void notifyDocumentUnarchivingProgress(float) noexcept override {}
	ARAPersistentID getDocumentArchiveID(ARAArchiveReaderHostRef) noexcept override { return ""; }
};


//! Content access controller providing the song tempo and bar signature so the
//! plug-in can align to the DAW timeline ("Use DAW Tempo / Beat").
class AraContentAccessController : public Host::ContentAccessControllerInterface
{
public:
	double tempo = 120.0;
	int timeSigNum = 4;
	int timeSigDen = 4;

	// a content reader is a small object holding the events it will hand out
	struct Reader
	{
		ARAContentType type;
		std::vector<ARAContentTempoEntry> tempo;
		std::vector<ARAContentBarSignature> bars;
		ARAContentTempoEntry tempoScratch{};
		ARAContentBarSignature barScratch{};
	};

	bool isMusicalContextContentAvailable(ARAMusicalContextHostRef, ARAContentType type) noexcept override
	{
		return type == kARAContentTypeTempoEntries || type == kARAContentTypeBarSignatures;
	}
	ARAContentGrade getMusicalContextContentGrade(ARAMusicalContextHostRef, ARAContentType) noexcept override
	{
		return kARAContentGradeAdjusted;
	}
	ARAContentReaderHostRef createMusicalContextContentReader(ARAMusicalContextHostRef,
			ARAContentType type, const ARAContentTimeRange*) noexcept override
	{
		auto reader = new Reader();
		reader->type = type;
		if (type == kARAContentTypeTempoEntries)
		{
			// constant tempo: two anchor points one quarter note apart
			const double secondsPerQuarter = 60.0 / (tempo > 0.0 ? tempo : 120.0);
			reader->tempo.push_back(ARAContentTempoEntry{0.0, 0.0});
			reader->tempo.push_back(ARAContentTempoEntry{secondsPerQuarter, 1.0});
		}
		else if (type == kARAContentTypeBarSignatures)
		{
			reader->bars.push_back(ARAContentBarSignature{timeSigNum, timeSigDen, 0.0});
		}
		return reinterpret_cast<ARAContentReaderHostRef>(reader);
	}
	bool isAudioSourceContentAvailable(ARAAudioSourceHostRef, ARAContentType) noexcept override
	{ return false; }
	ARAContentGrade getAudioSourceContentGrade(ARAAudioSourceHostRef, ARAContentType) noexcept override
	{ return kARAContentGradeInitial; }
	ARAContentReaderHostRef createAudioSourceContentReader(ARAAudioSourceHostRef, ARAContentType,
			const ARAContentTimeRange*) noexcept override { return nullptr; }
	ARAInt32 getContentReaderEventCount(ARAContentReaderHostRef readerRef) noexcept override
	{
		auto reader = reinterpret_cast<Reader*>(readerRef);
		if (reader == nullptr) { return 0; }
		return reader->type == kARAContentTypeTempoEntries
				? static_cast<ARAInt32>(reader->tempo.size())
				: static_cast<ARAInt32>(reader->bars.size());
	}
	const void* getContentReaderDataForEvent(ARAContentReaderHostRef readerRef, ARAInt32 eventIndex) noexcept override
	{
		auto reader = reinterpret_cast<Reader*>(readerRef);
		if (reader == nullptr) { return nullptr; }
		if (reader->type == kARAContentTypeTempoEntries)
		{
			if (eventIndex < 0 || eventIndex >= static_cast<ARAInt32>(reader->tempo.size())) { return nullptr; }
			reader->tempoScratch = reader->tempo[static_cast<size_t>(eventIndex)];
			return &reader->tempoScratch;
		}
		if (eventIndex < 0 || eventIndex >= static_cast<ARAInt32>(reader->bars.size())) { return nullptr; }
		reader->barScratch = reader->bars[static_cast<size_t>(eventIndex)];
		return &reader->barScratch;
	}
	void destroyContentReader(ARAContentReaderHostRef readerRef) noexcept override
	{
		delete reinterpret_cast<Reader*>(readerRef);
	}
};


//! Model update notifications - not needed for basic rendering.
class AraModelUpdateController : public Host::ModelUpdateControllerInterface
{
public:
	void notifyAudioSourceAnalysisProgress(ARAAudioSourceHostRef, ARAAnalysisProgressState,
			float) noexcept override {}
	void notifyAudioSourceContentChanged(ARAAudioSourceHostRef, const ARAContentTimeRange*,
			ContentUpdateScopes) noexcept override {}
	void notifyAudioModificationContentChanged(ARAAudioModificationHostRef, const ARAContentTimeRange*,
			ContentUpdateScopes) noexcept override {}
	void notifyPlaybackRegionContentChanged(ARAPlaybackRegionHostRef, const ARAContentTimeRange*,
			ContentUpdateScopes) noexcept override {}
	void notifyDocumentDataChanged() noexcept override {}
};


//! Playback transport requests from the plug-in, forwarded to the LMMS
//! transport on the GUI thread (these calls may arrive on any plug-in thread)
class AraPlaybackController : public Host::PlaybackControllerInterface
{
public:
	void requestStartPlayback() noexcept override
	{
		araDebugLog("plug-in requested playback start");
		auto song = Engine::getSong();
		if (song == nullptr) { return; }
		QMetaObject::invokeMethod(song, [song]() {
			if (!song->isPlaying()) { song->playSong(); }
		}, Qt::QueuedConnection);
	}

	void requestStopPlayback() noexcept override
	{
		araDebugLog("plug-in requested playback stop");
		auto song = Engine::getSong();
		if (song == nullptr) { return; }
		QMetaObject::invokeMethod(song, [song]() { song->stop(); }, Qt::QueuedConnection);
	}

	void requestSetPlaybackPosition(ARATimePosition seconds) noexcept override
	{
		araDebugLog(QString("plug-in requested playback position %1s").arg(seconds));
		auto song = Engine::getSong();
		if (song == nullptr) { return; }
		QMetaObject::invokeMethod(song, [song, seconds]() {
			const double ticksPerSecond = song->getTempo() * 48.0 / 60.0;
			song->setPlayPos(static_cast<tick_t>(seconds * ticksPerSecond), Song::PlayMode::Song);
		}, Qt::QueuedConnection);
	}

	void requestSetCycleRange(ARATimePosition, ARATimeDuration) noexcept override {}
	void requestEnableCycle(bool) noexcept override {}
};




struct Vst3AraDocument::Impl
{
	AraAudioAccessController audioAccess;
	AraArchivingController archiving;
	AraContentAccessController content;
	AraModelUpdateController modelUpdate;
	AraPlaybackController playback;

	// decoded audio, one entry per source (pointers are stable while alive)
	std::vector<std::unique_ptr<AraAudioData>> audioData;

	std::unique_ptr<Host::DocumentControllerHostInstance> hostInstance;
	const ARADocumentControllerInstance* dcInstance = nullptr;
	std::unique_ptr<Host::DocumentController> controller;
};




Vst3AraDocument::Vst3AraDocument() :
	m_impl(std::make_unique<Impl>())
{
}




Vst3AraDocument::~Vst3AraDocument()
{
	teardown();
}




bool Vst3AraDocument::setup(const ARAFactory* araFactory,
		IPlugInEntryPoint2* entryPoint,
		const std::vector<Vst3Plugin::AraSource>& sources,
		double tempo, int timeSigNum, int timeSigDen)
{
	if (araFactory == nullptr || entryPoint == nullptr || sources.empty()) { return false; }

	m_araFactory = araFactory;
	m_impl->content.tempo = tempo;
	m_impl->content.timeSigNum = timeSigNum;
	m_impl->content.timeSigDen = timeSigDen;

	araEnsureFactoryInitialized(araFactory);

	m_impl->hostInstance = std::make_unique<Host::DocumentControllerHostInstance>(
			&m_impl->audioAccess, &m_impl->archiving, &m_impl->content,
			&m_impl->modelUpdate, &m_impl->playback);

	auto docProps = makeProps<ARADocumentProperties>();
	docProps.name = "LMMS ARA Document";
	m_impl->dcInstance = araFactory->createDocumentControllerWithDocument(
			m_impl->hostInstance.get(), &docProps);
	if (m_impl->dcInstance == nullptr) { return false; }
	m_impl->controller = std::make_unique<Host::DocumentController>(m_impl->dcInstance);
	m_documentController = m_impl->controller.get();
	auto* dc = m_documentController;

	dc->beginEditing();

	auto musicalCtxProps = makeProps<ARAMusicalContextProperties>();
	musicalCtxProps.name = "LMMS Context";
	musicalCtxProps.orderIndex = 0;
	m_musicalContext = dc->createMusicalContext(nullptr, &musicalCtxProps);

	auto regionSeqProps = makeProps<ARARegionSequenceProperties>();
	regionSeqProps.name = "LMMS Track";
	regionSeqProps.orderIndex = 0;
	regionSeqProps.musicalContextRef = m_musicalContext;
	m_regionSequence = dc->createRegionSequence(nullptr, &regionSeqProps);

	araDebugLog(QString("setup: %1 source(s), tempo=%2 %3/%4, analyzeableTypes=%5")
			.arg(sources.size()).arg(tempo).arg(timeSigNum).arg(timeSigDen)
			.arg(araFactory->analyzeableContentTypesCount));

	buildSources(sources);

	dc->endEditing();

	if (m_sources.empty()) { return false; }

	enableSourceSampleAccess();

	// --- bind the VST3 instance with all three ARA roles ---
	const ARAPlugInInstanceRoleFlags roles =
			kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole;
	const ARAPlugInExtensionInstance* ext = entryPoint->bindToDocumentControllerWithRoles(
			m_impl->dcInstance->documentControllerRef, roles, roles);
	if (ext == nullptr)
	{
		araDebugLog("bindToDocumentControllerWithRoles FAILED (null extension)");
		return false;
	}
	araDebugLog(QString("bound OK: %1 region(s), plugRef=%2 editRef=%3 viewRef=%4")
			.arg(m_regions.size())
			.arg(ext->playbackRendererRef != nullptr)
			.arg(ext->editorRendererRef != nullptr)
			.arg(ext->editorViewRef != nullptr));

	m_playbackRenderer = std::make_unique<Host::PlaybackRenderer>(ext);
	m_editorRenderer = std::make_unique<Host::EditorRenderer>(ext);
	m_editorView = std::make_unique<Host::EditorView>(ext);

	for (auto region : m_regions)
	{
		m_playbackRenderer->addPlaybackRegion(region);
		m_editorRenderer->addPlaybackRegion(region);
	}

	auto selection = makeProps<ARAViewSelection>();
	selection.playbackRegionRefsCount = m_regions.size();
	selection.playbackRegionRefs = m_regions.data();
	m_editorView->notifySelection(&selection);

	return true;
}




bool Vst3AraDocument::buildSources(const std::vector<Vst3Plugin::AraSource>& sources)
{
	auto* dc = m_documentController;
	if (dc == nullptr) { return false; }

	int index = 0;
	for (const auto& src : sources)
	{
		auto decoded = SampleDecoder::decode(src.file);
		if (!decoded || decoded->data.empty())
		{
			araDebugLog(QString("  decode FAILED: %1").arg(src.file));
			++index;
			continue;
		}
		araDebugLog(QString("  decoded %1: %2 frames @ %3 Hz, startSong=%4s dur=%5s off=%6s")
				.arg(src.file).arg(decoded->data.size()).arg(decoded->sampleRate)
				.arg(src.startInSongSeconds).arg(src.durationSeconds).arg(src.offsetInSourceSeconds));

		auto data = std::make_unique<AraAudioData>();
		data->sampleRate = decoded->sampleRate;
		data->frames = static_cast<ARASampleCount>(decoded->data.size());
		// unique persistent IDs across the whole document lifetime so that
		// re-created sources never collide with ones the plug-in is retiring
		data->persistentID = "lmms-src-" + std::to_string(m_sourceIdCounter);
		data->left.resize(decoded->data.size());
		data->right.resize(decoded->data.size());
		for (size_t i = 0; i < decoded->data.size(); ++i)
		{
			data->left[i] = decoded->data[i].left();
			data->right[i] = decoded->data[i].right();
		}
		AraAudioData* dataPtr = data.get();
		m_impl->audioData.push_back(std::move(data));

		SourceGraph g;

		auto sourceProps = makeProps<ARAAudioSourceProperties>();
		sourceProps.name = "LMMS Audio";
		sourceProps.persistentID = dataPtr->persistentID.c_str();
		sourceProps.sampleCount = dataPtr->frames;
		sourceProps.sampleRate = static_cast<ARASampleRate>(dataPtr->sampleRate);
		sourceProps.channelCount = 2;
		sourceProps.merits64BitSamples = kARAFalse;
		// the audio source's host ref points to its decoded data
		g.audioSource = dc->createAudioSource(
				reinterpret_cast<ARAAudioSourceHostRef>(dataPtr), &sourceProps);

		const std::string modID = "lmms-mod-" + std::to_string(m_sourceIdCounter);
		auto modProps = makeProps<ARAAudioModificationProperties>();
		modProps.name = "LMMS Modification";
		modProps.persistentID = modID.c_str();
		g.audioModification = dc->createAudioModification(g.audioSource, nullptr, &modProps);

		const double srcDuration = static_cast<double>(dataPtr->frames) / dataPtr->sampleRate;
		const double dur = src.durationSeconds > 0.0 ? src.durationSeconds : srcDuration;

		auto regionProps = makeProps<ARAPlaybackRegionProperties>();
		regionProps.transformationFlags = kARAPlaybackTransformationNoChanges;
		regionProps.startInModificationTime = src.offsetInSourceSeconds;
		regionProps.durationInModificationTime = dur;
		regionProps.startInPlaybackTime = src.startInSongSeconds;
		regionProps.durationInPlaybackTime = dur;
		regionProps.musicalContextRef = m_musicalContext;
		regionProps.regionSequenceRef = m_regionSequence;
		regionProps.name = "LMMS Region";
		g.playbackRegion = dc->createPlaybackRegion(g.audioModification, nullptr, &regionProps);

		m_sources.push_back(g);
		m_regions.push_back(g.playbackRegion);
		++index;
		++m_sourceIdCounter;
	}
	return !m_sources.empty();
}




void Vst3AraDocument::destroySources()
{
	auto* dc = m_documentController;
	if (dc == nullptr) { return; }
	for (auto it = m_sources.rbegin(); it != m_sources.rend(); ++it)
	{
		if (it->playbackRegion) { dc->destroyPlaybackRegion(it->playbackRegion); }
		if (it->audioModification) { dc->destroyAudioModification(it->audioModification); }
		if (it->audioSource)
		{
			dc->enableAudioSourceSamplesAccess(it->audioSource, false);
			dc->destroyAudioSource(it->audioSource);
		}
	}
	m_sources.clear();
	m_regions.clear();
	m_impl->audioData.clear();
}




void Vst3AraDocument::enableSourceSampleAccess()
{
	auto* dc = m_documentController;
	if (dc == nullptr) { return; }
	for (const auto& g : m_sources)
	{
		dc->enableAudioSourceSamplesAccess(g.audioSource, true);
		if (m_araFactory != nullptr && m_araFactory->analyzeableContentTypesCount > 0
				&& m_araFactory->analyzeableContentTypes != nullptr)
		{
			dc->requestAudioSourceContentAnalysis(g.audioSource,
					m_araFactory->analyzeableContentTypesCount,
					m_araFactory->analyzeableContentTypes);
		}
	}
}




bool Vst3AraDocument::updateRegions(const std::vector<Vst3Plugin::AraSource>& sources,
		double tempo, int timeSigNum, int timeSigDen)
{
	auto* dc = m_documentController;
	if (dc == nullptr || m_playbackRenderer == nullptr) { return false; }

	araDebugLog(QString("updateRegions: %1 source(s)").arg(sources.size()));

	m_impl->content.tempo = tempo;
	m_impl->content.timeSigNum = timeSigNum;
	m_impl->content.timeSigDen = timeSigDen;

	// detach the old regions from the renderers before they are destroyed
	for (auto region : m_regions)
	{
		m_editorRenderer->removePlaybackRegion(region);
		m_playbackRenderer->removePlaybackRegion(region);
	}

	// swap the document content for the new clip set, keeping the binding,
	// the musical context and the region sequence intact
	dc->beginEditing();
	destroySources();
	buildSources(sources);
	dc->endEditing();

	enableSourceSampleAccess();

	for (auto region : m_regions)
	{
		m_playbackRenderer->addPlaybackRegion(region);
		m_editorRenderer->addPlaybackRegion(region);
	}

	auto selection = makeProps<ARAViewSelection>();
	selection.playbackRegionRefsCount = m_regions.size();
	selection.playbackRegionRefs = m_regions.data();
	m_editorView->notifySelection(&selection);

	dc->notifyModelUpdates();
	return true;
}




void Vst3AraDocument::notifyModelUpdates()
{
	if (m_documentController != nullptr)
	{
		m_documentController->notifyModelUpdates();
	}
}




void Vst3AraDocument::teardown()
{
	m_editorView.reset();
	if (m_editorRenderer)
	{
		for (auto region : m_regions) { m_editorRenderer->removePlaybackRegion(region); }
	}
	m_editorRenderer.reset();
	if (m_playbackRenderer)
	{
		for (auto region : m_regions) { m_playbackRenderer->removePlaybackRegion(region); }
	}
	m_playbackRenderer.reset();

	if (m_documentController)
	{
		auto* dc = m_documentController;
		dc->beginEditing();
		for (auto it = m_sources.rbegin(); it != m_sources.rend(); ++it)
		{
			if (it->playbackRegion) { dc->destroyPlaybackRegion(it->playbackRegion); }
			if (it->audioModification) { dc->destroyAudioModification(it->audioModification); }
			if (it->audioSource)
			{
				dc->enableAudioSourceSamplesAccess(it->audioSource, false);
				dc->destroyAudioSource(it->audioSource);
			}
		}
		if (m_regionSequence) { dc->destroyRegionSequence(m_regionSequence); }
		if (m_musicalContext) { dc->destroyMusicalContext(m_musicalContext); }
		dc->endEditing();
		dc->destroyDocumentController();
	}

	m_sources.clear();
	m_regions.clear();
	m_regionSequence = nullptr;
	m_musicalContext = nullptr;
	m_documentController = nullptr;

	if (m_impl)
	{
		m_impl->controller.reset();
		m_impl->dcInstance = nullptr;
		m_impl->hostInstance.reset();
		m_impl->audioData.clear();
	}
}


} // namespace lmms

#endif // LMMS_HAVE_ARA
