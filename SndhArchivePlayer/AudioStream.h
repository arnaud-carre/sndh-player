#pragma once
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
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

private:
	const int16_t* m_pcmBuffer;
	uint32_t m_pcmSampleCount;
	uint32_t m_replayRate;
	uint32_t m_playOffsetSample;

#ifdef _WIN32
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
#endif

};

