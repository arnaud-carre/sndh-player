#pragma once

#include <stdint.h>
#include "../AtariAudio/AtariAudio.h"
#include "AsyncSndhStream.h"

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
	bool show_demo_window = false;
	bool show_another_window = false;

	AsyncSndhStream	m_sndh;
	int			m_currentSubSong;

};