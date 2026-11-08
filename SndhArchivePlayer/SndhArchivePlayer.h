//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#pragma once

#define	SNDH_ARCHIVE_PLAYER_VERSION	"1.25"

#include <stdint.h>
#include <cui/cui.h>
#include "../AtariAudio/src/AtariAudio.h"
#include "AsyncSndhStream.h"

class SndhArchive;


static const char*	kWndSongInfo = "Song Info";
static const char*	kWndAudioOut = "Audio Out";
static const char*	kWndSndhArchive = "SNDH Archive";
static const char*	kWndFileViewer = "File Viewer";
static const char*	kWndEmulation = "Emulation";

class SndhArchivePlayer
{
public:
	SndhArchivePlayer();
	~SndhArchivePlayer();

	void	Startup(cui_ctx* ui);
	void	Update();
	void	DrawGui(cui_ctx* ui);
	bool	NeedsContinuousFrames() const;
	void	DropFile(const char* sFilename);
	bool	LoadNewMusic(const char* sFilename);
	void	Shutdown(cui_ctx* ui);
	void	PlayZipEntry(SndhArchive& sndhArchive, int zipIndex);
	void	QueueZipEntry(int zipIndex) { m_pendingZipIndex = zipIndex; }
	int 	GetPlayingZipEntry() const { return m_currentZipIndex; };

private:
	bool	StartSubsong(int subsong);
	void	Navigate(int direction);
	void	DrawSongInfo(cui_ctx* ui);
	bool	m_showAbout = false;
	bool	m_showDetails = false;
	int		m_pendingDirection = 0;
	int		m_pendingZipIndex = -1;
	float	m_topRatio = 0.36f;
	float	m_songRatio = 0.55f;
	float	m_archiveRatio = 0.64f;
	int		m_archiveTab = 0;

	AsyncSndhStream	m_sndh;
	int		m_currentSubSong = 1;
	int		m_currentZipIndex = -1;

};
