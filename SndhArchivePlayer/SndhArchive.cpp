//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#include "SndhArchivePlayer.h"
#include "SndhArchive.h"
#include "jobSystem.h"
#include "Ui.h"
#include <assert.h>
#include <stdio.h>
#include <algorithm>


SndhArchive::SndhArchive()
{
	m_zipArchive = NULL;
	m_list = NULL;
	m_filteredList = NULL;
	m_filterdSize = 0;
	m_size = 0;
	m_firstSearchFocus = false;
	m_asyncBrowse = false;
}

SndhArchive::~SndhArchive()
{
	Close();
}

bool SndhArchive::JobZipItemProcessing(void* user, int itemId, int workerId)
{
	SndhArchive* snd = (SndhArchive*)user;
	return snd->LoadZipEntry(itemId, workerId);
}

bool SndhArchive::JobZipItemComplete(void* user, int workerId)
{
	SndhArchive* snd = (SndhArchive*)user;
	return snd->LoadZipEnd();
}

static char* mp_strdup(const char* s)
{
	#if _WIN32
	return _strdup(s);
	#else
	return strdup(s);
	#endif
}

bool SndhArchive::LoadZipEntry(int itemId, int workerId)
{
	bool ret = false;
	int progress = ((itemId+1) * 100) / m_size;
	if (progress > m_progress)
		m_progress = progress;

	struct zip_t* zip = m_zipPerWorker[workerId];

	PlayListItem& item = m_list[itemId];
	if (0 == zip_entry_openbyindex(zip, item.zipIndex))
	{
		assert(!zip_entry_isdir(zip));
		size_t size = zip_entry_size(zip);
		void* unpack = calloc(1, size);
		size_t depackSize = zip_entry_noallocread(zip, unpack, size);
		if (depackSize == size)
		{
			const char* fname = zip_entry_name(zip);
			AtariAudioRenderer* sr = AtariAudioRenderer::Create(unpack, uint32_t(size), kHostReplayRate);		// dummy host replay rate
			if (sr)		// dummy host replay rate
			{
				const AtariAudioRenderer::SongInfo& si = sr->GetSongInfo();
				item.author = si.musicAuthor[0] ? mp_strdup(si.musicAuthor) : mp_strdup("Not defined");
				item.title = si.musicName[0] ? mp_strdup(si.musicName) : mp_strdup(fname);
				uint32_t totalLenSample = 0;
				for (int s = 0; s < si.subsongCount; s++)
					totalLenSample += sr->GetSubsongDurationSample(s + 1);
				item.duration = totalLenSample / kHostReplayRate;
				item.year = nullptr;
				item.subsongCount = si.subsongCount;
				ret = true;
				AtariAudioRenderer::Destroy(sr);
			}
		}
		free(unpack);
	}
	zip_entry_close(zip);
	if (!ret)
		item.zipIndex = -1;
	return ret;
}

bool SndhArchive::LoadZipEnd()
{
	for (int t=0;t<m_zipWorkersCount;t++)
		zip_close(m_zipPerWorker[t]);

	// pack the list, removing not loaded items
	const PlayListItem* r = m_list;
	PlayListItem* w = m_list;
	for (int i=0;i<m_size;i++)
	{
		if (r->zipIndex >= 0)
			*w++ = *r;
		r++;
	}
	m_size = int(w - m_list);
	m_firstSearchFocus = true;
	qsort((void *)m_list, (size_t)m_size, sizeof(PlayListItem), fEntrySort);
	return true;
}

bool	SndhArchive::Open(const char* sFilename)
{
	Close();
	bool ret = false;

	m_zipArchive = zip_open(sFilename, 0, 'r');
	if (m_zipArchive)
	{
		int entryCount = int(zip_entries_total(m_zipArchive));
		m_list = (PlayListItem*)malloc(entryCount * sizeof(PlayListItem));
		memset(m_list, 0, entryCount * sizeof(PlayListItem));
		m_filteredList = (PlayListItem*)malloc(entryCount * sizeof(PlayListItem));
		m_size = 0;
		for (int i = 0; i < entryCount; i++)
		{
			if (0 == zip_entry_openbyindex(m_zipArchive, i))
			{
				int isdir = zip_entry_isdir(m_zipArchive);
				if (!isdir)
				{
					m_list[m_size].zipIndex = i;
					m_size++;
				}
			}
			zip_entry_close(m_zipArchive);
		}
		if (m_size > 0)
		{
			m_progress = 0;

			m_zipWorkersCount = JobSystem::GetHardwareWorkerCount();
			if (m_zipWorkersCount > kMaxZipWorkers)
				m_zipWorkersCount = kMaxZipWorkers;
			for (int w=0;w<m_zipWorkersCount;w++)
				m_zipPerWorker[w] = zip_open(sFilename, 0, 'r');

			m_asyncBrowse = true;
			m_jsBrowse.RunJobs(this, m_size, JobZipItemProcessing, JobZipItemComplete, m_zipWorkersCount);

			ret = true;
		}
	}
	return ret;
}

