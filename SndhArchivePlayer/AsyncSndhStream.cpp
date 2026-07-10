#include <assert.h>
#include <string.h>
#include "AsyncSndhStream.h"
#include "imgui.h"
#include "WavWriter.h"

#pragma	comment(lib,"winmm.lib")

AsyncSndhStream::AsyncSndhStream()
{
	m_audioBuffer = NULL;
	m_audioDebugBuffer = NULL;
	m_bLoaded = false;
	m_asyncInfo.thread = NULL;
	m_playMode = PlayMode_Single;
	m_advanceNext = false;
}

AsyncSndhStream::~AsyncSndhStream()
{
	Unload();
}

void AsyncSndhStream::Unload()
{
	CloseSubsong();
	m_asyncInfo.sndh.Unload();
}

void AsyncSndhStream::CloseSubsong()
{

	// kill any async working thread
	if (m_asyncInfo.thread)
	{
		m_asyncInfo.forceQuit = true;
		m_asyncInfo.thread->join();
		delete m_asyncInfo.thread;
		m_asyncInfo.thread = NULL;
	}

	if (m_audioBuffer)
	{
		waveOutUnprepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
		waveOutReset(m_waveOutHandle);
		waveOutClose(m_waveOutHandle);

		free(m_audioBuffer);
		free(m_audioDebugBuffer);
		m_audioBuffer = NULL;
		m_audioDebugBuffer = NULL;
	}
}

bool AsyncSndhStream::LoadSndh(const void* sndhFile, int fileSize, uint32_t replayRate)
{
	Unload();
	m_replayRate = replayRate;
	m_bLoaded = m_asyncInfo.sndh.Load(sndhFile, fileSize, replayRate);
	return m_bLoaded;
}


void	AsyncSndhStream::sAsyncSndhWorkerThread(void* a)
{
	AsyncSndhStream* _this = (AsyncSndhStream*)a;
	_this->AsyncWorkerFunction();
}

void AsyncSndhStream::AsyncWorkerFunction()
{

	while (m_asyncInfo.fillPos < m_audioBufferLen)
	{
		if (m_asyncInfo.forceQuit)
			break;

		uint32_t todo = m_replayRate;
		if (m_asyncInfo.fillPos + todo > m_audioBufferLen)
			todo = m_audioBufferLen - m_asyncInfo.fillPos;

		m_asyncInfo.sndh.AudioRender(m_audioBuffer + m_asyncInfo.fillPos, todo, m_audioDebugBuffer + m_asyncInfo.fillPos);
		m_asyncInfo.fillPos += todo;

		m_asyncInfo.progress = (m_asyncInfo.fillPos * 100) / m_audioBufferLen;
	}

	// Poll for end-of-song here (rather than in DrawGui) so Continuous/Random advance
	// even while the window is unfocused, where ImGui rendering is throttled.
	while (!m_asyncInfo.forceQuit)
	{
		::Sleep(50);

		const PlayMode mode = m_playMode;
		if (m_paused || (mode != PlayMode_Continuous && mode != PlayMode_Random))
			continue;

		MMTIME mmt;
		mmt.wType = TIME_SAMPLES;
		if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
			continue;

		const uint32_t pos = mmt.u.sample + (uint32_t)playOffsetInSec * m_replayRate;
		if (pos < m_exactSongSamples)
			continue;

		m_advanceNext = true;
		m_paused = true;
		break;
	}
}

