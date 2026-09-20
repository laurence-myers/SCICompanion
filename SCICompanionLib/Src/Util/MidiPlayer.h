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
#pragma once

#include "Sound.h"
#include <atomic>

class MidiPlayer
{
public:
	MidiPlayer();
	~MidiPlayer();
	DWORD SetSound(const SoundComponent &sound, uint16_t wTempo);
	void Play();
	void Pause();
	void Stop();
	bool CanPlay();
	bool CanPause();
	bool CanStop();
	DWORD QueryPosition(DWORD scope);
	DWORD QueryPosition();
	void SetTempo(uint16_t wTempo) { _wTempo = wTempo; _SetTempoAndDivision(); }
	void SetDevice(DeviceType device) { _device = device; }
	void CueTickPosition(DWORD dwTicks);
	void CuePosition(DWORD dwTicks, DWORD scope);
	bool IsPlaying() { return _fPlaying; }
	void Reset();

private:
	void _Reset();
	bool _Init();
	void _SetTempoAndDivision();
	void _ClearHeaders();
	void static CALLBACK s_MidiOutProc(HMIDIOUT hmo, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
	// The MM_MOM_DONE callback posts to this message-only window so the follow-up
	// MMSYSTEM work runs on the UI thread, not inside the driver callback (which
	// forbids multimedia calls and can deadlock). See #49.
	static LRESULT CALLBACK s_NotifyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	bool _EnsureNotifyWindow();
	void _OnStreamDone(DWORD generation);
	void _CuePosition(DWORD dwEventIndex, DWORD ticks = 0xffffffff);

	HWND _hNotifyWnd;
	HMIDISTRM _handle;
	MIDIHDR _midiHdr;
	DWORD _cRemainingStreamEvents; // In case it didn't fit into a 64k chunk
	DWORD _cTotalStreamEvents;
	DWORD *_pRealData; // Full data for the 64k chunk in _midiHdr.
	std::vector<DWORD> _accumulatedStreamTicks;   // Corresponds to _midiHdr.lpData / 3
	DWORD _dwCurrentChunkTickStart;
	DWORD _dwCurrentTickPos;
	DeviceType _device;
	bool _fPlaying;
	bool _fQueuedUp;
	// Set true only around the deliberate midiOutReset calls, which make the driver
	// re-report the flushed buffer via MM_MOM_DONE. The callback reads this (on the
	// driver thread) to suppress that reset-generated notification, so it must be
	// atomic. (#49)
	std::atomic<bool> _fStoppingStream;
	// Bumped each time a chunk is cued or playback stops/seeks. The MM_MOM_DONE
	// callback stamps the current value into the posted message; _OnStreamDone
	// ignores a notification whose stamp no longer matches, so a genuine
	// chunk-end that was overtaken by a Stop/seek before the UI pump handled it
	// cannot re-cue against the changed state. Read on the driver thread, so it
	// is atomic. (#113)
	std::atomic<DWORD> _streamGeneration;
	DWORD _wTotalTime;
	uint16_t _wTempo;
	uint16_t _wTimeDivision;
	DWORD _dwLoopPoint;
	DWORD _dwCookie;

	std::unique_ptr<SoundComponent> _soundCache;
	uint16_t _tempoCache;
};

extern MidiPlayer g_midiPlayer;
