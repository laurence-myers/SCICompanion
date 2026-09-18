/***************************************************************************
	Copyright (c) 2020 Philip Fortier

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
***************************************************************************/
#include "stdafx.h"
#include "AudioPlayback.h"
#include "Audio.h"

AudioPlayback::AudioPlayback() : hWaveOut(nullptr), waveHeader(), _sound(nullptr)
{

}

AudioPlayback::~AudioPlayback()
{
	Cleanup();
}

void AudioPlayback::Cleanup()
{
	if (hWaveOut)
	{
		// Stop the device first. While it still plays the buffer,
		// waveOutUnprepareHeader fails with WAVERR_STILLPLAYING and the header
		// stays prepared (#72).
		waveOutReset(hWaveOut);
		waveOutUnprepareHeader(hWaveOut, &waveHeader, sizeof(waveHeader));
		waveOutClose(hWaveOut);
		hWaveOut = nullptr;
		memset(&waveHeader, 0, sizeof(waveHeader)); // Just for good measure
		// The device no longer reads the buffer, so we can free our copy.
		_playbackBuffer.clear();
		_playbackBuffer.shrink_to_fit();
	}
}

DWORD AudioPlayback::QueryPosition(DWORD scope)
{
	DWORD pos = 0;
	if (hWaveOut && !_playbackBuffer.empty())
	{
		MMTIME mmTime = {};
		mmTime.wType = TIME_BYTES;
		if (MMSYSERR_NOERROR == waveOutGetPosition(hWaveOut, &mmTime, sizeof(mmTime)))
		{
			pos = (scope * mmTime.u.cb / _playbackBuffer.size());
		}
	}
	return pos;
}

uint32_t AudioPlayback::QueryStreamPosition()
{
	uint32_t pos = 0;
	if (hWaveOut && !_playbackBuffer.empty())
	{
		MMTIME mmTime = {};
		mmTime.wType = TIME_BYTES;
		if (MMSYSERR_NOERROR == waveOutGetPosition(hWaveOut, &mmTime, sizeof(mmTime)))
		{
			pos = mmTime.u.cb;
		}
	}
	return pos;
}


void AudioPlayback::SetAudio(const AudioComponent *sound)
{
	_sound = sound;
}

bool AudioPlayback::IsPlaying()
{
	return hWaveOut != nullptr;
}

void AudioPlayback::IdleUpdate()
{
	if (hWaveOut)
	{
		// If we're finished playing, then close
		if (waveHeader.dwFlags & WHDR_DONE)
		{
			Cleanup();
		}
	}
}

void AudioPlayback::Stop()
{
	Cleanup();
}

void AudioPlayback::Play(int slowDown)
{
	if (hWaveOut)
	{
		// If we're finished playing, then close
		if (waveHeader.dwFlags & WHDR_DONE)
		{
			Cleanup();
		}
		else
		{
			// We're busy.
			return;
		}
	}

	if (_sound && !_sound->DigitalSamplePCM.empty())
	{
		uint16_t freq = _sound->Frequency;
		freq /= slowDown;

		WORD blockAlign = IsFlagSet(_sound->Flags, AudioFlags::SixteenBit) ? 2 : 1;
		WAVEFORMATEX waveFormat = { 0 };
		waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		waveFormat.nChannels = 1;   // mono
		waveFormat.nSamplesPerSec = freq;
		waveFormat.nAvgBytesPerSec = freq * blockAlign;
		waveFormat.nBlockAlign = blockAlign;
		waveFormat.wBitsPerSample = 8 * blockAlign;
		waveFormat.cbSize = 0;

		// WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE is only available on win7, so go without it
		MMRESULT result = waveOutOpen(&hWaveOut, WAVE_MAPPER, &waveFormat, 0, 0, CALLBACK_NULL);
		if (result == MMSYSERR_NOERROR)
		{
			// The device reads the buffer asynchronously for as long as it plays,
			// and the caller can delete or replace _sound during that time. Play
			// from our own copy, which Cleanup frees after waveOutReset (#72).
			_playbackBuffer = _sound->DigitalSamplePCM;
			waveHeader.lpData = reinterpret_cast<LPSTR>(_playbackBuffer.data());
			waveHeader.dwBufferLength = (DWORD)_playbackBuffer.size();
			waveHeader.dwLoops = 10;
			result = waveOutPrepareHeader(hWaveOut, &waveHeader, sizeof(waveHeader));
			if (result == MMSYSERR_NOERROR)
			{
				result = waveOutWrite(hWaveOut, &waveHeader, sizeof(waveHeader));
			}
			if (result != MMSYSERR_NOERROR)
			{
				// The device is open but nothing is queued: without this the header
				// never reaches WHDR_DONE, IsPlaying() stays true and IdleUpdate never
				// closes the device.
				Cleanup();
			}
		}
	}
}
