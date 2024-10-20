#include <assert.h>
#include <string.h>
#include <filesystem>
#include "AsyncSndhStream.h"
#include "imgui.h"
#include "WavWriter.h"
#include "SndhArchive.h"

#pragma	comment(lib,"winmm.lib")

static std::string AddExtension(std::string_view filename, std::string_view extension) {
	std::string result(filename);

	// Remove trailing spaces
	while (!result.empty() && std::isspace(result.back())) {
		result.pop_back();
	}

	result += extension;

	return result;
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
		m_lenInSec = info.playerTickCount / info.playerTickRate;
	else
		m_lenInSec = durationByDefaultInSec;

	if (m_lenInSec < 1)
		return false;
	
	// keep reasonable buffer len
	assert(uint64_t(m_lenInSec)*uint64_t(m_replayRate)*sizeof(int16_t) < 0x7fffffff);
	
	m_audioBufferLen = m_lenInSec * m_replayRate;

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
	m_waveHeader.dwBufferLength = m_audioBufferLen * sizeof(int16_t);
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
	m_rawSaved = false;
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
		return 0;

	const uint32_t posInSample = mmt.u.sample;
	const uint32_t posInSec = posInSample / m_replayRate;
	return int(posInSec) + playOffsetInSec;
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
	m_waveHeader.dwBufferLength = (m_audioBufferLen - spos)*sizeof(int16_t);
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

void	AsyncSndhStream::DrawGui(const char* musicName, const SndhArchive& archive)
{
	bool change = false;

	if (m_paused)
		change = ImGui::ArrowButton("play", ImGuiDir_Right);
	else
		change = ImGui::Button("||");

	if (change)
	{
		m_paused ^= true;
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
	if (ImGui::SliderInt(sLen, &pos, 0, m_lenInSec, sPos))
	{
		SetReplayPosInSec(pos);
	}

	if (musicName)
	{
		if (m_currentPath.empty())
		{
			std::string archiveFilename = archive.GetFilename();
			m_currentPath = std::filesystem::path(archiveFilename).parent_path();
		}

		std::string rawFilename = AddExtension(musicName, ".snd");

		char dispName[_MAX_PATH];
		int rawSize = m_asyncInfo.sndh.GetRawDataSize();
		float rawSizeInMb = rawSize / (1024.f);
		sprintf_s(dispName, m_rawSaved ? "\"%s\" SND saved (%0.2f KiB)" : "Save \"%s\" (%0.2f KiB)", rawFilename.c_str(), rawSizeInMb);

		ImGui::BeginDisabled(m_rawSaved);
		if (ImGui::Button(dispName))
		{
			m_selectedFile = rawFilename;
			m_currentEntries.clear();
			for (const auto& entry : std::filesystem::directory_iterator(m_currentPath))
			{
				m_currentEntries.push_back(entry);
			}

			m_showFileSelector = true;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();

		std::string filename = AddExtension(musicName, ".wav");
		uint32_t sizeInMiB = (m_audioBufferLen * sizeof(int16_t) + (1 << 20) - 1) >> 20;
		if ( m_saved )
			sprintf_s(dispName, "\"%s\" saved", filename.c_str());
		else
			sprintf_s(dispName, "Save \"%s\" (%d MiB)", filename.c_str(), sizeInMiB);

		ImGui::BeginDisabled(m_saved);
		if (ImGui::Button(dispName))
		{
			WavWriter wv;
			if (wv.Open(filename.c_str(), m_replayRate, 1))
			{
				wv.AddAudioData(m_audioBuffer, m_audioBufferLen);
				wv.Close();
				m_saved = true;
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::EndDisabled();

	if (m_showFileSelector)
	{
		DrawFileSelector();
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

void AsyncSndhStream::DrawFileSelector()
{
	ImGui::SetNextWindowPos(ImVec2(350, 150), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("File Selector", &m_showFileSelector)) 
	{
		ImGui::Text("Current Path: %s", m_currentPath.string().c_str());

		// Go up button
		if (ImGui::Button("..")) 
		{
			m_currentPath = m_currentPath.parent_path();
			m_currentEntries.clear();
			for (const auto& entry : std::filesystem::directory_iterator(m_currentPath))
			{
				m_currentEntries.push_back(entry);
			}
		}

		ImGui::SameLine();

		// File name input
		static char fileNameBuffer[256] = "";
		if (std::strlen(fileNameBuffer) == 0 && m_selectedFile.size() > 0)
		{
			strcpy_s(fileNameBuffer, m_selectedFile.c_str());
		}

		ImGui::InputText("File Name", fileNameBuffer, IM_ARRAYSIZE(fileNameBuffer));

		// Directory and file list
		ImGui::BeginChild("Files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
		for (const auto& entry : m_currentEntries)
		{
			const auto& path = entry.path();
			auto relativePath = path.filename();
			if (entry.is_directory()) 
			{
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
				if (ImGui::Selectable(relativePath.string().c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) 
				{
					if (ImGui::IsMouseDoubleClicked(0)) 
					{
						m_currentPath /= relativePath;
						m_currentEntries.clear();
						for (const auto& newEntry : std::filesystem::directory_iterator(m_currentPath))
						{
							m_currentEntries.push_back(newEntry);
						}
					}
				}
				ImGui::PopStyleColor();
			}
			else 
			{
				if (ImGui::Selectable(relativePath.string().c_str(), false)) 
				{
					strcpy_s(fileNameBuffer, relativePath.string().c_str());
				}
			}
		}
		ImGui::EndChild();

		// Save button
		if (ImGui::Button("Save"))
		{
			if (strlen(fileNameBuffer) > 0)
			{
				m_selectedFile = (m_currentPath / fileNameBuffer).string();
				m_showFileSelector = false;
				fileNameBuffer[0] = '\0';

				FILE* file = nullptr;
#if defined(_MSC_VER) && _MSC_VER >= 1400
				errno_t err = fopen_s(&file, m_selectedFile.c_str(), "wb");
				if (err != 0)
					file = nullptr;
#else
				file = fopen(m_selectedFile.c_str(), "wb");
#endif

				// Could be a bit smarter and use this for both raw and wav saving.
				// Also it wouldn't hurt to confirm overwrite.
				if (file)
				{
					int fileSize;
					const void* rawData = GetRawData(fileSize);
					fwrite(rawData, 1, fileSize, file);
					fclose(file);
					m_rawSaved = true;
				}
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_showFileSelector = false;
			fileNameBuffer[0] = '\0';
		}
	}
	ImGui::End();
}
