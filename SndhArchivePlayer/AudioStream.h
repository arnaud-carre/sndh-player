#pragma once
#include <stdint.h>

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
	bool Stop();

	#ifdef _WIN32
	#else
	bool InternalRenderCallback(AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timeStamp, UInt32 busNumber, UInt32 requestedFrames, AudioBufferList* ioData);
	#endif

private:
	const int16_t* m_pcmBuffer;
	uint32_t m_pcmSampleCount;
	uint32_t m_replayRate;
	uint32_t m_playOffsetSample;

#ifdef _WIN32
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
#else
	static OSStatus RenderCallback(void* refCon, AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timeStamp, UInt32 busNumber, UInt32 requestedFrames, AudioBufferList* ioData);

	std::atomic<size_t> m_writePos;
	AudioUnit m_audioUnit;
#endif

};

