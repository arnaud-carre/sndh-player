//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#define _CRT_SECURE_NO_WARNINGS
#include "AppData.h"
#include <string.h>
#include "Ui.h"
#include <algorithm>
#include <stdio.h>
#include <string>
#include "../AtariAudio/src/AtariAudio.h"
#include "SndhArchivePlayer.h"
#include "SndhArchive.h"
#include "AsyncSndhStream.h"


static int gDefaultDurationInMin = 4;

static	SndhArchive	gArchive;





SndhArchivePlayer::SndhArchivePlayer()
{
}

SndhArchivePlayer::~SndhArchivePlayer()
{
}

bool	SndhArchivePlayer::StartSubsong(int subsong)
{
	bool ret = false;
	const AtariAudioRenderer* sf = m_sndh.GetSndhFile();
	if (sf && (subsong >= 1) && (subsong <= sf->GetSongInfo().subsongCount))
	{
		if (m_sndh.StartSubsong(subsong, gDefaultDurationInMin*60))
		{
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

		if (m_sndh.LoadSndh(sndhBuffer, sndhSize, kHostReplayRate))
		{
			if ( StartSubsong(m_sndh.GetSndhFile()->GetSongInfo().defaultSubsong))
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
			m_currentZipIndex = zipIndex;

			size_t size = zip_entry_size(zipArchive);
			void* unpack = calloc(1, size);

			size_t depackSize = zip_entry_noallocread(zipArchive, unpack, size);
			if (depackSize == size)
			{
				m_sndh.Unload();
				if (m_sndh.LoadSndh(unpack, int(size), kHostReplayRate))
				{
					StartSubsong(m_sndh.GetSndhFile()->GetSongInfo().defaultSubsong);
				}
			}
			free(unpack);
		}
	}
}

void SndhArchivePlayer::Navigate(int direction)
{
	const AtariAudioRenderer* sf = m_sndh.GetSndhFile();
	if (!sf || !direction) return;
	int next = m_currentSubSong + direction;
	const int count = sf->GetSongInfo().subsongCount;
	int zipIndex = -1;
	int targetSubsong = 1;
	if (direction > 0 && m_sndh.GetPlayMode() == AsyncSndhStream::PlayMode_Random)
		gArchive.GetRandomFilteredSong(zipIndex, targetSubsong);
	else if (next < 1)
		zipIndex = gArchive.GetPrevFilteredZipIndex(m_currentZipIndex);
	else if (next > count)
		zipIndex = gArchive.GetNextFilteredZipIndex(m_currentZipIndex);
	if (zipIndex >= 0)
	{
		PlayZipEntry(gArchive, zipIndex);
		if (targetSubsong > 1) StartSubsong(targetSubsong);
	}
	else if (next >= 1 && next <= count)
		StartSubsong(next);
}

void SndhArchivePlayer::Update()
{
	gArchive.Update();
	if (m_pendingZipIndex >= 0)
	{
		PlayZipEntry(gArchive, m_pendingZipIndex);
		m_pendingZipIndex = -1;
		m_pendingDirection = 0;
		return;
	}
	const int direction = m_pendingDirection;
	m_pendingDirection = 0;
	if (direction) Navigate(direction);
	else if (m_sndh.GetSndhFile() && m_sndh.SubsongReachedEnd())
	{
		if (m_sndh.GetPlayMode() == AsyncSndhStream::PlayMode_Loop)
			m_sndh.SetReplayPosInSec(0);
		else
			Navigate(1);
	}
}

bool SndhArchivePlayer::NeedsContinuousFrames() const
{
	return gArchive.IsOpening() || (m_sndh.GetSndhFile() && !m_sndh.IsPaused());
}

void SndhArchivePlayer::DropFile(const char* filename)
{
	if (gArchive.IsOpening()) return;
	m_sndh.Unload();
	m_currentZipIndex = -1;
	m_pendingDirection = 0;
	bool loaded = gArchive.Open(filename);
	m_pendingZipIndex = -1;
	if (!loaded) loaded = LoadNewMusic(filename);
	if (loaded) SaveAppData("ArchiveFile", filename);
}

void SndhArchivePlayer::Startup(cui_ctx* ui)
{
	char value[4096];
	if (LoadAppData("CuiTheme", value, sizeof(value)))
		cui_set_theme(ui, strcmp(value, "light") == 0 ? CUI_THEME_LIGHT : CUI_THEME_DARK);
	if (LoadAppData("CuiSplits", value, sizeof(value)))
	{
		float top, song, archive;
		if (sscanf(value, "%f %f %f", &top, &song, &archive) == 3 &&
			top >= 0.1f && top <= 0.9f && song >= 0.1f && song <= 0.9f && archive >= 0.1f && archive <= 0.9f)
		{
			m_topRatio = top;
			m_songRatio = song;
			m_archiveRatio = archive;
		}
	}
	if (LoadAppData("ArchiveFile", value, sizeof(value))) DropFile(value);
}

void SndhArchivePlayer::Shutdown(cui_ctx* ui)
{
	char splits[96];
	snprintf(splits, sizeof(splits), "%.6f %.6f %.6f", m_topRatio, m_songRatio, m_archiveRatio);
	SaveAppData("CuiSplits", splits);
	SaveAppData("CuiTheme", cui_get_theme_mode(ui) == CUI_THEME_LIGHT ? "light" : "dark");
	m_sndh.Unload();
	gArchive.Close();
}

static void Detail(cui_ctx* ui, const char* label, const char* value)
{
	Ui::Row row(ui);
	cui_text_opts opts = {};
	opts.width = CUI_PX(112);
	opts.color = CUI_COLOR_TEXT_SECONDARY;
	cui_text_ex(ui, label, &opts);
	cui_text_ex(ui, value ? value : "", nullptr);
}

void SndhArchivePlayer::DrawSongInfo(cui_ctx* ui)
{
	cui_scroll_opts scroll = {};
	scroll.height = CUI_FILL;
	scroll.width = CUI_FILL;
	scroll.pad = 16;
	scroll.gap = 10;
	cui_scroll_begin(ui, &scroll);
	const AtariAudioRenderer* sf = m_sndh.GetSndhFile();
	if (sf)
	{
		const auto& info = sf->GetSongInfo();
		{
			Ui::Row row(ui, 6);
			cui_text_opts title = {};
			title.font = CUI_FONT_MEDIUM;
			title.size = 17;
			title.nowrap = true;
			title.width = CUI_FIT;
			title.align = CUI_TEXT_LEFT;
			std::string caption = info.musicName[0] ? info.musicName : "Untitled";
			if (info.musicAuthor && info.musicAuthor[0])
			{
				caption += " by ";
				caption += info.musicAuthor;
			}
			cui_text_ex(ui, caption.c_str(), &title);
			cui_button_opts details = {};
			details.icon = m_showDetails ? CUI_ICON_CHEVRON_DOWN : CUI_ICON_CHEVRON_RIGHT;
			details.tooltip = m_showDetails ? "Hide details" : "Show details";
			details.variant = CUI_BUTTON_OUTLINE;
			details.size = CUI_SIZE_SM;
			details.width = CUI_PX(30);
			cui_theme* theme = cui_theme_get(ui);
			const float radius = theme->radius_control;
			theme->radius_control = 15;
			if (cui_button_ex(ui, "##song-details", &details)) m_showDetails = !m_showDetails;
			theme->radius_control = radius;
		}
		if (m_showDetails)
		{
			cui_separator(ui);
			Detail(ui, "File type", info.fileFormat);
			if (info.fileType == AtariAudioRenderer::eFileType::eSndh)
			{
				Detail(ui, "Year", info.year);
				Detail(ui, "Converter", info.converter);
				Detail(ui, "Ripper", info.ripper);
			}
			else Detail(ui, "Comment", info.converter);
			char value[96];
			const uint32_t ms = sf->SampleToMs(sf->GetSubsongDurationSample(m_currentSubSong));
			snprintf(value, sizeof(value), "%u:%02u.%03u", ms / 60000, (ms / 1000) % 60, ms % 1000);
			Detail(ui, "Duration", value);
			snprintf(value, sizeof(value), "%d Hz", info.playerTickRate); Detail(ui, "Player rate", value);
			snprintf(value, sizeof(value), "%.3f MHz", info.ym2149Clock / 1000000.0); Detail(ui, "YM2149 clock", value);
			snprintf(value, sizeof(value), "%.1f kHz", info.hostReplayRate / 1000.0); Detail(ui, "Output rate", value);
			snprintf(value, sizeof(value), "%u bytes", unsigned(info.rawBinaryDataSize)); Detail(ui, "Data size", value);
		}
		if (info.subsongCount >= 2)
		{
			Ui::Row row(ui);
			if (Ui::IconButton(ui, CUI_ICON_CHEVRON_LEFT, "Previous sub-tune / track")) m_pendingDirection = -1;
			cui_textf(ui, "Sub-tune %d / %d", m_currentSubSong, info.subsongCount);
			if (Ui::IconButton(ui, CUI_ICON_CHEVRON_RIGHT, "Next sub-tune / track")) m_pendingDirection = 1;
		}
		m_sndh.DrawGui(ui);
	}
	else
	{
		cui_heading(ui, "Your Atari music collection", 3);
		cui_secondary(ui, "Drop a ZIP archive, SNDH or YM file into this window to start listening.");
	}
	{
		Ui::Row row(ui, 8);
		if (Ui::Button(ui, "About")) m_showAbout = true;
		cui_menu_opts options = {};
		options.width = 240;
		if (cui_menu_begin(ui, "Options", &options))
		{
			m_sndh.DrawOptions(ui, sf ? sf->GetSongInfo().musicName : nullptr);
			cui_menu_separator(ui);
			cui_menu_label(ui, "Default length");
			static const char* durations[] = { "4 minutes", "8 minutes", "12 minutes", "16 minutes", "32 minutes" };
			static const int minutes[] = { 4, 8, 12, 16, 32 };
			int duration = 0;
			for (int i = 0; i < 5; i++) if (minutes[i] == gDefaultDurationInMin) duration = i;
			cui_select_opts durationOptions = {};
			durationOptions.width = CUI_FILL;
			if (cui_select_ex(ui, "default-duration", durations, 5, &duration, &durationOptions))
				gDefaultDurationInMin = minutes[duration];
			cui_menu_separator(ui);
			cui_menu_label(ui, "Appearance");
			cui_menu_item_opts dark = {};
			dark.checked = cui_get_theme_mode(ui) == CUI_THEME_DARK;
			if (cui_menu_item_ex(ui, "Dark", &dark)) cui_set_theme(ui, CUI_THEME_DARK);
			cui_menu_item_opts light = {};
			light.checked = cui_get_theme_mode(ui) == CUI_THEME_LIGHT;
			if (cui_menu_item_ex(ui, "Light", &light)) cui_set_theme(ui, CUI_THEME_LIGHT);
			cui_menu_end(ui);
		}
	}
	cui_scroll_end(ui);
}

static const int kScopeSamples = kHostReplayRate / 60;
struct ScopeData
{
	const int16_t* audio;
	const uint32_t* voices;
	int voice;
};

static void PaintScope(cui_ctx* ui, cui_rect rect, void* user)
{
	const ScopeData& data = *static_cast<const ScopeData*>(user);
	cui_draw_rect(ui, rect, CUI_CORNERS(8), CUI_COLOR_SURFACE_MUTED);
	cui_push_clip(ui, rect);
	const float center = rect.y + rect.h * 0.5f;
	cui_draw_line(ui, { rect.x, center }, { rect.x + rect.w, center }, 1, CUI_COLOR_BORDER);
	cui_vec2 points[kScopeSamples];
	const int count = (std::max)(2, (std::min)(kScopeSamples, int(rect.w)));
	for (int i = 0; i < count; i++)
	{
		const int sample = i * (kScopeSamples - 1) / (count - 1);
		float value = 0;
		if (data.voice < 0 && data.audio) value = data.audio[sample] / 65536.0f;
		else if (data.voices) value = int8_t((data.voices[sample] >> (data.voice * 8)) & 255) / 256.0f;
		points[i] = { rect.x + rect.w * i / (count - 1), center - value * (rect.h - 12) };
	}
	cui_draw_polyline(ui, points, count, 1.35f, data.voice < 0 ? CUI_COLOR_ACCENT : CUI_COLOR_SERIES(data.voice), false);
	cui_pop_clip(ui);
}

void SndhArchivePlayer::DrawGui(cui_ctx* ui)
{
	Ui::Column root(ui, CUI_FILL, 10, 14);
	// Fixed panel arrangement: only the dividers can move.
	cui_split_opts vertical = {};
	vertical.vertical = true;
	vertical.min_first = 120;
	vertical.min_second = 120;
	cui_split_begin(ui, "player-top-bottom", &m_topRatio, &vertical);
	cui_split_opts horizontal = {};
	horizontal.min_first = 200;
	horizontal.min_second = 200;
	cui_split_begin(ui, "player-song-output", &m_songRatio, &horizontal);
	{
		Ui::Column panel(ui, CUI_FILL, 0, 0, CUI_COLOR_SURFACE);
		DrawSongInfo(ui);
	}
	uint32_t* voices = nullptr;
	const int16_t* audio = m_sndh.GetDisplaySampleData(kScopeSamples, &voices);
	cui_split_next(ui);
	{
		Ui::Column panel(ui, CUI_FILL, 0, 0, CUI_COLOR_SURFACE);
		Ui::Column column(ui, CUI_FILL, 8, 14);
		cui_muted(ui, "MASTER OUTPUT");
		ScopeData scope = { audio, voices, -1 };
		cui_canvas(ui, "master-scope", CUI_FILL, CUI_FILL, PaintScope, &scope, sizeof(scope));
	}
	cui_split_end(ui);
	cui_split_next(ui);
	cui_split_begin(ui, "player-archive-voices", &m_archiveRatio, &vertical);
	{
		Ui::Column panel(ui, CUI_FILL, 0, 0, CUI_COLOR_SURFACE);
		const cui_doc_tab tabs[] = { { kWndSndhArchive }, { kWndFileViewer } };
		cui_doc_tabs_opts tabOptions = {};
		tabOptions.no_close = true;
		tabOptions.no_reorder = true;
		tabOptions.shortcuts = true;
		cui_doc_tabs_ex(ui, "archive-tabs", tabs, 2, &m_archiveTab, &tabOptions);
		if (m_archiveTab == 0)
			gArchive.DrawGui(ui, *this);
		else
		{
			Ui::Column column(ui, CUI_FILL, 8, 12);
			const auto* sf = m_sndh.GetSndhFile();
			if (sf)
			{
				const auto& info = sf->GetSongInfo();
				cui_hex_view_opts opts = {};
				opts.height = -1;
				cui_hex_view_ex(ui, "file-bytes", static_cast<const uint8_t*>(info.rawBinaryData), info.rawBinaryDataSize, nullptr, &opts);
			}
			else cui_secondary(ui, "Load a track to inspect its file.");
		}
	}
	cui_split_next(ui);
	{
		Ui::Column panel(ui, CUI_FILL, 0, 0, CUI_COLOR_SURFACE);
		Ui::Column column(ui, CUI_FILL, 8, 12);
		const char* names[] = { "YM VOICE A", "YM VOICE B", "YM VOICE C", "STE DAC" };
		for (int rowIndex = 0; rowIndex < 2; rowIndex++)
		{
			Ui::Row row(ui, 12, false, CUI_FILL);
			for (int columnIndex = 0; columnIndex < 2; columnIndex++)
			{
				const int i = rowIndex * 2 + columnIndex;
				cui_push_id_int(ui, i);
				{
					Ui::Column voice(ui, CUI_FILL, 4);
					cui_muted(ui, names[i]);
					ScopeData scope = { audio, voices, i };
					cui_canvas(ui, "voice-scope", CUI_FILL, CUI_FILL, PaintScope, &scope, sizeof(scope));
				}
				cui_pop_id(ui);
			}
		}
	}
	cui_split_end(ui);
	cui_split_end(ui);
	cui_dialog_opts about = {};
	about.width = 400;
	about.center_title = true;
	if (cui_dialog_begin(ui, "About", &m_showAbout, &about))
	{
		{
			Ui::Column content(ui, CUI_FIT, 8);
			auto text = [&](const char* label)
			{
				cui_text_opts opts = {};
				opts.width = CUI_FILL;
				opts.align = CUI_TEXT_CENTER;
				cui_text_ex(ui, label, &opts);
			};
			auto button = [&](const char* label)
			{
				cui_box row = {};
				row.width = CUI_FILL;
				row.justify = CUI_JUSTIFY_CENTER;
				cui_row_begin(ui, &row);
				const bool clicked = Ui::Button(ui, label);
				cui_box_end(ui);
				return clicked;
			};
			text("SNDH & YM Archive Player v" SNDH_ARCHIVE_PLAYER_VERSION);
			cui_separator(ui);
			cui_gap(ui, 4);
			text("Accurate & fast ATARI SNDH & YM player");
			text("Written by Leonard/Oxygene");
			extern void OsOpenInShell(const char* path);
			if (button("GitHub Repository")) OsOpenInShell("https://github.com/arnaud-carre/sndh-player");
			cui_gap(ui, 4);
			text("Powered by AtariAudio library v" ATARI_AUDIO_VERSION);
			if (button("AtariAudio Repository")) OsOpenInShell("https://github.com/arnaud-carre/AtariAudio");
			cui_gap(ui, 4);
			text("You can also use awesome Web player");
			text("written by Oxbab/Oxygene");
			if (button("Web Player")) OsOpenInShell("https://sndh.oxygenedemos.com/");
			cui_gap(ui, 4);
			text("Interface powered by CUI v" CUI_VERSION_STRING);
			if (button("Ok")) m_showAbout = false;
		}
		cui_dialog_end(ui);
	}
}