void	SndhArchive::Close()
{

	if (m_asyncBrowse)
		m_jsBrowse.Join();

	if (m_zipArchive)
		zip_close(m_zipArchive);

	for (int i = 0; i < m_size; i++)
	{
		free((void*)m_list[i].author);
		free((void*)m_list[i].title);
	}
	free(m_list);
	free(m_filteredList);
	m_list = NULL;
	m_filteredList = NULL;
	m_zipArchive = nullptr;
	m_asyncBrowse = false;
	m_selected.reset();
	m_cursor = -1;
	m_lastPlayingIndex = -1;
	m_size = 0;
	m_filterdSize = 0;
}

void OsOpenInShell(const char* path)
{
#ifdef _WIN32
	// Note: executable path must use backslashes!
	::ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWDEFAULT);
#else
#if __APPLE__
	const char* open_executable = "open";
#else
	const char* open_executable = "xdg-open";
#endif
	char command[256];
	snprintf(command, 256, "%s \"%s\"", open_executable, path);
	system(command);
#endif
}

void SndhArchive::Update()
{
	if (m_asyncBrowse && !m_jsBrowse.Running())
	{
		m_jsBrowse.Join();
		m_asyncBrowse = false;
		RebuildFilterList();
	}
}

void SndhArchive::SortFilteredList()
{
	if (m_filterdSize > 1)
		std::sort(m_filteredList, m_filteredList + m_filterdSize, [this](const PlayListItem& a, const PlayListItem& b)
		{
			int result = 0;
			if (m_sortColumn == 0 || m_sortColumn == 1)
			{
				const char* left = m_sortColumn == 0 ? a.author : a.title;
				const char* right = m_sortColumn == 0 ? b.author : b.title;
#ifdef _WIN32
				result = _stricmp(left ? left : "", right ? right : "");
#else
				result = strcasecmp(left ? left : "", right ? right : "");
#endif
			}
			else
			{
				const int left = m_sortColumn == 2 ? a.duration : a.subsongCount;
				const int right = m_sortColumn == 2 ? b.duration : b.subsongCount;
				result = (left > right) - (left < right);
			}
			if (!result) result = fEntrySort(&a, &b);
			if (!result) result = (a.zipIndex > b.zipIndex) - (a.zipIndex < b.zipIndex);
			return m_sortDescending ? result > 0 : result < 0;
		});
	m_cursor = -1;
	m_lastPlayingIndex = -1;
}

