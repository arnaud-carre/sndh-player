//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#pragma once
#include <stdint.h>
#include <thread>
#include <atomic>
#include <random>
#include <ctime>
#include "ArchiveFilter.h"
#include <cui/cui.h>
#include <memory>
#include <cstring>
#include <cstdlib>
#ifndef _WIN32
#include <strings.h>
#endif
#include "extern/zip/src/zip.h"
#include "jobSystem.h"


class SndhArchivePlayer;
struct zip_t;

class SndhArchive
{
public:
	SndhArchive();
	~SndhArchive();

	struct zip_t* GetZipArchiveHandle() { return m_zipArchive; }

	bool	Open(const char* sFilename);
	void	Close();
	int		GetFilteredSize() const { return m_filterdSize; }
	int		GetUnFilteredSize() const { return m_size; }
	void	DrawGui(cui_ctx* ui, SndhArchivePlayer& player);
	void	Update();
	bool	IsOpen() const { return m_zipArchive != NULL; }
	bool	IsOpening() const { return m_asyncBrowse; }

	// Returns the zipIndex following currentZipIndex in the filtered list, or -1 if last/not found
	int GetNextFilteredZipIndex(int currentZipIndex) const
	{
		for (int i = 0; i < m_filterdSize - 1; ++i)
		{
			if (m_filteredList[i].zipIndex == currentZipIndex)
				return m_filteredList[i + 1].zipIndex;
		}
		return -1;
	}

	// Returns the zipIndex preceding currentZipIndex in the filtered list, or -1 if first/not found
	int GetPrevFilteredZipIndex(int currentZipIndex) const
	{
		for (int i = 1; i < m_filterdSize; ++i)
		{
			if (m_filteredList[i].zipIndex == currentZipIndex)
				return m_filteredList[i - 1].zipIndex;
		}
		return -1;
	}

	// Picks a random track (and random subsong, if it has several) from the filtered list
	void GetRandomFilteredSong(int& outZipIndex, int& outSubsong) const
	{
		if (m_filterdSize <= 0)
		{
			outZipIndex = -1;
			outSubsong = 1;
			return;
		}

		static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
		std::uniform_int_distribution<int> fileDist(0, m_filterdSize - 1);
		const int randomRow = fileDist(rng);

		outZipIndex = m_filteredList[randomRow].zipIndex;

		const int subsongCount = m_filteredList[randomRow].subsongCount;
		if (subsongCount > 1)
		{
			std::uniform_int_distribution<int> subDist(1, subsongCount);
			outSubsong = subDist(rng);
		}
		else
		{
			outSubsong = 1;
		}
	}

private:
	struct PlayListItem
	{
		int zipIndex;
		const char* title;
		const char* author;
		const char* year;
		int	duration;
		int subsongCount;
	};

	void			RebuildFilterList()
	{
		m_filterdSize = 0;
		for (int i = 0; i < m_size; i++)
		{
			bool bFilter = m_filter.PassFilter(m_list[i].author);
			if ( !bFilter )
				bFilter = m_filter.PassFilter(m_list[i].title);
			if (bFilter)
			{
				m_filteredList[m_filterdSize] = m_list[i];
				m_filterdSize++;
			}
		}
		SortFilteredList();
		m_selected.reset(new bool[m_filterdSize]());
		m_cursor = -1;
		m_lastPlayingIndex = -1;
	}

	void SortFilteredList();
	int m_sortColumn = 0;
	bool m_sortDescending = false;

	PlayListItem*	m_list;
	int				m_size;
	PlayListItem*	m_filteredList;
	int				m_filterdSize;
	ArchiveFilter m_filter;
	char m_search[256] = {};
	std::unique_ptr<bool[]> m_selected;
	int m_cursor = -1;
	int m_lastPlayingIndex = -1;
	int m_columnOrder[4] = { 0, 1, 2, 3 };
	bool m_columnVisible[4] = { true, true, true, true };
	int m_columnRevision = 0;
	struct zip_t*	m_zipArchive;
	static int fEntrySort(const void *arg1, const void *arg2)
	{
		const PlayListItem* a = (const PlayListItem*)arg1;
		const PlayListItem* b = (const PlayListItem*)arg2;
		#if _WIN32
		int r = _stricmp(a->author, b->author);
		if ( 0 == r )
			r = _stricmp(a->title, b->title);
		#else
		int r = strcasecmp(a->author, b->author);
		if ( 0 == r )
			r = strcasecmp(a->title, b->title);
		#endif
		return r;
	}

	static const int kMaxZipWorkers = 16;

	// job system large SNDH zip archive reader
	struct zip_t* m_zipPerWorker[kMaxZipWorkers];
	JobSystem m_jsBrowse;
	static bool JobZipItemProcessing(void* user, int itemId, int workerId);
	static bool JobZipItemComplete(void* user, int workerId);
	bool LoadZipEntry(int itemId, int workerId);
	bool LoadZipEnd();
	int m_zipWorkersCount;
	bool m_asyncBrowse;
	std::atomic<int> m_progress;
	bool m_firstSearchFocus;

};
