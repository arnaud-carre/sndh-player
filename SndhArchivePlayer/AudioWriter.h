#pragma once
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>

class AudioWriter
{
public:

	~AudioWriter();
	AudioWriter();

	bool			Open(int latencyBlockSizeInSample, uint32_t replayRate, bool stereo);
	int16_t*		GetAudioBlockToFillIfAny(int& sampleCountOut);
	uint32_t		GetPlayTime() const;
	const int16_t*	GetDisplayBuffer();
	void	Close();

private:
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
	bool		m_open;
	int16_t*	m_audioBuffer;
	int			m_blockCount;
	int			m_blockSampleCount;
	int			m_channelCount;
	uint32_t	m_replayRate;

	uint32_t	m_fillBlockId;
};