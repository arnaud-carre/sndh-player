#include "AudioWriter.h"
#include <assert.h>

#pragma	comment(lib,"winmm.lib")

AudioWriter::AudioWriter()
{
	m_audioBuffer = NULL;
	m_open = false;
}

AudioWriter::~AudioWriter()
{
	Close();
}

void	AudioWriter::Close()
{
	if (m_open)
	{
		waveOutUnprepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
		waveOutReset(m_waveOutHandle);
		waveOutClose(m_waveOutHandle);
		m_open = false;
	}

	if (m_audioBuffer)
	{
		free(m_audioBuffer);
		m_audioBuffer = NULL;
	}
}

bool	AudioWriter::Open(int latencyBlockSizeInSample, uint32_t replayRate, bool stereo)
{

	WAVEFORMATEX	pcmwf;
	pcmwf.wFormatTag = WAVE_FORMAT_PCM;
	pcmwf.nChannels = stereo?2:1;
	pcmwf.wBitsPerSample = 16;
	pcmwf.nBlockAlign = pcmwf.nChannels * pcmwf.wBitsPerSample / 8;
	pcmwf.nSamplesPerSec = replayRate;
	pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.cbSize = 0;
	m_blockSampleCount = latencyBlockSizeInSample;
	m_replayRate = replayRate;

	MMRESULT hr = waveOutOpen(&m_waveOutHandle, WAVE_MAPPER, &pcmwf, 0, 0, 0);
	if (hr != MMSYSERR_NOERROR)
		return false;

	// Create a looping buffer of 1 second
	m_blockCount = (replayRate + latencyBlockSizeInSample - 1) / latencyBlockSizeInSample;
	m_channelCount = pcmwf.nChannels;
	const int sizeInBytes = m_blockCount * latencyBlockSizeInSample * pcmwf.nBlockAlign;
	m_audioBuffer = (int16_t*)calloc(sizeInBytes, 1);

	m_waveHeader.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
	m_waveHeader.lpData = (LPSTR)m_audioBuffer;
	m_waveHeader.dwBufferLength = sizeInBytes;
	m_waveHeader.dwBytesRecorded = 0;
	m_waveHeader.dwUser = 0;
	m_waveHeader.dwLoops = -1;
	waveOutPrepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	m_fillBlockId = 0;

	// start the looping replay of 1 sec buffer
	waveOutWrite(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	m_open = true;
	return m_open;
}

const int16_t*	AudioWriter::GetDisplayBuffer()
{
	if (!m_open)
		return NULL;

	MMTIME mmt;
	mmt.wType = TIME_BYTES;
	if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		return NULL;

	uint32_t posInSample = mmt.u.cb / (m_channelCount * 2);
	uint32_t currentPlayBlock = posInSample / m_blockSampleCount;
	if (currentPlayBlock <= 0)
		return NULL;

	currentPlayBlock--;		// previous block
	uint32_t blkId = currentPlayBlock % m_blockCount;
	const int16_t* ret = m_audioBuffer + blkId * m_blockSampleCount * m_channelCount;
	return ret;
}

uint32_t	AudioWriter::GetPlayTime() const
{
	if (!m_open)
		return 0;

	MMTIME mmt;
	mmt.wType = TIME_SAMPLES;
	if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		return NULL;

	const uint32_t posInSample = mmt.u.sample;
	const uint32_t posInSec = posInSample / m_replayRate;
	return posInSec;
}

int16_t*	AudioWriter::GetAudioBlockToFillIfAny(int& sampleCountOut)
{

	if (!m_open)
		return NULL;

	sampleCountOut = m_blockSampleCount;

	MMTIME mmt;
	mmt.wType = TIME_SAMPLES;
	if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		return NULL;

	const uint32_t posInSample = mmt.u.sample;
	uint32_t currentPlayBlock = posInSample / m_blockSampleCount;

	if (m_fillBlockId <= currentPlayBlock)
	{
		// fill ptr is very late (catastrophic failure), we should reset it
		m_fillBlockId = currentPlayBlock + 2;
		return NULL;
	}

	// If buffer is full, no use to fill a new one
	if (m_fillBlockId >= currentPlayBlock + m_blockCount - 1)
		return NULL;

	uint32_t blkId = m_fillBlockId % m_blockCount;
	int16_t* ret = m_audioBuffer + blkId * m_blockSampleCount * m_channelCount;
	m_fillBlockId++;
	return ret;
}

