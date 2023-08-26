#define _CRT_SECURE_NO_WARNINGS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#include "imgui_memory_editor.h"
#include "../AtariAudio/AtariAudio.h"
#include "SndhArchivePlayer.h"
#include "SndhArchive.h"
#include "AsyncSndhStream.h"

static const int kHostReplayRate = 44100;
static const int kLatencySampleCount = kHostReplayRate / 60;

static int gDefaultDurationInMin = 4;

static	SndhArchive	gArchive;

int16_t gOneSecondAudioBuffer[kHostReplayRate];

SndhArchivePlayer::SndhArchivePlayer()
{
}

SndhArchivePlayer::~SndhArchivePlayer()
{
}

bool	SndhArchivePlayer::StartSubsong(int subsong)
{
	bool ret = false;
	if ((subsong >= 1) && (subsong <= m_sndh.GetSubsongCount()))
	{
		if (m_sndh.StartSubsong(subsong, gDefaultDurationInMin*60))
		{
			m_currentSubSong = subsong;
		}
	}
	return ret;
}

bool	SndhArchivePlayer::LoadNewMusic(const char* sFilename)
{
	bool ret = false;

	m_sndh.Unload();

	FILE* h = fopen(sFilename, "rb");
	if (h)
	{
		fseek(h, 0, SEEK_END);
		int sndhSize = ftell(h);
		void* sndhBuffer = malloc(sndhSize);
		fseek(h, 0, SEEK_SET);
		fread(sndhBuffer, 1, sndhSize, h);
		fclose(h);

		if (m_sndh.LoadSndh(sndhBuffer, sndhSize, kHostReplayRate))
		{
			if ( StartSubsong(m_sndh.GetDefaultSubsong()))
				ret = true;
		}
		free(sndhBuffer);
	}
	return ret;
}

void	SndhArchivePlayer::PlayZipEntry(SndhArchive& sndhArchive, int zipIndex)
{
	struct zip_t* zipArchive = sndhArchive.GetZipArchiveHandle();
	if (zipArchive)
	{
		if (0 == zip_entry_openbyindex(zipArchive, zipIndex))
		{
			size_t size = zip_entry_size(zipArchive);
			void* unpack = calloc(1, size);

			size_t depackSize = zip_entry_noallocread(zipArchive, unpack, size);
			if (depackSize == size)
			{
				m_sndh.Unload();
				if (m_sndh.LoadSndh(unpack, int(size), kHostReplayRate))
				{
					StartSubsong(m_sndh.GetDefaultSubsong());
				}
			}
			free(unpack);
		}
	}
}

void	SndhArchivePlayer::DrawPlayList()
{
	gArchive.ImGuiDraw(*this);
}




static bool drawOscillo = true;

void	ImDrawOscillo(const int16_t* audio, int count, const char* winName)
{
	const float dpiScale = 1.0f;
//	ImGui::SetNextWindowSizeConstraints(ImVec2(64.0f*dpiScale, 32.0f*dpiScale), ImVec2(1280, 800));
	if (ImGui::Begin(winName, NULL, 0))
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (count > kLatencySampleCount)
			count = kLatencySampleCount;
		ImVec2 waveform[kLatencySampleCount];
		ImVec2 size = ImGui::GetContentRegionAvail();

		ImVec2 minArea = window->DC.CursorPos;
		ImVec2 maxArea = ImVec2(
			minArea.x + size.x,
			minArea.y + size.y
		);
		ImRect rect = ImRect(minArea, maxArea);
		ImRect inRect = rect;
		inRect.Min.x += dpiScale;
		inRect.Min.y += dpiScale;
		inRect.Max.x -= dpiScale;
		inRect.Max.y -= dpiScale;
		ImGuiStyle& style = ImGui::GetStyle();
		ImU32 color = IM_COL32(255, 255, 255, 255);
		ImU32 borderColor = IM_COL32(0, 0, 255, 255);
		ImU32 refColor = IM_COL32(0, 0, 0, 255);
		ImU32 guideColor = IM_COL32(255, 0, 0, 255);
		ImGui::ItemSize(size, style.FramePadding.y);
		ImU32 bkgColor = IM_COL32(16, 16, 48, 255);
		if (ImGui::ItemAdd(rect, ImGui::GetID("wsDisplay")))
		{
			dl->AddRectFilledMultiColor(
				inRect.Min,
				inRect.Max,
				bkgColor,
				bkgColor,
				bkgColor,
				bkgColor
			);

			int w = int(inRect.Max.x) - int(inRect.Min.x);
			if (w > count)
				w = count;
			if (w <= 0)
				w = 1;

			ImU32 rPos = 0;
			ImU32 rStep = (count << 16) / w;
			for (int i = 0; i < w; i++)
			{
				float x = (float)i / float(w);
				float y = 0.f;
				if (audio)
				{
					y = float(audio[rPos >> 16])*(1.0f / 65536.f);
					rPos += rStep;
					{
						if (y < -0.5f) y = -0.5f;
						if (y > 0.5f) y = 0.5f;
					}
				}
				waveform[i] = ImLerp(inRect.Min, inRect.Max, ImVec2(x, 0.5f - y));
			}

			{
				dl->PushClipRectFullScreen();
				dl->AddPolyline(waveform, w, color, ImDrawFlags_None, dpiScale);
				dl->PopClipRect();
			}
			{
				dl->AddRect(inRect.Min, inRect.Max, borderColor, 8.0f*dpiScale, 0, 1.5f*dpiScale);
			}
		}

	}
	ImGui::End();
}

