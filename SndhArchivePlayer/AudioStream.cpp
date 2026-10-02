#include <assert.h>
#include <stdint.h>
#include "AudioStream.h"

#ifdef _WIN32
#pragma	comment(lib,"winmm.lib")

AudioStream::AudioStream()
{
	m_pcmBuffer = nullptr;
}

bool AudioStream::Start(const int16_t* pcmBuffer, uint32_t sampleCount, uint32_t replayRate)
{
	bool ret = false;
	assert(NULL == m_pcmBuffer);
	m_replayRate = replayRate;
	m_pcmSampleCount = sampleCount;
	m_playOffsetSample = 0;

	WAVEFORMATEX	pcmwf;
	pcmwf.wFormatTag = WAVE_FORMAT_PCM;
	pcmwf.nChannels = 1;
	pcmwf.wBitsPerSample = 16;
	pcmwf.nBlockAlign = pcmwf.nChannels * pcmwf.wBitsPerSample / 8;
	pcmwf.nSamplesPerSec = replayRate;
	pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.cbSize = 0;

	MMRESULT hr = waveOutOpen(&m_waveOutHandle, WAVE_MAPPER, &pcmwf, 0, 0, 0);
	if (hr != MMSYSERR_NOERROR)
		return false;

	m_waveHeader.dwFlags = 0; // WHDR_BEGINLOOP | WHDR_ENDLOOP;
	m_waveHeader.lpData = (LPSTR)pcmBuffer;
	m_waveHeader.dwBufferLength = sampleCount * sizeof(int16_t);
	m_waveHeader.dwBytesRecorded = 0;
	m_waveHeader.dwUser = 0;
	m_waveHeader.dwLoops = -1;
	waveOutPrepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	// start the replay
	waveOutWrite(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
	m_pcmBuffer = pcmBuffer;
	ret = true;

	return ret;
}


bool AudioStream::Stop()
{
	bool ret = false;
	if (m_pcmBuffer)
	{
		waveOutUnprepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
		waveOutReset(m_waveOutHandle);
		waveOutClose(m_waveOutHandle);
		m_pcmBuffer = nullptr;
	}
	ret = true;
	return ret;
}

uint32_t AudioStream::GetSpeakerPositionSample() const
{
	uint32_t posInSample = 0;
	MMTIME mmt;
	mmt.wType = TIME_SAMPLES;
	if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		return 0;

	posInSample = mmt.u.sample + m_playOffsetSample;
	return posInSample;
}

bool AudioStream::SetPositionSample(uint32_t posSample)
{
	bool ret = false;
	if ((m_pcmBuffer) && (posSample < m_pcmSampleCount))
	{
		// Stupid Microsoft WaveOut API doesn't have "SetPosition"!!! So stop replay, create a new block and start it
		waveOutUnprepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
		waveOutReset(m_waveOutHandle);

		m_playOffsetSample = posSample;

		m_waveHeader.dwFlags = 0; // WHDR_BEGINLOOP | WHDR_ENDLOOP;
		m_waveHeader.lpData = (LPSTR)(m_pcmBuffer + posSample);
		m_waveHeader.dwBufferLength = (m_pcmSampleCount - posSample) * sizeof(int16_t);
		m_waveHeader.dwBytesRecorded = 0;
		m_waveHeader.dwUser = 0;
		m_waveHeader.dwLoops = -1;
		waveOutPrepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

		// start replay
		waveOutWrite(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
		ret = true;
	}
	return ret;
}

bool AudioStream::SetPause(bool pause)
{
	bool ret = false;
	if ( pause )
		waveOutPause(m_waveOutHandle);
	else
		waveOutRestart(m_waveOutHandle);
	return ret;
}

#else		// _WIN32

bool AudioStream::Start(const int16_t* pcmBuffer, uint32_t sampleCount, uint32_t replayRate)
{
	return false;
}

bool AudioStream::Stop()
{
	return false;
}

uint32_t AudioStream::GetSpeakerPositionSample() const
{
	return 0;
}

bool AudioStream::SetPositionSample(uint32_t posSample)
{
	return false; 
}

bool AudioStream::SetPause(bool pause)
{
	return false;
}

#endif