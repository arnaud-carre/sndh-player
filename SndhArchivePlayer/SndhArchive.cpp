#include "SndhArchivePlayer.h"
#include "SndhArchive.h"
#include "jobSystem.h"


SndhArchive::SndhArchive()
{
	m_zipArchive = NULL;
	m_list = NULL;
	m_filteredList = NULL;
	m_filterdSize = 0;
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
			SndhFile sndhFile;
			if (sndhFile.Load(unpack, int(size), 44100))		// dummy host replay rate
			{
				SndhFile::SubSongInfo info;
				if (sndhFile.GetSubsongInfo(sndhFile.GetDefaultSubsong(), info))
				{
					item.author = info.musicAuthor ? _strdup(info.musicAuthor) : _strdup("Not defined");
					item.title = info.musicName ? _strdup(info.musicName) : _strdup(fname);
					item.duration = info.playerTickCount / info.playerTickRate;
					item.year = NULL;
					item.subsongCount = info.subsongCount;
					ret = true;
				}
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
	RebuildFilterList();
	return true;
}

bool	SndhArchive::Open(const char* sFilename)
{
	Close();
	bool ret = false;

	m_zipArchive = zip_open(sFilename, 0, 'r');
	if (m_zipArchive)
	{
		m_filename = std::string(sFilename);

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
	else
	{
		m_filename = std::string();
	}

	return ret;
}

void	SndhArchive::Close()
{

	if (m_jsBrowse.Running())
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
	m_filteredList = 0;
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

void	SndhArchive::ImGuiDraw(SndhArchivePlayer& player)
{

	if (ImGui::Begin(kWndSndhArchive))
	{
		if ( m_asyncBrowse )
		{
			if ( m_jsBrowse.Running() )
			{
				ImGui::Text("Parsing large SNDH ZIP archive...");
				ImGui::SameLine();
				ImGui::ProgressBar(float(m_progress)/100.f);
			}
			else
			{
				// loading just finished
				m_jsBrowse.Join();
				m_asyncBrowse = false;
			}
		}

		if ( !m_asyncBrowse )
		{
			if (IsOpen())
			{
				ImGui::Text("Search:");
				ImGui::SameLine();
				if (m_firstSearchFocus)
				{
					ImGui::SetKeyboardFocusHere();
					m_firstSearchFocus = false;
				}
				if (m_ImGuiFilter.Draw("Found:"))
					RebuildFilterList();

				int count = GetFilteredSize();
				ImGui::SameLine();
				ImGui::Text("%d files", count);

				// When using ScrollX or ScrollY we need to specify a size for our table container!
				// Otherwise by default the table will fit all available space, like a BeginChild() call.
				static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
				// 		ImVec2 outer_size = ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8);
				// 		if (ImGui::BeginTable("table_scrolly", 3, flags, outer_size))

		/*
				static ImGuiTableFlags flags =
					ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable
					| ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti
					| ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_NoBordersInBody
					| ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY
					| ImGuiTableFlags_SizingFixedFit;
		*/
				if (ImGui::BeginTable("table_advanced", 4, flags))
				{
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
					ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthStretch, 40.0f);
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch,40.0f);
					ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthStretch,10.f);
					ImGui::TableSetupColumn("Sub-Song", ImGuiTableColumnFlags_WidthStretch,10.f);
					ImGui::TableHeadersRow();

					// Demonstrate using clipper for large vertical lists
					ImGuiListClipper clipper;
					clipper.Begin(count);
					while (clipper.Step())
					{
						for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
						{
							ImGui::PushID(row);

							const PlayListItem& item = m_filteredList[row];

							ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);

							ImGui::TableSetColumnIndex(0);
							if (item.author)
								ImGui::TextUnformatted(item.author);
							else
								ImGui::TextUnformatted("");

							ImGui::TableSetColumnIndex(1);
							const ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
							if (ImGui::Selectable(item.title, false, selectable_flags, ImVec2(0, 0)))
							{
								player.PlayZipEntry(*this, item.zipIndex);
							}

							ImGui::TableSetColumnIndex(2);
							if (item.duration > 0)
							{
								const int mm = item.duration / 60;
								const int ss = item.duration % 60;
								ImGui::Text("%d:%02d", mm, ss);
							}
							else
								ImGui::TextUnformatted("?");

							ImGui::TableSetColumnIndex(3);
							if ( item.subsongCount>1)
								ImGui::Text("%d", item.subsongCount);

							ImGui::PopID();
						}
					}
					ImGui::EndTable();
				}
			}
			else
			{
				ImGui::Text("Please drop a large SNDH Archive .zip file here!");
				if (ImGui::Button("(you can get some from https://sndh.atari.org/download.php)"))
				{
					OsOpenInShell("https://sndh.atari.org/download.php");
				}
			}
		}
	}
	ImGui::End();

}