void	ImDrawOscilloVoice(const uint32_t* audio, int count, int index, const char* name, const ImVec2& size)
{
	const float dpiScale = 1.0f;
	ImGui::BeginChild(name, size, false); //, window_flags);
	ImGui::Text(name);
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (count > kLatencySampleCount)
			count = kLatencySampleCount;
		ImVec2 waveform[kLatencySampleCount];
		ImVec2 size = ImGui::GetContentRegionAvail();

		ImVec2 minArea = window->DC.CursorPos;
		ImVec2 maxArea = ImVec2(
			minArea.x + size.x,
			minArea.y + size.y
		);
		ImRect rect = ImRect(minArea, maxArea);
		ImRect inRect = rect;
		const int voiceShift = index * 8;
/*
		inRect.Min.x += dpiScale;
		inRect.Min.y += dpiScale;
		inRect.Max.x -= dpiScale;
		inRect.Max.y -= dpiScale;
*/
		ImGuiStyle& style = ImGui::GetStyle();
		ImU32 color = IM_COL32(224, 255, 224, 255);
		ImU32 borderColor = IM_COL32(0, 0, 255, 255);
		ImU32 refColor = IM_COL32(0, 0, 0, 255);
		ImU32 guideColor = IM_COL32(255, 0, 0, 255);
		ImGui::ItemSize(size, style.FramePadding.y);
		ImU32 bkgColor = IM_COL32(15, 31, 15, 255);
		if (ImGui::ItemAdd(rect, ImGui::GetID("wsDisplay")))
		{

			dl->AddRectFilledMultiColor(
				inRect.Min,
				inRect.Max,
				bkgColor,
				bkgColor,
				bkgColor,
				bkgColor
			);

			int w = int(inRect.Max.x) - int(inRect.Min.x);
			if (w > count)
				w = count;
			if (w <= 0)
				w = 1;

			ImU32 rPos = 0;
			ImU32 rStep = (count << 16) / w;
			for (int i = 0; i < w; i++)
			{
				float x = (float)i / float(w);
				float y = 0.f;
				if (audio)
				{
					int8_t sv = int8_t((audio[rPos >> 16] >> voiceShift) & 0xff);
					y = float(sv * (1.0f / 256.f));
					rPos += rStep;
					{
						if (y < -0.5f) y = -0.5f;
						if (y > 0.5f) y = 0.5f;
					}
				}
				waveform[i] = ImLerp(inRect.Min, inRect.Max, ImVec2(x, 0.5f - y));
			}

			{
				dl->PushClipRectFullScreen();
				dl->AddPolyline(waveform, w, color, ImDrawFlags_None, dpiScale);
				dl->PopClipRect();
			}
			{
//				dl->AddRect(inRect.Min, inRect.Max, borderColor, 8.0f * dpiScale, 0, 1.5f * dpiScale);
			}
		}
	}
	ImGui::EndChild();
}

