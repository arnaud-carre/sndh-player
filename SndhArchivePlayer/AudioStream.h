//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#pragma once
#include <stdint.h>
#include <cstddef>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#elif defined(__linux__)
#include <SDL.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#else
#error Unsupported audio platform
#endif

class	AudioStream
{
public:

	~AudioStream();

	bool Start(const int16_t* pcmBuffer, uint32_t sampleCount, uint32_t replayRate);
	bool SetPause(bool pause);
	uint32_t GetSpeakerPositionSample() const;
	bool SetPositionSample(uint32_t posSample);
	bool IsEndReached() const { return m_endReached.load(); }
	bool Stop();

	#ifdef _WIN32
	void WaveOutDoneCallback();
	#elif defined(__linux__)
	void RenderLinuxAudio(uint8_t* output, int byteCount);
	#elif defined(__APPLE__)
	bool InternalRenderCallback(AudioUnitRenderActionFlags* actionFlags, UInt32 requestedFrames, AudioBufferList* ioData);
	#endif

private:
	const int16_t* m_pcmBuffer = nullptr;
	uint32_t m_pcmSampleCount = 0;
	std::atomic<bool> m_endReached = false;

#ifdef _WIN32
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
	uint32_t m_playOffsetSample = 0;
	std::atomic<bool> m_seeking = false;
#elif defined(__linux__)
	SDL_AudioDeviceID m_audioDevice = 0;
	bool m_audioInitialized = false;
	std::atomic<size_t> m_writePos = 0;
#elif defined(__APPLE__)
	static OSStatus RenderCallback(void* refCon, AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timeStamp, UInt32 busNumber, UInt32 requestedFrames, AudioBufferList* ioData);
	AudioUnit m_audioUnit;
	std::atomic<size_t> m_writePos = 0;
#endif

};
