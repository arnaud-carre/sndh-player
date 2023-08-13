#pragma once
#include <stdint.h>
#include <thread>
#include <atomic>
#include "imgui_internal.h"
#include "extern/zip/src/zip.h"


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
	void	ImGuiDraw(SndhArchivePlayer& player);
	bool	IsOpen() const { return m_zipArchive != NULL; }

private:
	struct PlayListItem
	{
		int zipIndex;
		const char* title;
		const char* author;
		const char* year;
		int	duration;
	};

	void			RebuildFilterList()
	{
		m_filterdSize = 0;
		for (int i = 0; i < m_size; i++)
		{
			bool bFilter = m_ImGuiFilter.PassFilter(m_list[i].author);
			if ( !bFilter )
				bFilter = m_ImGuiFilter.PassFilter(m_list[i].title);
			if (bFilter)
			{
				m_filteredList[m_filterdSize] = m_list[i];
				m_filterdSize++;
			}
		}
	}

	PlayListItem*	m_list;
	int				m_size;
	PlayListItem*	m_filteredList;
	int				m_filterdSize;
	ImGuiTextFilter m_ImGuiFilter;
	struct zip_t*	m_zipArchive;
	static int fEntrySort(const void *arg1, const void *arg2)
	{
		const PlayListItem* a = (const PlayListItem*)arg1;
		const PlayListItem* b = (const PlayListItem*)arg2;
		int r = _stricmp(a->author, b->author);
		if ( 0 == r )
			r = _stricmp(a->title, b->title);
		return r;
	}

	static void AsyncBrowseArchiveEntry(void* a);
	void AsyncBrowseArchive();
	std::thread*	m_asyncZipThread;
	std::atomic<bool> m_asyncDone;
	std::atomic<int> m_progress;

};