void ImDrawOscillo4Voices(const uint32_t* audio, int count)
{

	if (ImGui::Begin("Emulation", NULL, 0))
	{
		ImVec2 totalSize = ImGui::GetContentRegionAvail();
		ImDrawOscilloVoice(audio, count, 0, "Ym Voice A", ImVec2(totalSize.x*0.5f, totalSize.y * 0.5f));
		ImGui::SameLine();
		ImDrawOscilloVoice(audio, count, 1, "Ym Voice B", ImVec2(0, totalSize.y*0.5f));
		ImDrawOscilloVoice(audio, count, 2, "Ym Voice C", ImVec2(totalSize.x*0.5f, 0));
		ImGui::SameLine();
		ImDrawOscilloVoice(audio, count, 3, "STE DAC", ImVec2(0, 0));
	}
	ImGui::End();
}

void	SndhArchivePlayer::DropFile(const char* sFilename)
{

	if (gArchive.IsOpening())
		return;

	m_sndh.Unload();

	// first, try to open as a big ZIP archive
	bool loadOk = gArchive.Open(sFilename);

	if (!loadOk)
	{
		// if not zip archive, try to play the file as sndh
		loadOk = LoadNewMusic(sFilename);
	}

	if (loadOk)
	{
		WritePrivateProfileStringA("SNDH-Archive-Player", "ArchiveFile", sFilename, ".\\SNDH_Archive.ini");
	}
}

void	SndhArchivePlayer::Startup()
{
	char sFilename[_MAX_PATH];
	DWORD nc = GetPrivateProfileStringA("SNDH-Archive-Player", "ArchiveFile", "", sFilename, _MAX_PATH, ".\\SNDH_Archive.ini");
	if (nc > 0)
	{
		DropFile(sFilename);
	}
}

static void DrawTextCentered(const char* text)
{
	ImGui::SetCursorPosX( (ImGui::GetWindowWidth() - ImGui::CalcTextSize(text).x) * 0.5f);
	ImGui::Text(text);
}

static bool CenteredButton(const char* label, float widthScale = 1.2f)
{
	const float buttonWidth = ImGui::CalcTextSize(label).x * widthScale;
	ImGui::SetCursorPosX( ((ImGui::GetWindowWidth() - buttonWidth) * 0.5f));
	return ImGui::Button(label, ImVec2(buttonWidth, 0));
}

