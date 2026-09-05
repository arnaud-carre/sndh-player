#pragma once
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <atomic>
#include "../AtariAudio/src/AtariAudio.h"

static const int kHostReplayRate = 48000;

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
		PlayMode_Single = 0,
		PlayMode_Loop,
		PlayMode_Continuous,
		PlayMode_Random,
		PlayMode_Count
	};

	int GetReplayPosInSec() const;
	const int16_t* GetDisplaySampleData(int sampleCount, uint32_t** ppDebugView = NULL) const;
	PlayMode GetPlayMode() const { return m_playMode; }
	bool ShouldAdvanceNext() { bool ret = m_advanceNext; m_advanceNext = false; return ret; }

	void	DrawGui(const char* musicName);

	static void sAsyncSndhWorkerThread(void* a);
	const SndhFile& GetSndhFile() const { return m_asyncInfo.sndh; }

private:
	void SetReplayPosInSec(int pos);
	void CloseSubsong();
	void AsyncWorkerFunction();

	struct AsyncInfo
	{
		std::atomic <uint32_t> fillPos;
		std::thread*	thread;
		std::atomic<bool> forceQuit;
		SndhFile sndh;
	};

	bool m_bLoaded;
	std::atomic<int> playOffsetInSec;
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
	int16_t*	m_audioBuffer;
	uint32_t*	m_audioDebugBuffer;
	uint32_t	m_replayRate;
	uint32_t	m_exactSongSamples;
	std::atomic<bool> m_paused;
	bool		m_saved;
	std::atomic<PlayMode> m_playMode;
	std::atomic<bool> m_advanceNext;

	AsyncInfo m_asyncInfo;
};
