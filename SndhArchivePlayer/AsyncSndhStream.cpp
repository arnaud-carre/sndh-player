#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "AsyncSndhStream.h"
#include "imgui.h"
#include "WavWriter.h"

static const uint32_t kMaxSongDurationSample = 60 * 60*kHostReplayRate;		// clamp max pre-rendering music time to 1 hour

AsyncSndhStream::AsyncSndhStream()
{
	m_audioBuffer = nullptr;
	m_audioDebugBuffer = nullptr;
	m_asyncInfo.sndh = nullptr;
	m_asyncInfo.thread = nullptr;
	m_playMode = PlayMode_Single;
	m_replayRate = kHostReplayRate;
}

AsyncSndhStream::~AsyncSndhStream()
{
	Unload();
}

void AsyncSndhStream::Unload()
{
	CloseSubsong();
	if (m_asyncInfo.sndh)
		AtariAudioRenderer::Destroy(m_asyncInfo.sndh);
	m_asyncInfo.sndh = nullptr;
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
		m_audioStream.Stop();

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
	m_asyncInfo.sndh = AtariAudioRenderer::Create(sndhFile, fileSize, m_replayRate);
	return (m_asyncInfo.sndh != nullptr);
}


void	AsyncSndhStream::sAsyncSndhWorkerThread(void* a)
{
	AsyncSndhStream* _this = (AsyncSndhStream*)a;
	_this->AsyncWorkerFunction();
}

void AsyncSndhStream::AsyncWorkerFunction()
{
	while (m_asyncInfo.fillPos < m_exactSongSamples)
	{
		if (m_asyncInfo.forceQuit)
			break;

		uint32_t todo = m_replayRate;
		if (m_asyncInfo.fillPos + todo > m_exactSongSamples)
			todo = m_exactSongSamples - m_asyncInfo.fillPos;

		m_asyncInfo.sndh->AudioRenderWithVisualInfos(m_audioBuffer + m_asyncInfo.fillPos, todo, m_audioDebugBuffer + m_asyncInfo.fillPos);
		m_asyncInfo.fillPos += todo;
	}
}

bool AsyncSndhStream::StartSubsong(int subSongId, int durationByDefaultInSec)
{

	if (nullptr == m_asyncInfo.sndh)
		return false;

	CloseSubsong();

	if (!m_asyncInfo.sndh->InitSubSong(subSongId))
		return false;

	m_exactSongSamples = m_asyncInfo.sndh->GetSubsongDurationSample(subSongId);
	if (m_exactSongSamples == 0)
	{
		// No length tag: fall back to the default duration, playing the full buffer
		m_exactSongSamples = durationByDefaultInSec * m_replayRate;
	}

	if (m_exactSongSamples > kMaxSongDurationSample)
		m_exactSongSamples = kMaxSongDurationSample;

	// keep reasonable buffer len
	assert(uint64_t(m_exactSongSamples) * sizeof(int16_t) < 0x7fffffff);

	assert(NULL == m_audioBuffer);
	assert(NULL == m_audioDebugBuffer);
	m_audioBuffer = (int16_t*)malloc(m_exactSongSamples*sizeof(int16_t));
	m_audioDebugBuffer = (uint32_t*)malloc(m_exactSongSamples*sizeof(uint32_t));

	// Generate first second of music
	const uint32_t firstChunkSize = (m_exactSongSamples >= m_replayRate) ? m_replayRate : m_exactSongSamples;
	m_asyncInfo.sndh->AudioRenderWithVisualInfos(m_audioBuffer, firstChunkSize, m_audioDebugBuffer);

	// launch worker thread to generate
	m_asyncInfo.forceQuit = false;
	m_asyncInfo.fillPos = firstChunkSize;
	m_paused = false;
	m_saved = false;
	m_asyncInfo.thread = new std::thread(sAsyncSndhWorkerThread, (void*)this);

	// start the replay
	playOffsetInSec = 0;

	m_audioStream.Start(m_audioBuffer, m_exactSongSamples, m_replayRate);

	return true;
}

int AsyncSndhStream::GetReplayPosInSec() const
{
	if (NULL == m_audioBuffer)
		return 0;

	uint32_t posSample = m_audioStream.GetSpeakerPositionSample();

	// Clip so the reported position never overshoots the exact song length
	if (posSample > m_exactSongSamples)
		posSample = m_exactSongSamples;

	return int(posSample / m_replayRate);
}

void AsyncSndhStream::SetReplayPosInSec(int pos)
{
	if (NULL == m_audioBuffer)
		return;

	uint32_t spos = pos * m_replayRate;
	if (spos >= m_exactSongSamples)
		return;

	m_audioStream.SetPositionSample(spos);

	m_paused = false;
}

const int16_t* AsyncSndhStream::GetDisplaySampleData(int sampleCount, uint32_t** ppDebugView) const
{
	if (NULL == m_audioBuffer)
		return NULL;

	const uint32_t posInSample = m_audioStream.GetSpeakerPositionSample();

	if (posInSample + sampleCount > m_exactSongSamples)
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

	ImGui::BeginDisabled(m_asyncInfo.fillPos < m_exactSongSamples);
	uint32_t lenInSec = m_exactSongSamples / m_replayRate;
	char sLen[64];
	sprintf(sLen, "%d:%02d", lenInSec / 60, lenInSec % 60);
	static int pos;
	pos = GetReplayPosInSec();

	char sPos[64];
	sprintf(sPos, "%d:%02d", pos / 60, pos % 60);

	// Leave room on the right for the length text and the play-mode button
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 150.0f);
	if (ImGui::SliderInt("##TimeSlider", &pos, 0, lenInSec, sPos))
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
		char sFilename[kMAX_PATH];
		sprintf(sFilename, "%s.wav", musicName);
		char dispName[kMAX_PATH];
		uint32_t sizeInMiB = (m_exactSongSamples * sizeof(int16_t) + (1 << 20) - 1) >> 20;
		if ( m_saved )
			sprintf(dispName, "\"%s\" saved", sFilename);
		else
			sprintf(dispName, "Save \"%s\" (%d MiB)", sFilename, sizeInMiB);
		ImGui::BeginDisabled(m_saved);
		if (ImGui::Button(dispName))
		{
			WavWriter wv;
			if (wv.Open(sFilename, m_replayRate, 1))
			{
				wv.AddAudioData(m_audioBuffer, m_exactSongSamples);
				wv.Close();
				m_saved = true;
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::EndDisabled();
}

void AsyncSndhStream::Pause(bool pause)
{
	if (NULL == m_audioBuffer)
		return;

	m_audioStream.SetPause(pause);
}

