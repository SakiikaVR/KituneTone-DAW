/*
 * AudioEngineWorkerThread.cpp - implementation of AudioEngineWorkerThread
 *
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioEngineWorkerThread.h"

#include <QDebug>
#include <QMutex>
#include <QWaitCondition>

#include "AudioEngine.h"
#include "Hardware.h"
#include "ThreadableJob.h"


namespace lmms
{

AudioEngineWorkerThread::JobQueue AudioEngineWorkerThread::globalJobQueue;
QWaitCondition * AudioEngineWorkerThread::queueReadyWaitCond = nullptr;
QMutex AudioEngineWorkerThread::queueReadyMutex;
QList<AudioEngineWorkerThread *> AudioEngineWorkerThread::workerThreads;
std::atomic_size_t AudioEngineWorkerThread::queueGeneration{0};
std::atomic_size_t AudioEngineWorkerThread::cycleAcknowledgements{0};

// implementation of internal JobQueue
void AudioEngineWorkerThread::JobQueue::reset( OperationMode _opMode )
{
	m_writeIndex = 0;
	m_itemsDone = 0;
	m_opMode.store(_opMode, std::memory_order_release);
}




void AudioEngineWorkerThread::JobQueue::addJob( ThreadableJob * _job )
{
	if( _job->requiresProcessing() )
	{
		// update job state
		_job->queue();
		// actually queue the job via atomic operations
		auto index = m_writeIndex++;
		if (index < JOB_QUEUE_SIZE) {
			m_items[index] = _job;
		} else {
			qWarning() << "Job queue is full!";
			++m_itemsDone;
		}
	}
}



void AudioEngineWorkerThread::JobQueue::run()
{
	bool processedJob = true;
	while (processedJob && m_itemsDone < m_writeIndex)
	{
		processedJob = false;
		for (auto i = std::size_t{0}; i < m_writeIndex && i < JOB_QUEUE_SIZE; ++i)
		{
			ThreadableJob * job = m_items[i].exchange(nullptr);
			if( job )
			{
				try { job->process(); }
				catch (...) { qWarning() << "Audio worker job threw an exception"; }
				processedJob = true;
				++m_itemsDone;
			}
		}
		// always exit loop if we're not in dynamic mode
		processedJob = processedJob
				&& (m_opMode.load(std::memory_order_acquire) == OperationMode::Dynamic);
	}
}




void AudioEngineWorkerThread::JobQueue::wait()
{
	while (m_itemsDone < m_writeIndex) { busyWaitHint(); }
}





// implementation of worker threads

AudioEngineWorkerThread::AudioEngineWorkerThread(AudioEngine* audioEngine, bool startedWorker) :
	QThread( audioEngine ),
	m_quit(false),
	m_cycleActive(false),
	m_threadReady(false),
	m_seenGeneration(0),
	m_startedWorker(startedWorker)
{
	// initialize global static data
	if( queueReadyWaitCond == nullptr )
	{
		queueReadyWaitCond = new QWaitCondition;
	}

	// keep track of all instantiated worker threads - this is used for
	// processing the last worker thread "inline", see comments in
	// AudioEngineWorkerThread::startAndWaitForJobs() for details
	workerThreads << this;

	resetJobQueue();
}




AudioEngineWorkerThread::~AudioEngineWorkerThread()
{
	workerThreads.removeAll( this );
}




void AudioEngineWorkerThread::quit()
{
	// The quit predicate and wake-up must be changed under the same mutex
	// used by run().  Otherwise a worker can observe false, miss wakeAll(),
	// and remain blocked while AudioEngine is being destroyed.
	QMutexLocker lock(&queueReadyMutex);
	m_quit.store(true, std::memory_order_release);
	queueReadyWaitCond->wakeAll();
}




void AudioEngineWorkerThread::resetJobQueue(JobQueue::OperationMode opMode)
{
	// A runner can finish the final job slightly before it returns from run().
	// Never reset counters/items until every worker has left the previous cycle.
	for (AudioEngineWorkerThread* worker : workerThreads)
	{
		while (worker->m_cycleActive.load(std::memory_order_acquire)) { busyWaitHint(); }
	}
	globalJobQueue.reset(opMode);
}




void AudioEngineWorkerThread::startAndWaitForJobs()
{
	std::size_t workersExpected = 0;
	{
		QMutexLocker lock(&queueReadyMutex);
		for (const auto* worker : workerThreads)
		{
			// A QThread can fail to start when the process or system is under
			// resource pressure.  Only wait for threads which have actually
			// entered run(); the inline runner still processes every queued job.
			if (worker->m_startedWorker
					&& worker->m_threadReady.load(std::memory_order_acquire))
			{
				++workersExpected;
			}
		}
		cycleAcknowledgements.store(0, std::memory_order_release);
		queueGeneration.fetch_add(1, std::memory_order_acq_rel);
		queueReadyWaitCond->wakeAll();
	}
	// The last worker-thread is never started. Instead it's processed "inline"
	// i.e. within the global AudioEngine thread. This way we can reduce latencies
	// that otherwise would be caused by synchronizing with another thread.
	globalJobQueue.run();
	globalJobQueue.wait();
	while (cycleAcknowledgements.load(std::memory_order_acquire) < workersExpected)
	{
		busyWaitHint();
	}
	for (AudioEngineWorkerThread* worker : workerThreads)
	{
		if (!worker->m_startedWorker
				|| !worker->m_threadReady.load(std::memory_order_acquire))
		{
			continue;
		}
		while (worker->m_cycleActive.load(std::memory_order_acquire)) { busyWaitHint(); }
	}
}




void AudioEngineWorkerThread::run()
{
	disableDenormals();

	QMutexLocker lock(&queueReadyMutex);
	// Synchronize readiness with startAndWaitForJobs().  If this thread starts
	// after a cycle was announced, regard that generation as already seen so it
	// cannot join an uncounted cycle and outlive the caller's barrier.
	m_seenGeneration = queueGeneration.load(std::memory_order_acquire);
	m_threadReady.store(true, std::memory_order_release);
	while (!m_quit.load(std::memory_order_acquire))
	{
		while (!m_quit.load(std::memory_order_acquire)
				&& m_seenGeneration == queueGeneration.load(std::memory_order_acquire))
		{
			queueReadyWaitCond->wait(&queueReadyMutex);
		}
		if (m_seenGeneration != queueGeneration.load(std::memory_order_acquire))
		{
			// A new cycle was announced; startAndWaitForJobs() has counted this
			// thread and spins on the acknowledgement barrier. Acknowledge even
			// when quitting, or that spin never completes and shutdown hangs.
			m_seenGeneration = queueGeneration.load(std::memory_order_acquire);
			m_cycleActive.store(true, std::memory_order_release);
			cycleAcknowledgements.fetch_add(1, std::memory_order_acq_rel);
			if (m_quit.load(std::memory_order_acquire))
			{
				// Skipping the job run is safe: the inline runner in
				// startAndWaitForJobs() drains the queue regardless.
				m_cycleActive.store(false, std::memory_order_release);
				break;
			}
			lock.unlock();
			globalJobQueue.run();
			m_cycleActive.store(false, std::memory_order_release);
			lock.relock();
		}
		else if (m_quit.load(std::memory_order_acquire)) { break; }
	}
	m_threadReady.store(false, std::memory_order_release);
}

} // namespace lmms
