/*
 * Copyright 2012-2016 Augustin Cavalier <waddlesplash>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "QMidiOut.h"

#include <MidiRoster.h>
#include <MidiConsumer.h>
#include <MidiProducer.h>

/* -----------------------------[ QMidiOut ]------------------------------ */

struct NativeMidiOutInstances {
	BMidiConsumer* midiOutConsumer;
	BMidiLocalProducer* midiOutLocProd;
};

// TODO: error reporting

QMap<QString, QString> QMidiOut::devices()
{
	QMap<QString, QString> ret;

	bool OK = true;
	int32 id = 0;
	while (OK) {
		BMidiConsumer* c = BMidiRoster::NextConsumer(&id);
		if (c != NULL) {
			ret.insert(QString::number(id), QString::fromUtf8(c->Name()));
			c->Release();
		} else {
			OK = false;
		}
	}

	return ret;
}

bool QMidiOut::connect(QString outDeviceId)
{
	if (fConnected)
		disconnect();
	fMidiPtrs = new NativeMidiOutInstances;

	fMidiPtrs->midiOutConsumer = BMidiRoster::FindConsumer(outDeviceId.toInt());
	if (fMidiPtrs->midiOutConsumer == NULL) {
		return false;
	}
	fMidiPtrs->midiOutLocProd = new BMidiLocalProducer("QMidi");
	if (!fMidiPtrs->midiOutLocProd->IsValid()) {
		fMidiPtrs->midiOutLocProd->Release();
		return false;
	}
	fMidiPtrs->midiOutLocProd->Register();
	if (fMidiPtrs->midiOutLocProd->Connect(fMidiPtrs->midiOutConsumer) != B_OK) {
		return false;
	}

	fDeviceId = outDeviceId;
	fConnected = true;
	return true;
}

void QMidiOut::disconnect()
{
	if (!fConnected)
		return;

	fMidiPtrs->midiOutLocProd->Disconnect(fMidiPtrs->midiOutConsumer);
	fMidiPtrs->midiOutConsumer->Release();
	fMidiPtrs->midiOutLocProd->Unregister();
	fMidiPtrs->midiOutLocProd->Release();
	fConnected = false;

	delete fMidiPtrs;
	fMidiPtrs = NULL;
}

void QMidiOut::sendMsg(qint32 msg)
{
	if (!fConnected)
		return;

	size_t bufferLength = 3;
	char buf[3];
	buf[0] = msg & 0xFF;
	buf[1] = (msg >> 8) & 0xFF;
	buf[2] = (msg >> 16) & 0xFF;

	if (buf[0] == '\xC0' || buf[0] == '\xD0')
	{
		// workaround for Haiku bug #15562
		bufferLength = 2;
	}

	fMidiPtrs->midiOutLocProd->SprayData((void*)&buf, bufferLength, true);
}

void QMidiOut::sendSysEx(const QByteArray &data)
{
    if (!fConnected)
        return;

    fMidiPtrs->midiOutLocProd->SprayData((char*)data.data(), data.length(), false);
}

/* ------------------------------[ QMidiIn ]------------------------------ */

#include "QMidiIn.h"
#include "OS/QMidi_Haiku.h"

struct NativeMidiInInstances {
    BMidiProducer* midiInProducer;
    MidiInConsumer* midiInConsumer;
};

QMap<QString, QString> QMidiIn::devices()
{
    QMap<QString, QString> ret;

    bool OK = true;
    int32 id = 0;
    while (OK) {
        BMidiProducer* c = BMidiRoster::NextProducer(&id);
        if (c != NULL) {
            ret.insert(QString::number(id), QString::fromUtf8(c->Name()));
            c->Release();
        } else {
            OK = false;
        }
    }

    return ret;
}

bool QMidiIn::connect(QString inDeviceId)
{
    if (fConnected)
    {
        disconnect();
    }

    fMidiPtrs = new NativeMidiInInstances;

    fMidiPtrs->midiInProducer = BMidiRoster::FindProducer(inDeviceId.toInt());
    if (fMidiPtrs->midiInProducer == NULL) {
        return false;
    }
    fMidiPtrs->midiInConsumer = new MidiInConsumer(this, "QMidi");
    if (!fMidiPtrs->midiInConsumer->IsValid()) {
        fMidiPtrs->midiInConsumer->Release();
        return false;
    }
    fMidiPtrs->midiInConsumer->Register();

    fDeviceId = inDeviceId;
    fConnected = true;

    return true;
}

void QMidiIn::disconnect()
{
    if (!fConnected)
    {
        return;
    }

    stop();

    fMidiPtrs->midiInConsumer->Release();
    fMidiPtrs->midiInConsumer->Unregister();
    fMidiPtrs->midiInProducer->Release();

    fConnected = false;
    delete fMidiPtrs;
    fMidiPtrs = NULL;
}

void QMidiIn::start()
{
    if (!fConnected)
    {
        return;
    }

    if (fMidiPtrs->midiInProducer->Connect(fMidiPtrs->midiInConsumer) != B_OK) {
        qWarning("QMidiIn::start: could not connect producer with our consumer");
        return;
    }
}

void QMidiIn::stop()
{
    if (!fConnected)
    {
        return;
    }

    fMidiPtrs->midiInProducer->Disconnect(fMidiPtrs->midiInConsumer);
}

MidiInConsumer::MidiInConsumer(QMidiIn* midiIn, const char* name)
    : BMidiLocalConsumer(name), fMidiIn(midiIn)
{
}

void MidiInConsumer::ChannelPressure(uchar channel, uchar pressure, bigtime_t time)
{
    int data = 0xD0
            | (channel & 0x0F)
            | (pressure << 8);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::ControlChange(uchar channel, uchar controlNumber, uchar controlValue, bigtime_t time)
{
    int data = 0xB0
            | (channel & 0x0F)
            | (controlNumber << 8)
            | (controlValue << 16);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::KeyPressure(uchar channel, uchar note, uchar pressure, bigtime_t time)
{
    int data = 0xA0
            | (channel & 0x0F)
            | (note << 8)
            | (pressure << 16);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
    int data = 0x80
            | (channel & 0x0F)
            | (note << 8)
            | (velocity << 16);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
    int data = 0x90
            | (channel & 0x0F)
            | (note << 8)
            | (velocity << 16);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::PitchBend(uchar channel, uchar lsb, uchar msb, bigtime_t time)
{
    int data = 0xE0
            | (channel & 0x0F)
            | (lsb << 8)
            | (msb << 16);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::ProgramChange(uchar channel, uchar programNumber, bigtime_t time)
{
    int data = 0xC0
            | (channel & 0x0F)
            | (programNumber << 8);
    emit(fMidiIn->midiEvent(static_cast<quint32>(data), time));
}

void MidiInConsumer::SystemExclusive(void* data, size_t length, bigtime_t time)
{
   QByteArray ba = QByteArray(reinterpret_cast<const char*>(data), length);
   ba.prepend('\xF0');
   ba.append('\xF7');
   emit(fMidiIn->midiSysExEvent(ba));
}
