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

#include <QDebug>

#include <cstring>
#include <set>
#include <string>

#include "SampleDecoder.h"

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

} // anonymous namespace




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

//! Serves decoded audio samples to the plug-in.
class AraAudioAccessController : public Host::AudioAccessControllerInterface
{
public:
	std::vector<float> left;
	std::vector<float> right;

	struct Reader { bool use64; };

	ARAAudioReaderHostRef createAudioReaderForSource(
			ARAAudioSourceHostRef, bool use64BitSamples) noexcept override
	{
		auto reader = new Reader{use64BitSamples};
		return reinterpret_cast<ARAAudioReaderHostRef>(reader);
	}

	bool readAudioSamples(ARAAudioReaderHostRef readerRef, ARASamplePosition samplePosition,
			ARASampleCount samplesPerChannel, void* const buffers[]) noexcept override
	{
		auto reader = reinterpret_cast<Reader*>(readerRef);
		const auto total = static_cast<ARASampleCount>(left.size());
		for (ARASampleCount i = 0; i < samplesPerChannel; ++i)
		{
			const ARASamplePosition pos = samplePosition + i;
			const bool valid = pos >= 0 && pos < total;
			const float l = valid ? left[static_cast<size_t>(pos)] : 0.f;
			const float r = valid ? right[static_cast<size_t>(pos)] : 0.f;
			if (reader != nullptr && reader->use64)
			{
				static_cast<double*>(buffers[0])[i] = l;
				static_cast<double*>(buffers[1])[i] = r;
			}
			else
			{
				static_cast<float*>(buffers[0])[i] = l;
				static_cast<float*>(buffers[1])[i] = r;
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


//! Minimal content access controller (no musical context content provided).
class AraContentAccessController : public Host::ContentAccessControllerInterface
{
public:
	bool isMusicalContextContentAvailable(ARAMusicalContextHostRef, ARAContentType) noexcept override
	{ return false; }
	ARAContentGrade getMusicalContextContentGrade(ARAMusicalContextHostRef, ARAContentType) noexcept override
	{ return kARAContentGradeInitial; }
	ARAContentReaderHostRef createMusicalContextContentReader(ARAMusicalContextHostRef, ARAContentType,
			const ARAContentTimeRange*) noexcept override { return nullptr; }
	bool isAudioSourceContentAvailable(ARAAudioSourceHostRef, ARAContentType) noexcept override
	{ return false; }
	ARAContentGrade getAudioSourceContentGrade(ARAAudioSourceHostRef, ARAContentType) noexcept override
	{ return kARAContentGradeInitial; }
	ARAContentReaderHostRef createAudioSourceContentReader(ARAAudioSourceHostRef, ARAContentType,
			const ARAContentTimeRange*) noexcept override { return nullptr; }
	ARAInt32 getContentReaderEventCount(ARAContentReaderHostRef) noexcept override { return 0; }
	const void* getContentReaderDataForEvent(ARAContentReaderHostRef, ARAInt32) noexcept override
	{ return nullptr; }
	void destroyContentReader(ARAContentReaderHostRef) noexcept override {}
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


//! Playback transport requests from the plug-in - not wired to LMMS yet.
class AraPlaybackController : public Host::PlaybackControllerInterface
{
public:
	void requestStartPlayback() noexcept override {}
	void requestStopPlayback() noexcept override {}
	void requestSetPlaybackPosition(ARATimePosition) noexcept override {}
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
		const QString& file, double startInSongSeconds, double durationSeconds)
{
	if (araFactory == nullptr || entryPoint == nullptr) { return false; }

	// decode the whole audio file into memory
	auto decoded = SampleDecoder::decode(file);
	if (!decoded || decoded->data.empty()) { return false; }

	const auto frameCount = static_cast<ARASampleCount>(decoded->data.size());
	m_impl->audioAccess.left.resize(decoded->data.size());
	m_impl->audioAccess.right.resize(decoded->data.size());
	for (size_t i = 0; i < decoded->data.size(); ++i)
	{
		m_impl->audioAccess.left[i] = decoded->data[i].left();
		m_impl->audioAccess.right[i] = decoded->data[i].right();
	}

	araEnsureFactoryInitialized(araFactory);

	// host instance carrying our controller interfaces
	m_impl->hostInstance = std::make_unique<Host::DocumentControllerHostInstance>(
			&m_impl->audioAccess, &m_impl->archiving, &m_impl->content,
			&m_impl->modelUpdate, &m_impl->playback);

	// create the document controller
	auto docProps = makeProps<ARADocumentProperties>();
	docProps.name = "LMMS ARA Document";
	m_impl->dcInstance = araFactory->createDocumentControllerWithDocument(
			m_impl->hostInstance.get(), &docProps);
	if (m_impl->dcInstance == nullptr) { return false; }
	m_impl->controller = std::make_unique<Host::DocumentController>(m_impl->dcInstance);
	m_documentController = m_impl->controller.get();

	auto* dc = m_documentController;

	// --- build the model graph ---
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

	auto sourceProps = makeProps<ARAAudioSourceProperties>();
	sourceProps.name = "LMMS Audio";
	sourceProps.persistentID = "lmms-audio-source-1";
	sourceProps.sampleCount = frameCount;
	sourceProps.sampleRate = static_cast<ARASampleRate>(decoded->sampleRate);
	sourceProps.channelCount = 2;
	sourceProps.merits64BitSamples = kARAFalse;
	m_audioSource = dc->createAudioSource(nullptr, &sourceProps);

	auto modProps = makeProps<ARAAudioModificationProperties>();
	modProps.name = "LMMS Modification";
	modProps.persistentID = "lmms-audio-mod-1";
	m_audioModification = dc->createAudioModification(m_audioSource, nullptr, &modProps);

	const double srcDuration = static_cast<double>(frameCount) / decoded->sampleRate;
	const double dur = durationSeconds > 0.0 ? durationSeconds : srcDuration;

	auto regionProps = makeProps<ARAPlaybackRegionProperties>();
	regionProps.transformationFlags = kARAPlaybackTransformationNoChanges;
	regionProps.startInModificationTime = 0.0;
	regionProps.durationInModificationTime = dur;
	regionProps.startInPlaybackTime = startInSongSeconds;
	regionProps.durationInPlaybackTime = dur;
	regionProps.musicalContextRef = m_musicalContext;
	regionProps.regionSequenceRef = m_regionSequence;
	regionProps.name = "LMMS Region";
	m_playbackRegion = dc->createPlaybackRegion(m_audioModification, nullptr, &regionProps);

	dc->endEditing();

	// allow the plug-in to read the audio source samples
	dc->enableAudioSourceSamplesAccess(m_audioSource, true);

	// ask the plug-in to analyse the audio so its editor shows notes/waveform
	if (araFactory->analyzeableContentTypesCount > 0
			&& araFactory->analyzeableContentTypes != nullptr)
	{
		dc->requestAudioSourceContentAnalysis(m_audioSource,
				araFactory->analyzeableContentTypesCount,
				araFactory->analyzeableContentTypes);
	}

	// --- bind the VST3 instance with all three ARA roles ---
	const ARAPlugInInstanceRoleFlags knownRoles =
			kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole;
	const ARAPlugInInstanceRoleFlags assignedRoles =
			kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole;

	const ARAPlugInExtensionInstance* ext = entryPoint->bindToDocumentControllerWithRoles(
			m_impl->dcInstance->documentControllerRef, knownRoles, assignedRoles);
	if (ext == nullptr)
	{
		qWarning() << "Vst3AraDocument: bindToDocumentControllerWithRoles failed";
		return false;
	}

	// playback renderer: produces the modified audio during playback
	m_playbackRenderer = std::make_unique<Host::PlaybackRenderer>(ext);
	m_playbackRenderer->addPlaybackRegion(m_playbackRegion);

	// editor renderer: previews audio while the user edits in the plug-in UI
	m_editorRenderer = std::make_unique<Host::EditorRenderer>(ext);
	m_editorRenderer->addPlaybackRegion(m_playbackRegion);

	// editor view: tell the plug-in which region is selected so it displays it
	m_editorView = std::make_unique<Host::EditorView>(ext);
	auto selection = makeProps<ARAViewSelection>();
	selection.playbackRegionRefsCount = 1;
	selection.playbackRegionRefs = &m_playbackRegion;
	selection.regionSequenceRefsCount = 0;
	selection.regionSequenceRefs = nullptr;
	selection.timeRange = nullptr;
	m_editorView->notifySelection(&selection);

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
	if (m_editorView) { m_editorView.reset(); }
	if (m_editorRenderer && m_playbackRegion)
	{
		m_editorRenderer->removePlaybackRegion(m_playbackRegion);
	}
	m_editorRenderer.reset();
	if (m_playbackRenderer && m_playbackRegion)
	{
		m_playbackRenderer->removePlaybackRegion(m_playbackRegion);
	}
	m_playbackRenderer.reset();

	if (m_documentController)
	{
		auto* dc = m_documentController;
		dc->beginEditing();
		if (m_playbackRegion) { dc->destroyPlaybackRegion(m_playbackRegion); }
		if (m_audioModification) { dc->destroyAudioModification(m_audioModification); }
		if (m_audioSource)
		{
			dc->enableAudioSourceSamplesAccess(m_audioSource, false);
			dc->destroyAudioSource(m_audioSource);
		}
		if (m_regionSequence) { dc->destroyRegionSequence(m_regionSequence); }
		if (m_musicalContext) { dc->destroyMusicalContext(m_musicalContext); }
		dc->endEditing();
		dc->destroyDocumentController();
	}

	m_playbackRegion = nullptr;
	m_audioModification = nullptr;
	m_audioSource = nullptr;
	m_regionSequence = nullptr;
	m_musicalContext = nullptr;
	m_documentController = nullptr;

	if (m_impl)
	{
		m_impl->controller.reset();
		m_impl->dcInstance = nullptr;
		m_impl->hostInstance.reset();
	}
}


} // namespace lmms

#endif // LMMS_HAVE_ARA
