/*--------------------------------------------------------------------
	Atari Audio Library
	Small & accurate ATARI-ST audio emulation
	Arnaud Carré aka Leonard/Oxygene
	@leonard_coder
--------------------------------------------------------------------*/
#pragma once
#include <stdint.h>
#include "AtariMachine.h"

static	const	int		kDefaultSndhSubsongDuration = 3 * 60;		// 3 minutes by default
static	const	int		kSubsongCountMax = 128;

class SndhFile
{
public:
	SndhFile();
	~SndhFile();

	struct SubSongInfo
	{
		int subsongCount;
		int	playerTickCount;
		int playerTickRate;
		int samplePerTick;
		const char* musicName;
		const char* musicAuthor;
		const char* year;
	};

	bool	Load(const void* rawSndhFile, int sndhFileSize, uint32_t hostReplayRate, int subsongDurationInSecByDefault = kDefaultSndhSubsongDuration);
	void	Unload();
	bool	IsLoaded() const { return m_bLoaded; }
	
	int		GetSubsongCount() const;
	int		GetDefaultSubsong() const { return m_defaultSubSong; }
	bool	GetSubsongInfo(int subSongId, SubSongInfo& out);
	bool	InitSubSong(int subSongId);
	int		AudioRender(int16_t* buffer, int count);

	const void*	GetRawData() const { return m_rawBuffer; }
	const int	GetRawDataSize() const { return m_rawSize; }

private:
	uint16_t		Read16(const char*);
	const char*	skipNTString(const char* r);

	bool	m_bLoaded;
	char*	m_Title;
	char*	m_Author;
	char*	m_sYear;
	const void*	m_rawBuffer;
	int		m_rawSize;

	int		m_defaultSubSong;
	int		m_subSongLen[kSubsongCountMax];
	int		m_subSongCount;
	int		m_playerRate;

	int		m_samplePerTick;
	int		m_innerSamplePos;
	int		m_frame;
	int		m_frameCount;
	int		m_loopCount;
	uint32_t m_hostReplayRate;

	AtariMachine m_atariMachine;
};