bool AsyncSndhStream::StartSubsong(int subSongId, int durationByDefaultInSec)
{

	if (!m_bLoaded)
		return false;

	CloseSubsong();

	SndhFile::SubSongInfo info;
	if (!m_asyncInfo.sndh.GetSubsongInfo(subSongId, info))
		return false;

	if (!m_asyncInfo.sndh.InitSubSong(subSongId))
		return false;

	assert(m_replayRate > 0);
	assert(info.playerTickRate > 0);

	if (info.playerTickCount > 0)
	{
		// Exact sample count from the SNDH v2.2 "frames" tag, used for playback/WAV end-of-song
		m_exactSongSamples = info.playerTickCount * info.samplePerTick;

		// Round up to the next whole second for the ImGui slider range
		m_lenInSec = (info.playerTickCount + info.playerTickRate - 1) / info.playerTickRate;
		if (m_lenInSec <= 0)
			m_lenInSec = 1;
	}
	else
	{
		// No length tag: fall back to the default duration, playing the full buffer
		m_lenInSec = durationByDefaultInSec;
		m_exactSongSamples = m_lenInSec * m_replayRate;
	}

	assert(m_lenInSec >= 1);

	// keep reasonable buffer len
	m_audioBufferLen = m_lenInSec * m_replayRate;
	assert(uint64_t(m_audioBufferLen) * sizeof(int16_t) < 0x7fffffff);

	WAVEFORMATEX	pcmwf;
	pcmwf.wFormatTag = WAVE_FORMAT_PCM;
	pcmwf.nChannels = 1;
	pcmwf.wBitsPerSample = 16;
	pcmwf.nBlockAlign = pcmwf.nChannels * pcmwf.wBitsPerSample / 8;
	pcmwf.nSamplesPerSec = m_replayRate;
	pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
	pcmwf.cbSize = 0;

	MMRESULT hr = waveOutOpen(&m_waveOutHandle, WAVE_MAPPER, &pcmwf, 0, 0, 0);
	if (hr != MMSYSERR_NOERROR)
		return false;

	assert(NULL == m_audioBuffer);
	assert(NULL == m_audioDebugBuffer);
	m_audioBuffer = (int16_t*)malloc(m_audioBufferLen*sizeof(int16_t));
	m_audioDebugBuffer = (uint32_t*)malloc(m_audioBufferLen*sizeof(uint32_t));

	m_waveHeader.dwFlags = 0; // WHDR_BEGINLOOP | WHDR_ENDLOOP;
	m_waveHeader.lpData = (LPSTR)m_audioBuffer;
	m_waveHeader.dwBufferLength = m_exactSongSamples * sizeof(int16_t);
	m_waveHeader.dwBytesRecorded = 0;
	m_waveHeader.dwUser = 0;
	m_waveHeader.dwLoops = -1;
	waveOutPrepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	// Generate first second of music
	m_asyncInfo.sndh.AudioRender(m_audioBuffer, m_replayRate, m_audioDebugBuffer);

	// launch worker thread to generate
	m_asyncInfo.forceQuit = false;
	m_asyncInfo.fillPos = m_replayRate;
	m_paused = false;
	m_saved = false;
	m_asyncInfo.thread = new std::thread(sAsyncSndhWorkerThread, (void*)this);

	// start the replay
	playOffsetInSec = 0;
	waveOutWrite(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	return true;
}

int AsyncSndhStream::GetSubsongCount() const
{
	return m_asyncInfo.sndh.GetSubsongCount();
}

int AsyncSndhStream::GetDefaultSubsong() const
{
	return m_asyncInfo.sndh.GetDefaultSubsong();
}

bool AsyncSndhStream::GetSubsongInfo(int subSongId, SndhFile::SubSongInfo& out) const
{
	return m_asyncInfo.sndh.GetSubsongInfo(subSongId, out);
}

int AsyncSndhStream::GetReplayPosInSec() const
{
	if (NULL == m_audioBuffer)
		return 0;

	MMTIME mmt;
	mmt.wType = TIME_SAMPLES;
	if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		return playOffsetInSec;

	uint32_t posInSample = mmt.u.sample + (playOffsetInSec * m_replayRate);

	// Clip so the reported position never overshoots the exact song length
	if (posInSample > m_exactSongSamples)
		posInSample = m_exactSongSamples;

	return int(posInSample / m_replayRate);
}

void AsyncSndhStream::SetReplayPosInSec(int pos)
{
	if (NULL == m_audioBuffer)
		return;

	if (pos >= m_lenInSec)
		return;

	uint32_t spos = pos * m_replayRate;
	if (spos >= m_audioBufferLen)
		return;

	// Stupid Microsoft WaveOut API doesn't have "SetPosition"!!! So stop replay, create a new block and start it
	waveOutUnprepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));
	waveOutReset(m_waveOutHandle);

	playOffsetInSec = pos;

	m_waveHeader.dwFlags = 0; // WHDR_BEGINLOOP | WHDR_ENDLOOP;
	m_waveHeader.lpData = (LPSTR)(m_audioBuffer + spos);
	m_waveHeader.dwBufferLength = (m_exactSongSamples - spos)*sizeof(int16_t);
	m_waveHeader.dwBytesRecorded = 0;
	m_waveHeader.dwUser = 0;
	m_waveHeader.dwLoops = -1;
	waveOutPrepareHeader(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	// start replay
	waveOutWrite(m_waveOutHandle, &m_waveHeader, sizeof(WAVEHDR));

	m_paused = false;

}

