#pragma once
#include <stdint.h>
#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <atomic>
#include "../AtariAudio/AtariAudio.h"

#include <string>
#include <filesystem>
#include <vector>

class SndhArchive;

class AsyncSndhStream
{
public:

	~AsyncSndhStream();

	bool LoadSndh(const void* sndhFile, int fileSize, uint32_t replayRate);
	void Unload();
	bool StartSubsong(int subSongId, int durationByDefaultInSec);
	void Pause(bool pause);

	int GetReplayPosInSec() const;
	const int16_t* GetDisplaySampleData(int sampleCount, uint32_t** ppDebugView = NULL) const;
	int		GetSubsongCount() const;
	int		GetDefaultSubsong() const;
	bool	GetSubsongInfo(int subSongId, SndhFile::SubSongInfo& out) const;
	const void* GetRawData(int& fileSize) const;

	void	DrawGui(const char* musicName, const SndhArchive& archive);

	static void sAsyncSndhWorkerThread(void* a);

private:
	void SetReplayPosInSec(int pos);
	void CloseSubsong();
	void AsyncWorkerFunction();
	void DrawFileSelector();

	struct AsyncInfo
	{
		std::atomic <uint32_t> fillPos;
		std::thread* thread{ nullptr };
		std::atomic<bool> forceQuit{ false };
		std::atomic<int> progress;
		SndhFile sndh;
	};

	int m_lenInSec = 0;
	int playOffsetInSec = 0;
	HWAVEOUT	m_waveOutHandle;
	WAVEHDR		m_waveHeader;
	int16_t*	m_audioBuffer = nullptr;
	uint32_t*	m_audioDebugBuffer = nullptr;
	uint32_t 	m_audioBufferLen = 0;
	uint32_t	m_replayRate = 0;
	bool		m_bLoaded = false;
	bool		m_paused = false;
	bool		m_saved = false;
	bool		m_rawSaved = false;
	bool		m_showFileSelector = false;

	AsyncInfo m_asyncInfo;

	// Data for the file selector ImGui
	std::filesystem::path m_currentPath;
	std::string m_selectedFile;
	std::vector<std::filesystem::directory_entry> m_currentEntries;
};
