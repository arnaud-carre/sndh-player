#pragma once
#include <stdint.h>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#endif

class	AudioStream
{
public:

	AudioStream();
	~AudioStream();

	bool Start(const int16_t* pcmBuffer, uint32_t sampleCount, uint32_t replayRate);
	bool SetPause(bool pause);
	uint32_t GetSpeakerPositionSample() const;
	bool SetPositionSample(uint32_t posSample);
	bool IsEndReached() const { return m_endReached.load(); }
	bool Stop();

	#ifdef _WIN32
	void WaveOutDoneCallback();
	#else
	bool InternalRenderCallback(AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timeStamp, UInt32 busNumber, UInt32 requestedFrames, AudioBufferList* ioData);
	#endif

private:
	const int16_t* m_pcmBuffer = nullptr;
	uint32_t m_pcmSampleCount = 0;;
	uint32_t m_replayRate = 0;
	uint32_t m_playOffsetSample = 0;
	std::atomic<bool> m_endReached = false;

#ifdef _WIN32
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
	std::atomic<bool> m_seeking;
#else
	static OSStatus RenderCallback(void* refCon, AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timeStamp, UInt32 busNumber, UInt32 requestedFrames, AudioBufferList* ioData);

	std::atomic<size_t> m_writePos;
	AudioUnit m_audioUnit;
#endif

};