void SndhArchive::DrawGui(cui_ctx* ui, SndhArchivePlayer& player)
{
	Ui::Column column(ui, CUI_FILL, 12, 14);
	if (m_asyncBrowse)
	{
		cui_heading(ui, "Opening your archive", 4);
		cui_secondary(ui, "Reading SNDH and YM metadata...");
		cui_progress_opts progress = {};
		cui_progress_ex(ui, float(m_progress.load()) / 100, &progress);
		return;
	}
	if (!IsOpen())
	{
		cui_heading(ui, "Discover Atari music", 3);
		cui_secondary(ui, "Drop a ZIP archive containing SNDH and YM files anywhere in this window.");
		if (Ui::Button(ui, "Get the SNDH archive", false, CUI_ICON_DOWNLOAD))
			OsOpenInShell("https://sndh.atari.org/download.php");
#ifndef _WIN32
		cui_muted(ui, "Safari: disable 'Open safe files after downloading' to keep the archive zipped.");
#endif
		return;
	}
	{
		Ui::Row row(ui);
		cui_input_opts search = {};
		search.width = CUI_FILL;
		search.placeholder = "Search author or title (include, -exclude)";
		search.clearable = true;
		search.autofocus = m_firstSearchFocus;
		if (cui_search_ex(ui, "archive-search", m_search, sizeof(m_search), &search) & CUI_CHANGED)
		{
			m_filter.Build(m_search);
			RebuildFilterList();
		}
		m_firstSearchFocus = false;
		cui_textf(ui, "%d files", m_filterdSize);
		if (Ui::Button(ui, "Columns")) cui_popover_toggle(ui, "archive-columns");
		cui_popover_opts popup = {};
		popup.width = 290;
		if (cui_popover_begin(ui, "archive-columns", &popup))
		{
			const char* names[] = { "Author", "Name", "Duration", "Sub-tunes" };
			cui_muted(ui, "Show and reorder columns");
			for (int pos = 0; pos < 4; pos++)
			{
				const int index = m_columnOrder[pos];
				cui_push_id_int(ui, pos);
				{
					Ui::Row controls(ui);
					const int visible = int(m_columnVisible[0]) + int(m_columnVisible[1]) + int(m_columnVisible[2]) + int(m_columnVisible[3]);
					cui_check_opts check = {};
					check.disabled = visible == 1 && m_columnVisible[index];
					if (cui_checkbox_ex(ui, names[index], &m_columnVisible[index], &check)) ++m_columnRevision;
					cui_spacer(ui);
					if (Ui::IconButton(ui, CUI_ICON_CHEVRON_UP, "Move column left", pos == 0))
					{
						std::swap(m_columnOrder[pos], m_columnOrder[pos - 1]);
						++m_columnRevision;
					}
					if (Ui::IconButton(ui, CUI_ICON_CHEVRON_DOWN, "Move column right", pos == 3))
					{
						std::swap(m_columnOrder[pos], m_columnOrder[pos + 1]);
						++m_columnRevision;
					}
				}
				cui_pop_id(ui);
			}
			cui_popover_end(ui);
		}
	}
	const int playing = player.GetPlayingZipEntry();
	if (playing != m_lastPlayingIndex)
	{
		for (int i = 0; i < m_filterdSize; i++)
		{
			m_selected[i] = m_filteredList[i].zipIndex == playing;
			if (m_selected[i]) m_cursor = i;
		}
		m_lastPlayingIndex = playing;
	}
	static const cui_grid_column columns[] = {
		{ "Author", 260, CUI_TEXT_LEFT, true }, { "Name", 350, CUI_TEXT_LEFT, true }, { "Duration", 110, CUI_TEXT_RIGHT, true }, { "Sub-tunes", 100, CUI_TEXT_RIGHT, true }
	};
	cui_grid_column shownColumns[4];
	int shownIndices[4];
	int columnCount = 0;
	for (int index : m_columnOrder)
	{
		if (!m_columnVisible[index]) continue;
		shownIndices[columnCount] = index;
		shownColumns[columnCount++] = columns[index];
	}
	cui_table_sort sort = { -1, m_sortDescending };
	for (int i = 0; i < columnCount; i++)
		if (shownIndices[i] == m_sortColumn) sort.column = i;
	const cui_table_sort previousSort = sort;
	cui_grid_opts grid = {};
	grid.sort = &sort;
	grid.width = CUI_FILL;
	grid.height = CUI_FILL;
	grid.row_height = 30;
	grid.striped = true;
	grid.selected = m_selected.get();
	grid.hide_checkboxes = true;
	grid.cursor = &m_cursor;
	int first = 0, last = 0;
	int activate = -1;
	cui_push_id_int(ui, m_columnRevision);
	if (cui_data_grid_begin_ex(ui, "archive-tracks", shownColumns, columnCount, m_filterdSize, &first, &last, &grid))
	{
		for (int row = first; row < last; row++)
		{
			const PlayListItem& item = m_filteredList[row];
			if (cui_data_grid_row(ui, row)) activate = item.zipIndex;
			for (int columnIndex = 0; columnIndex < columnCount; columnIndex++)
			{
				char value[32];
				switch (shownIndices[columnIndex])
				{
				case 0: cui_data_grid_cell_text(ui, item.author ? item.author : ""); break;
				case 1:
				{
					cui_data_grid_cell(ui);
					cui_text_opts text = {};
					text.nowrap = true;
					text.width = CUI_FILL;
					text.color = item.zipIndex == playing ? CUI_COLOR_ACCENT : CUI_COLOR_FOREGROUND;
					cui_text_ex(ui, item.title ? item.title : "", &text);
					break;
				}
				case 2:
					if (item.duration > 0) snprintf(value, sizeof(value), "%d:%02d", item.duration / 60, item.duration % 60);
					else snprintf(value, sizeof(value), "?");
					cui_data_grid_cell_text(ui, value);
					break;
				case 3:
					if (item.subsongCount > 1) snprintf(value, sizeof(value), "%d", item.subsongCount);
					else value[0] = 0;
					cui_data_grid_cell_text(ui, value);
					break;
				}
			}
		}
		cui_data_grid_end(ui);
	}
	cui_pop_id(ui);
	if (sort.column >= 0 && (sort.column != previousSort.column || sort.descending != previousSort.descending))
	{
		m_sortColumn = shownIndices[sort.column];
		m_sortDescending = sort.descending;
		SortFilteredList();
	}
	// CUI paints canvases after layout; keep their audio pointers alive until then.
	if (activate >= 0) player.QueueZipEntry(activate);
}
