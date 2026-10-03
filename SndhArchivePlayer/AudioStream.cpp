#include <assert.h>
#include <stdint.h>
#include "AudioStream.h"

AudioStream::~AudioStream()
{
	Stop();
}

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

AudioStream::AudioStream()
{
	m_pcmBuffer = nullptr;
	m_audioUnit = nullptr;
}


bool AudioStream::InternalRenderCallback(AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timeStamp, UInt32 busNumber, UInt32 requestedFrames, AudioBufferList* ioData)
{
    // Our configured format is mono, with one buffer.
    const size_t bytes = size_t(requestedFrames) * sizeof(int16_t);

    if (!ioData || ioData->mNumberBuffers != 1)
        return false;

    AudioBuffer& buffer = ioData->mBuffers[0];


    if (!buffer.mData || buffer.mDataByteSize < bytes)
        return false;

	const size_t writePos = m_writePos.load();
    const size_t available = m_pcmSampleCount - writePos;
    const size_t copiedFrames = std::min(size_t(requestedFrames), available);

	if (copiedFrames > 0)
	{
		std::memcpy(buffer.mData, m_pcmBuffer + writePos, copiedFrames * sizeof(int16_t));
		m_writePos.store(writePos + copiedFrames);
	}

    // Fill the rest with silence, including after end of track.
    if (copiedFrames < requestedFrames)
    {
        int16_t* destination = static_cast<int16_t*>(buffer.mData);
        const size_t copiedBytes = copiedFrames * sizeof(int16_t);
        std::memset(destination + copiedFrames, 0, (requestedFrames-copiedFrames)*sizeof(int16_t));
    }

    buffer.mDataByteSize = static_cast<UInt32>(requestedFrames*sizeof(int16_t));

    if (copiedFrames == 0)
        *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;

    // timeStamp can later anchor your playback presentation timeline.
    (void)timeStamp;
    (void)busNumber;

    return true;
}


static OSStatus sRenderCallback(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 requestedFrames,
    AudioBufferList* ioData)
{

	AudioStream& as = *static_cast<AudioStream*>(refCon);
	bool ret = as.InternalRenderCallback(actionFlags, timeStamp, busNumber, requestedFrames, ioData);
	return ret ? noErr : kAudio_ParamError;
}

bool AudioStream::Start(const int16_t* pcmBuffer, uint32_t sampleCount, uint32_t replayRate)
{
	bool ret = false;

	assert(nullptr == m_pcmBuffer);
	if (m_pcmBuffer)
		return false;

	AudioComponentDescription description{};
	description.componentType = kAudioUnitType_Output;
	description.componentSubType = kAudioUnitSubType_DefaultOutput;
	description.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &description);

	if (!component)
		return false;

	OSStatus status = AudioComponentInstanceNew(component, &m_audioUnit);
    if (status != noErr)
        return false;

    // Cleanup if any subsequent setup step fails.
    auto fail = [&](OSStatus error)
    {
        AudioComponentInstanceDispose(m_audioUnit);
        m_audioUnit = nullptr;
        return false;
    };

    AudioStreamBasicDescription format{};
    format.mSampleRate = double(replayRate);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kAudioFormatFlagIsSignedInteger |
        kAudioFormatFlagIsPacked |
        kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket = sizeof(int16_t);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(int16_t);
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 16;

	status = AudioUnitSetProperty(	m_audioUnit,
									kAudioUnitProperty_StreamFormat,
									kAudioUnitScope_Input,
									0,
									&format,
									sizeof(format));
	if (noErr == status)
	{

		AURenderCallbackStruct callback{};
		callback.inputProc = ::sRenderCallback;
		callback.inputProcRefCon = this;

		status = AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback));
		if (noErr == status)
		{
			status = AudioUnitInitialize(m_audioUnit);
			if (noErr == status)
			{
				m_pcmBuffer = pcmBuffer;
				m_pcmSampleCount = sampleCount;
				m_playOffsetSample = 0;
				m_replayRate = replayRate;
				m_writePos = 0;

				status = AudioOutputUnitStart(m_audioUnit);
				if (noErr == status)
				{
					ret = true;
				}
				else
				{
					AudioUnitUninitialize(m_audioUnit);
					m_pcmBuffer = nullptr;
				}
			}
		}
	}

	return ret;
}


bool AudioStream::Stop()
{
	if (m_pcmBuffer)
	{
		OSStatus status = AudioOutputUnitStop(m_audioUnit);
		if (status == noErr)
		{
			AudioUnitUninitialize(m_audioUnit);
			AudioComponentInstanceDispose(m_audioUnit);
			m_pcmBuffer = nullptr;
			return true;
		}
	}
	return false;
}

uint32_t AudioStream::GetSpeakerPositionSample() const
{

	if (nullptr == m_pcmBuffer)
		return 0;

	uint32_t rpos = uint32_t(m_writePos.load());
	if (rpos >= m_pcmSampleCount)
		return 0;
	
	return rpos;
}

bool AudioStream::SetPositionSample(uint32_t posSample)
{
	bool ret = false;
	if ((m_pcmBuffer) && (posSample < m_pcmSampleCount))
	{
		m_writePos.store(size_t(posSample));
		ret = true;
	}
	return ret;
}

bool AudioStream::SetPause(bool pause)
{
	return false;
}

#endif