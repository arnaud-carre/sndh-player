//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "AsyncSndhStream.h"
#include "Ui.h"
#include "WavWriter.h"

static const uint32_t kMaxSongDurationSample = 60 * 60*kHostReplayRate;		// clamp max pre-rendering music time to 1 hour

AsyncSndhStream::AsyncSndhStream()
{
	m_audioBuffer = nullptr;
	m_audioDebugBuffer = nullptr;
	m_asyncInfo.sndh = nullptr;
	m_asyncInfo.thread = nullptr;
	m_playMode = PlayMode_Continuous;
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

void AsyncSndhStream::DrawGui(cui_ctx* ui)
{
	{
		Ui::Row row(ui);
		cui_button_opts play = {};
		play.icon = m_paused ? CUI_ICON_PLAY : CUI_ICON_PAUSE;
		play.tooltip = m_paused ? "Play" : "Pause";
		play.size = CUI_SIZE_SM;
		play.variant = CUI_BUTTON_PRIMARY;
		if (cui_button_ex(ui, "##transport-play", &play))
		{
			m_paused = !m_paused;
			Pause(m_paused);
		}
		const bool ready = IsRenderComplete();
		const uint32_t length = m_exactSongSamples / m_replayRate;
		float position = float(GetReplayPosInSec());
		cui_slider_opts slider = {};
		slider.step = 1;
		slider.width = CUI_FILL;
		slider.disabled = !ready;
		if (cui_slider_ex(ui, "##seek", &position, 0, float(length), &slider))
			SetReplayPosInSec(int(position));
		const int seconds = int(position);
		cui_textf(ui, "%d:%02d / %u:%02u", seconds / 60, seconds % 60, length / 60, length % 60);
	}
}

void AsyncSndhStream::DrawOptions(cui_ctx* ui, const char* musicName)
{
	cui_menu_label(ui, "Playback mode");
	const char* modes[] = { "Continuous", "Random", "Loop" };
	for (int mode = 0; mode < PlayMode_Count; mode++)
	{
		cui_menu_item_opts item = {};
		item.checked = m_playMode.load() == PlayMode(mode);
		if (cui_menu_item_ex(ui, modes[mode], &item)) m_playMode = PlayMode(mode);
	}
	cui_menu_separator(ui);
	const std::string filename = musicName ? std::string(musicName) + ".wav" : "";
	char label[80];
	if (!musicName) snprintf(label, sizeof(label), "Export WAV");
	else
	{
		const uint32_t mib = (m_exactSongSamples * sizeof(int16_t) + (1 << 20) - 1) >> 20;
		snprintf(label, sizeof(label), m_saved ? "WAV saved" : "Export WAV (%u MiB)", mib);
	}
	cui_menu_item_opts exportItem = {};
	exportItem.icon = CUI_ICON_DOWNLOAD;
	exportItem.disabled = !musicName || !IsRenderComplete() || m_saved;
	exportItem.tooltip = filename.empty() ? nullptr : filename.c_str();
	const bool save = cui_menu_item_ex(ui, label, &exportItem);
	if (save)
	{
		WavWriter writer;
		if (writer.Open(filename.c_str(), m_replayRate, 1))
		{
			writer.AddAudioData(m_audioBuffer, m_exactSongSamples);
			writer.Close();
			m_saved = true;
		}
		else cui_toast_ex(ui, "Unable to save WAV file", nullptr);
	}
}

void AsyncSndhStream::Pause(bool pause)
{
	if (NULL == m_audioBuffer)
		return;

	m_audioStream.SetPause(pause);
}