const int16_t* AsyncSndhStream::GetDisplaySampleData(int sampleCount, uint32_t** ppDebugView) const
{
	if (NULL == m_audioBuffer)
		return NULL;

	MMTIME mmt;
	mmt.wType = TIME_SAMPLES;
	if (MMSYSERR_NOERROR != waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		return NULL;

	const uint32_t posInSample = mmt.u.sample + playOffsetInSec * m_replayRate;
	if (posInSample + sampleCount > m_audioBufferLen)
		return NULL;

	if (ppDebugView)
		*ppDebugView = m_audioDebugBuffer + posInSample;

	return m_audioBuffer + posInSample;
}

void	AsyncSndhStream::DrawGui(const char* musicName)
{

	bool change = false;

	if (m_paused)
		change = ImGui::ArrowButton("play", ImGuiDir_Right);
	else
		change = ImGui::Button("||");

	if (change)
	{
		m_paused = !m_paused;
		Pause(m_paused);
	}
	ImGui::SameLine();

	ImGui::BeginDisabled(m_asyncInfo.fillPos < m_audioBufferLen);
	char sLen[64];
	sprintf_s(sLen, "%d:%02d", m_lenInSec / 60, m_lenInSec % 60);
	static int pos;
	pos = GetReplayPosInSec();

	char sPos[64];
	sprintf_s(sPos, "%d:%02d", pos / 60, pos % 60);

	// Leave room on the right for the length text and the play-mode button
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
	if (ImGui::SliderInt("##TimeSlider", &pos, 0, m_lenInSec, sPos))
	{
		SetReplayPosInSec(pos);
	}
	ImGui::PopItemWidth();

	ImGui::SameLine();
	ImGui::Text("%s", sLen);

	ImGui::SameLine();

	// Cycles Single -> Loop -> Continuous -> Random
	const char* modeLabels[] = { "[ ]", "[L]", "[C]", "[R]" };
	if (ImGui::Button(modeLabels[m_playMode]))
	{
		m_playMode = static_cast<PlayMode>((m_playMode + 1) % PlayMode_Count);
	}
	if (ImGui::IsItemHovered())
	{
		const char* tooltips[] = { "Mode: Single Track", "Mode: Loop Current", "Mode: Continuous Play", "Mode: Random Shuffle" };
		ImGui::SetTooltip("%s", tooltips[m_playMode]);
	}

	if (musicName)
	{
		char sFilename[_MAX_PATH];
		sprintf_s(sFilename, "%s.wav", musicName);
		char dispName[_MAX_PATH];
		uint32_t sizeInMiB = (m_audioBufferLen * sizeof(int16_t) + (1 << 20) - 1) >> 20;
		if ( m_saved )
			sprintf_s(dispName, "\"%s\" saved", sFilename);
		else
			sprintf_s(dispName, "Save \"%s\" (%d MiB)", sFilename, sizeInMiB);
		ImGui::BeginDisabled(m_saved);
		if (ImGui::Button(dispName))
		{
			WavWriter wv;
			if (wv.Open(sFilename, m_replayRate, 1))
			{
				uint32_t exactSampleLen = m_exactSongSamples;
				if (exactSampleLen == 0 || exactSampleLen > m_audioBufferLen)
					exactSampleLen = m_audioBufferLen;

				wv.AddAudioData(m_audioBuffer, exactSampleLen);
				wv.Close();
				m_saved = true;
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::EndDisabled();

	// Loop mode only: seamless restart when song ends (Continuous/Random are handled
	// by the background worker thread so they work even when the app is unfocused)
	if (m_playMode == PlayMode_Loop && !m_paused)
	{
		MMTIME mmt;
		mmt.wType = TIME_SAMPLES;
		if (MMSYSERR_NOERROR == waveOutGetPosition(m_waveOutHandle, &mmt, sizeof(MMTIME)))
		{
			const uint32_t currentPosInSamples = mmt.u.sample + (playOffsetInSec * m_replayRate);
			if (currentPosInSamples >= m_exactSongSamples)
				SetReplayPosInSec(0);
		}
	}
}

const void* AsyncSndhStream::GetRawData(int& fileSize) const
{
	fileSize = m_asyncInfo.sndh.GetRawDataSize();
	return m_asyncInfo.sndh.GetRawData();
}

void AsyncSndhStream::Pause(bool pause)
{
	if (NULL == m_audioBuffer)
		return;

	if ( pause )
		waveOutPause(m_waveOutHandle);
	else
		waveOutRestart(m_waveOutHandle);
}

