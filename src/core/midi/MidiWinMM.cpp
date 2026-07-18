/*
 * MidiWinMM.cpp - WinMM MIDI client
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "MidiWinMM.h"

#include <vector>

#ifdef LMMS_BUILD_WIN32


namespace lmms
{


MidiWinMM::MidiWinMM() :
	MidiClient(),
	m_inputDevices(),
	m_outputDevices(),
	m_inputSubs(),
	m_outputSubs()
{
	openDevices();
}




MidiWinMM::~MidiWinMM()
{
	closeDevices();
}




void MidiWinMM::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	Q_UNUSED(time)
	const DWORD shortMsg = ( event.type() + event.channel() ) +
				( ( event.param( 0 ) & 0xff ) << 8 ) +
				( ( event.param( 1 ) & 0xff ) << 16 );

	// Collect the target handles under the lock, but call the driver outside
	// it: a software loopback driver (e.g. loopMIDI) may deliver the paired
	// input callback synchronously on this call stack, and handleInputEvent
	// locks the same non-recursive mutex. Worst case after unlocking is
	// midiOutShortMsg on a just-closed handle, which the driver rejects
	// harmlessly (closeDevices already calls midiOutClose outside the lock).
	std::vector<HMIDIOUT> handles;
	{
		QMutexLocker lock(&m_mutex);
		if (m_closing) { return; }
		QStringList outDevs;
		for( SubMap::ConstIterator it = m_outputSubs.begin(); it != m_outputSubs.end(); ++it )
		{
			for( MidiPortList::ConstIterator jt = it.value().begin(); jt != it.value().end(); ++jt )
			{
				if( *jt == port )
				{
					outDevs += it.key();
					break;
				}
			}
		}

		for( QMap<HMIDIOUT, QString>::Iterator it = m_outputDevices.begin(); it != m_outputDevices.end(); ++it )
		{
			if( outDevs.contains( *it ) )
			{
				handles.push_back( it.key() );
			}
		}
	}

	for( HMIDIOUT handle : handles )
	{
		midiOutShortMsg( handle, shortMsg );
	}
}




void MidiWinMM::applyPortMode( MidiPort* port )
{
	QMutexLocker lock(&m_mutex);
	// make sure no subscriptions exist which are not possible with
	// current port-mode
	if( !port->isInputEnabled() )
	{
		for( SubMap::Iterator it = m_inputSubs.begin(); it != m_inputSubs.end(); ++it )
		{
			it.value().removeAll( port );
		}
	}

	if( !port->isOutputEnabled() )
	{
		for( SubMap::Iterator it = m_outputSubs.begin(); it != m_outputSubs.end(); ++it )
		{
			it.value().removeAll( port );
		}
	}
}




void MidiWinMM::removePort( MidiPort* port )
{
	{
		QMutexLocker lock(&m_mutex);
		for( SubMap::Iterator it = m_inputSubs.begin(); it != m_inputSubs.end(); ++it )
		{
			it.value().removeAll( port );
		}

		for( SubMap::Iterator it = m_outputSubs.begin(); it != m_outputSubs.end(); ++it )
		{
			it.value().removeAll( port );
		}
	}

	MidiClient::removePort( port );
}




QString MidiWinMM::sourcePortName( const MidiEvent& event ) const
{
	QMutexLocker lock(&m_mutex);
	if( event.sourcePort() )
	{
		return m_inputDevices.value( *static_cast<const HMIDIIN *>( event.sourcePort() ) );
	}

	return MidiClient::sourcePortName( event );
}




void MidiWinMM::subscribeReadablePort( MidiPort* port, const QString& dest, bool subscribe )
{
	QMutexLocker lock(&m_mutex);
	if( subscribe && port->isInputEnabled() == false )
	{
		qWarning( "port %s can't be (un)subscribed!\n", port->displayName().toLatin1().constData() );
		return;
	}

	m_inputSubs[dest].removeAll( port );
	if( subscribe )
	{
		m_inputSubs[dest].push_back( port );
	}
}




void MidiWinMM::subscribeWritablePort( MidiPort* port, const QString& dest, bool subscribe )
{
	QMutexLocker lock(&m_mutex);
	if( subscribe && port->isOutputEnabled() == false )
	{
		qWarning( "port %s can't be (un)subscribed!\n", port->displayName().toLatin1().constData() );
		return;
	}

	m_outputSubs[dest].removeAll( port );
	if( subscribe )
	{
		m_outputSubs[dest].push_back( port );
	}
}




void WINAPI CALLBACK MidiWinMM::inputCallback( HMIDIIN hm, UINT msg, DWORD_PTR inst, DWORD_PTR param1, DWORD_PTR param2 )
{
	if( msg == MIM_DATA )
	{
		( (MidiWinMM *) inst )->handleInputEvent( hm, param1 );
	}
}




void MidiWinMM::handleInputEvent( HMIDIIN hm, DWORD ev )
{
	const int cmd = ev & 0xff;
	if( cmd == MidiActiveSensing )
	{
		return;
	}
	const int par1 = ( ev >> 8 ) & 0xff;
	const int par2 = ev >> 16;
	const MidiEventTypes cmdtype = static_cast<MidiEventTypes>( cmd & 0xf0 );
	const int chan = cmd & 0x0f;

	QMutexLocker lock(&m_mutex);
	if (m_closing) { return; }
	const QString d = m_inputDevices.value( hm );
	if( d.isEmpty() || !m_inputSubs.contains( d ) )
	{
		return;
	}

	const MidiPortList& ports = m_inputSubs[d];
	for (MidiPort* port : ports)
	{
		// Attach the delivery to the MidiPort QObject while the subscription
		// lock prevents its destructor from unregistering. Qt then cancels the
		// queued call automatically if the track is deleted before delivery.
		QMetaObject::invokeMethod(port, [port, hm, cmdtype, chan, par1, par2] {
			HMIDIIN source = hm;
			switch (cmdtype)
			{
				case MidiNoteOn:
				case MidiNoteOff:
				case MidiKeyPressure:
				case MidiControlChange:
				case MidiProgramChange:
				case MidiChannelPressure:
					port->processInEvent(MidiEvent(cmdtype, chan, par1, par2 & 0xff, &source));
					break;
				case MidiPitchBend:
					port->processInEvent(MidiEvent(cmdtype, chan, par1 + par2 * 128, 0, &source));
					break;
				default:
					break;
			}
		}, Qt::QueuedConnection);
	}
}


QStringList MidiWinMM::readablePorts() const
{
	QMutexLocker lock(&m_mutex);
	return m_inputDevices.values();
}


QStringList MidiWinMM::writablePorts() const
{
	QMutexLocker lock(&m_mutex);
	return m_outputDevices.values();
}




void MidiWinMM::updateDeviceList()
{
	closeDevices();
	openDevices();

	emit readablePortsChanged();
	emit writablePortsChanged();
}



void MidiWinMM::closeDevices()
{
	QMap<HMIDIIN, QString> inputDevices;
	QMap<HMIDIOUT, QString> outputDevices;
	{
		QMutexLocker lock(&m_mutex);
		m_closing = true;
		m_inputSubs.clear();
		m_outputSubs.clear();
		inputDevices = m_inputDevices;
		outputDevices = m_outputDevices;
	}

	QMapIterator<HMIDIIN, QString> i(inputDevices);

	HMIDIIN hInDev;
	while( i.hasNext() )
	{
		hInDev = i.next().key();
		midiInReset( hInDev );
		midiInClose( hInDev );
	}

	QMapIterator<HMIDIOUT, QString> o(outputDevices);
	while( o.hasNext() )
	{
		midiOutClose( o.next().key() );
	}

	QMutexLocker lock(&m_mutex);
	m_inputDevices.clear();
	m_outputDevices.clear();
}




void MidiWinMM::openDevices()
{
	{
		QMutexLocker lock(&m_mutex);
		m_inputDevices.clear();
		m_outputDevices.clear();
		m_closing = false;
	}
	for( unsigned int i = 0; i < midiInGetNumDevs(); ++i )
	{
		MIDIINCAPSW c;
		midiInGetDevCapsW(i, &c, sizeof(c));
		HMIDIIN hm = 0;
		MMRESULT res = midiInOpen( &hm, i, (DWORD_PTR) &inputCallback,
						(DWORD_PTR) this,
							CALLBACK_FUNCTION );
		if( res == MMSYSERR_NOERROR )
		{
			{
				QMutexLocker lock(&m_mutex);
				m_inputDevices[hm] = QString::fromWCharArray(c.szPname);
			}
			midiInStart( hm );
		}
	}

	for( unsigned int i = 0; i < midiOutGetNumDevs(); ++i )
	{
		MIDIOUTCAPSW c;
		midiOutGetDevCapsW(i, &c, sizeof(c));
		HMIDIOUT hm = 0;
		MMRESULT res = midiOutOpen( &hm, i, 0, 0, CALLBACK_NULL );
		if( res == MMSYSERR_NOERROR )
		{
			QMutexLocker lock(&m_mutex);
			m_outputDevices[hm] = QString::fromWCharArray(c.szPname);
		}
	}
}


} // namespace lmms

#endif // LMMS_BUILD_WIN32
