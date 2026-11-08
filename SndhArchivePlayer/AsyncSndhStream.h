//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#pragma once
#include <stdint.h>
#include <cui/cui.h>
#include <thread>
#include <atomic>
#include "../AtariAudio/src/AtariAudio.h"
#include "AudioStream.h"

static const int kHostReplayRate = 48000;
static const int kMAX_PATH = 1024;

class AsyncSndhStream
{
public:

	~AsyncSndhStream();
	AsyncSndhStream();

	bool LoadSndh(const void* sndhFile, int fileSize, uint32_t replayRate);
	void Unload();
	bool StartSubsong(int subSongId, int durationByDefaultInSec);
	void Pause(bool pause);

	enum PlayMode
	{
		PlayMode_Continuous = 0,
		PlayMode_Random,
		PlayMode_Loop,
		PlayMode_Count
	};

	int GetReplayPosInSec() const;
	const int16_t* GetDisplaySampleData(int sampleCount, uint32_t** ppDebugView = NULL) const;
	PlayMode GetPlayMode() const { return m_playMode; }
	bool SubsongReachedEnd() const { return m_audioStream.IsEndReached(); }
	void SetReplayPosInSec(int pos);

	void	DrawGui(cui_ctx* ui);
	void	DrawOptions(cui_ctx* ui, const char* musicName);
	bool IsPaused() const { return m_paused.load(); }
	bool IsRenderComplete() const { return m_audioBuffer && m_asyncInfo.fillPos.load() >= m_exactSongSamples; }

	static void sAsyncSndhWorkerThread(void* a);
	const AtariAudioRenderer* GetSndhFile() const { return m_asyncInfo.sndh; }

private:
	void CloseSubsong();
	void AsyncWorkerFunction();

	struct AsyncInfo
	{
		std::atomic <uint32_t> fillPos;
		std::thread*	thread;
		std::atomic<bool> forceQuit;
		AtariAudioRenderer* sndh;
	};

	std::atomic<int> playOffsetInSec;
	AudioStream m_audioStream;
	int16_t*	m_audioBuffer;
	uint32_t*	m_audioDebugBuffer;
	uint32_t	m_replayRate;
	uint32_t	m_exactSongSamples;
	std::atomic<bool> m_paused;
	bool		m_saved;
	std::atomic<PlayMode> m_playMode;

	AsyncInfo m_asyncInfo;
};
