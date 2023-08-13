#pragma once

#include <stdint.h>
#include "AudioWriter.h"
#include "../AtariAudio/AtariAudio.h"

class SndhArchive;

class SndhArchivePlayer
{
public:
	SndhArchivePlayer();
	~SndhArchivePlayer();

	void	Startup();
	void	UpdateImGui();
	void	DropFile(const char* sFilename);
	bool	LoadNewMusic(const char* sFilename);
	void	Shutdown();
	void	PlayZipEntry(SndhArchive& sndhArchive, int zipIndex);

private:
	bool	StartSubsong(int subsong);
	void	DrawPlayList();
	void	StopAudio();
	bool show_demo_window = false;
	bool show_another_window = false;

	bool		m_musicPlaying;
	AudioWriter	m_audioWriter;
	SndhFile	m_sndh;
	int			m_currentSubSong;

};