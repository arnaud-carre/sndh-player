#include "SndhArchivePlayer.h"
#include "SndhArchive.h"

SndhArchive::SndhArchive()
{
	m_zipArchive = NULL;
	m_list = NULL;
	m_filteredList = NULL;
	m_filterdSize = 0;
	m_asyncZipThread = NULL;
	m_firstSearchFocus = false;
}

SndhArchive::~SndhArchive()
{
	Close();
}

void	SndhArchive::AsyncBrowseArchiveEntry(void* a)
{
	SndhArchive* _this = (SndhArchive*)a;
	_this->AsyncBrowseArchive();
}

void	SndhArchive::AsyncBrowseArchive()
{
	assert(m_zipArchive);

	m_asyncDone = false;
	int outSize = 0;
	for (int i = 0; i < m_size; i++)
	{
		m_progress = ((i+1) * 100) / m_size;
		const PlayListItem& itemIn = m_list[i];
		if (0 == zip_entry_openbyindex(m_zipArchive, itemIn.zipIndex))
		{
			assert(!zip_entry_isdir(m_zipArchive));
			size_t size = zip_entry_size(m_zipArchive);
			void* unpack = calloc(1, size);
			size_t depackSize = zip_entry_noallocread(m_zipArchive, unpack, size);
			if (depackSize == size)
			{
				const char* fname = zip_entry_name(m_zipArchive);
				SndhFile sndhFile;
				if (sndhFile.Load(unpack, int(size), 44100))		// dummy host replay rate
				{
					SndhFile::SubSongInfo info;
					if (sndhFile.GetSubsongInfo(sndhFile.GetDefaultSubsong(), info))
					{
						PlayListItem& itemOut = m_list[outSize];
						itemOut.zipIndex = itemIn.zipIndex;
						itemOut.author = info.musicAuthor ? _strdup(info.musicAuthor) : _strdup("Not defined");
						itemOut.title = info.musicName ? _strdup(info.musicName) : _strdup(fname);
						itemOut.duration = info.playerTickCount / info.playerTickRate;
						itemOut.year = NULL;
						itemOut.subsongCount = info.subsongCount;
						outSize++;
					}
				}
			}
			free(unpack);
		}
		zip_entry_close(m_zipArchive);
	}

	m_firstSearchFocus = true;
	m_size = outSize;
	qsort((void *)m_list, (size_t)m_size, sizeof(PlayListItem), fEntrySort);
	RebuildFilterList();
	m_asyncDone = true;
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
			m_asyncDone = false;
			m_asyncZipThread = new std::thread(AsyncBrowseArchiveEntry, (void*)this);

			ret = true;
		}
	}
	return ret;
}

void	SndhArchive::Close()
{

	if (m_asyncZipThread)
	{
		m_asyncZipThread->join();
		m_asyncZipThread = NULL;
	}

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

	if (ImGui::Begin("SNDH Archive"))
	{

		if (m_asyncDone)
		{
			m_asyncZipThread->join();
			delete m_asyncZipThread;
			m_asyncZipThread = NULL;
			m_asyncDone = false;
		}

		if ((m_asyncZipThread) && (!m_asyncDone))
		{
			ImGui::Text("Parsing large SNDH ZIP archive...");
			ImGui::SameLine();
			ImGui::ProgressBar(float(m_progress)/100.f);
		}
		else
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
					ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 120.0f, 0);
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 0.0f, 1);
					ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 0.0f, 2);
					ImGui::TableSetupColumn("Sub-Song", ImGuiTableColumnFlags_PreferSortDescending, 0.0f, 3);

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
