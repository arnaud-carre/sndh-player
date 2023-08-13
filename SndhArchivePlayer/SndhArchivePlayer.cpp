#define _CRT_SECURE_NO_WARNINGS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#include "imgui_memory_editor.h"
#include "AudioWriter.h"
#include "../AtariAudio/AtariAudio.h"
#include "SndhArchivePlayer.h"
#include "SndhArchive.h"

static const int kHostReplayRate = 44100;
static const int kLatencySampleCount = kHostReplayRate / 60;

static	SndhArchive	gArchive;


SndhArchivePlayer::SndhArchivePlayer()
{
	m_musicPlaying = false;
}

SndhArchivePlayer::~SndhArchivePlayer()
{

}

void	SndhArchivePlayer::StopAudio()
{
	if (m_musicPlaying)
	{
		m_musicPlaying = false;
		m_audioWriter.Close();
	}
}

bool	SndhArchivePlayer::StartSubsong(int subsong)
{
	bool ret = false;
	if ((subsong >= 1) && (subsong <= m_sndh.GetSubsongCount()))
	{
		StopAudio();
		if (m_sndh.InitSubSong(subsong))
		{
			m_audioWriter.Open(kLatencySampleCount, kHostReplayRate, false);
			m_musicPlaying = true;
			m_currentSubSong = subsong;
			ret = true;
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

		if (m_sndh.Load(sndhBuffer, sndhSize, kHostReplayRate))
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
				if (m_sndh.Load(unpack, int(size), kHostReplayRate))
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
	ImGui::SetNextWindowSizeConstraints(ImVec2(64.0f*dpiScale, 32.0f*dpiScale), ImVec2(1280, 800));
	if (ImGui::Begin(winName, &drawOscillo, 0))
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
		ImU32 bkgColor = IM_COL32(63, 63, 63, 255);
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


void	SndhArchivePlayer::DropFile(const char* sFilename)
{

	StopAudio();
	m_sndh.Unload();

	// first, try to open as a big ZIP archive
	if (!gArchive.Open(sFilename))
	{
		// if not zip archive, try to play the file as sndh
		LoadNewMusic(sFilename);
	}


}

void	SndhArchivePlayer::Startup()
{
//	DropFile("C:\\Users\\arnaud\\Downloads\\sndh48lf.zip");
}

void	SndhArchivePlayer::UpdateImGui()
{
	
	
	ImGuiIO& io = ImGui::GetIO(); (void)io;

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
		if (m_sndh.GetSubsongInfo(1, info))
		{
			if (ImGui::BeginTable("song", 2, ImGuiTableFlags_NoBordersInBody))
			{
				ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				const int len = info.playerTickCount / info.playerTickRate;
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Song name:");
				ImGui::TableNextColumn();
				ImGui::Text("%s (%d:%02d)", info.musicName, len / 60, len % 60);

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

				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Play pos:");
				ImGui::TableNextColumn();
				uint32_t psec = m_audioWriter.GetPlayTime();
				ImGui::Text("%d:%02d", psec/60, psec%60);


				ImGui::EndTable();
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
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		if (ImGui::Button("AtariAudio & SNDH Player by Leonard/Oxygene"))
		{
			extern void OsOpenInShell(const char* path);
			OsOpenInShell("https://github.com/arnaud-carre/sndh-player");
		}
		ImGui::End();
	}

	if (m_musicPlaying)
	{
		int count;
		int16_t* p;
		while (p = m_audioWriter.GetAudioBlockToFillIfAny(count))
			m_sndh.AudioRender(p, kLatencySampleCount);
	}

	if (drawOscillo)
	{
		const int16_t* display = m_audioWriter.GetDisplayBuffer();
		ImDrawOscillo(display, kLatencySampleCount,"Oscilloscope");
//		ImDrawOscillo(display, kLatencySampleCount, "Oscilloscope2");
	}

	// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
// 	if (show_demo_window)
// 		ImGui::ShowDemoWindow(&show_demo_window);


	static MemoryEditor mem_edit;
	mem_edit.ReadOnly = true;
	mem_edit.DrawWindow("File Viewer", (void*)m_sndh.GetRawData(), m_sndh.GetRawDataSize());


	DrawPlayList();

}

void	SndhArchivePlayer::Shutdown()
{
	StopAudio();
}