void	SndhArchivePlayer::UpdateImGui()
{
	
	
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);


	// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
	{
		static float f = 0.0f;
		static int counter = 0;

		//			ImGui::DockSpaceOverViewport(NULL, (ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoResize | ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoDocking | ImGuiDockNodeFlags_NoDockingSplitMe | ImGuiDockNodeFlags_NoDockingSplitOther));

		static char foos[8] = {};

		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		ImGui::Begin("Song Info");                          // Create a window called "Hello, world!" and append into it.
//				ImGui::InputText("mytxt", foos, sizeof(foos));

//		if (ImGui::BeginTable("song", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))

		SndhFile::SubSongInfo info;
		if (m_sndh.GetSubsongInfo(m_currentSubSong, info))
		{
			if (ImGui::BeginTable("song", 2, ImGuiTableFlags_NoBordersInBody))
			{
				ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				const int len = info.playerTickCount / info.playerTickRate;
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Song name:");
				ImGui::TableNextColumn();
				if (info.playerTickCount > 0)
				{
					const int len = info.playerTickCount / info.playerTickRate;
					ImGui::Text("%s (%d:%02d)", info.musicName, len / 60, len % 60);
				}
				else
					ImGui::Text("%s (?)", info.musicName);

				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Author:");
				ImGui::TableNextColumn();
				if (info.year)
					ImGui::Text("%s (%s)",info.musicAuthor, info.year);
				else
					ImGui::TextUnformatted(info.musicAuthor);

				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Tick rate:");
				ImGui::TableNextColumn();
				ImGui::Text("%d Hz", info.playerTickRate);

				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Sub-tune:");
				ImGui::TableNextColumn();
				int dir = 0;
				if (ImGui::ArrowButton("prev", ImGuiDir_Left))
					dir = -1;
				ImGui::SameLine();
				ImGui::Text("%d/%d", m_currentSubSong, m_sndh.GetSubsongCount());
				ImGui::SameLine();
				if (ImGui::ArrowButton("next", ImGuiDir_Right))
					dir = 1;

				if (dir)
				{
					int newSubsong = m_currentSubSong + dir;
					if (newSubsong < 1) newSubsong = 1;
					if (newSubsong > m_sndh.GetSubsongCount())
						newSubsong = m_sndh.GetSubsongCount();
					if (newSubsong != m_currentSubSong)
						StartSubsong(newSubsong);
				}

				ImGui::EndTable();

				m_sndh.DrawGui(info.musicName);

//				ImGui::Slider

			}

			//				ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
	/*
			ImGui::Checkbox("Oscillo", &drawOscillo);      // Edit bools storing our window open/close state
			ImGui::Checkbox("Another Window", &show_another_window);

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
			ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);
	*/
	//		ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state

		}


		{
			static char sBuf[128];
			sprintf_s(sBuf, "Default duration: %d min", gDefaultDurationInMin);
			if (ImGui::Button(sBuf))
				ImGui::OpenPopup("my_select_popup");
			if (ImGui::BeginPopup("my_select_popup"))
			{
				const char* names[] = { "4 minutes", "8 minutes", "12 minutes", "16 minutes", "32 minutes" };
				const int lens[] = { 4,8,12,16,32 };
				for (int i = 0; i < 5; i++)
					if (ImGui::Selectable(names[i]))
						gDefaultDurationInMin = lens[i];
				ImGui::EndPopup();
			}
		}

		if (ImGui::Button("About"))
			ImGui::OpenPopup("About");

		// Always center this window when appearing
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(480,400), ImGuiCond_FirstUseEver);

		if (ImGui::BeginPopupModal("About", NULL, ImGuiWindowFlags_AlwaysAutoResize))
		{
			DrawTextCentered("SNDH Archive Player v0.71");
			ImGui::Separator();
			extern void OsOpenInShell(const char* path);

			ImGui::Text("\n");
			DrawTextCentered("Accurate & fast ATARI SNDH player");
			DrawTextCentered("Written by Leonard/Oxygene");
			if (CenteredButton("Twitter"))
			{
				OsOpenInShell("https://twitter.com/leonard_coder");
			}
			ImGui::Text("\n");

			DrawTextCentered("Powered by AtariAudio library");
			DrawTextCentered("and DearImGui!");
			if (CenteredButton("GitHub Repository"))
			{
				OsOpenInShell("https://github.com/arnaud-carre/sndh-player");
			}
			ImGui::Text("\n");
			DrawTextCentered("You can also use awesome Web player");
			DrawTextCentered("written by Oxbab/Oxygene");
			if (CenteredButton("Web Player"))
			{
				OsOpenInShell("https://sndh.oxygenedemos.com/");
			}

			ImGui::Text("\n\n");

			//static int unused_i = 0;
			//ImGui::Combo("Combo", &unused_i, "Delete\0Delete harder\0");

			if (CenteredButton("Ok", 6.0f))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemDefaultFocus();
			ImGui::EndPopup();
		}



//		ImGui::SliderInt("Default Duration (min)", &gDefaultDurationInMin, 1, 30);

//		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		ImGui::End();
	}

	if (drawOscillo)
	{
		uint32_t* debugAudio = NULL;
		const int16_t* display = m_sndh.GetDisplaySampleData(kLatencySampleCount, &debugAudio);
		ImDrawOscillo(display, kLatencySampleCount,"Oscilloscope");
		ImDrawOscillo4Voices(debugAudio, kLatencySampleCount);
	}

	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
/*
	show_demo_window = true;
 	if (show_demo_window)
 		ImGui::ShowDemoWindow(&show_demo_window);
*/
	static MemoryEditor mem_edit;
	mem_edit.ReadOnly = true;
	int fsize;
	const void* rdata = m_sndh.GetRawData(fsize);
	mem_edit.DrawWindow("File Viewer", (void*)rdata, fsize);

	DrawPlayList();

	ImGui::PopStyleVar();

}

void	SndhArchivePlayer::Shutdown()
{
}
