#pragma once

#include <cui/cui.h>

// Small C++17 helpers for CUI's explicit C API.
namespace Ui
{

class Column
{
public:
	Column(cui_ctx* ui, cui_size height = CUI_FIT, float gap = 10, float pad = 0, cui_color bg = CUI_TRANSPARENT) : m_ui(ui)
	{
		cui_box box = {};
		box.width = CUI_FILL;
		box.height = height;
		box.gap = gap;
		box.pad = pad;
		box.bg = bg;
		cui_column_begin(ui, &box);
	}
	~Column() { cui_box_end(m_ui); }
	Column(const Column&) = delete;
	Column& operator=(const Column&) = delete;
private:
	cui_ctx* m_ui;
};

class Row
{
public:
	Row(cui_ctx* ui, float gap = 8, bool wrap = false, cui_size height = CUI_FIT) : m_ui(ui)
	{
		cui_box box = {};
		box.width = CUI_FILL;
		box.height = height;
		box.gap = gap;
		box.wrap = wrap;
		cui_row_begin(ui, &box);
	}
	~Row() { cui_box_end(m_ui); }
	Row(const Row&) = delete;
	Row& operator=(const Row&) = delete;
private:
	cui_ctx* m_ui;
};

inline bool Button(cui_ctx* ui, const char* label, bool disabled = false, const char* icon = nullptr)
{
	cui_button_opts opts = {};
	opts.disabled = disabled;
	opts.icon = icon;
	opts.size = CUI_SIZE_SM;
	return cui_button_ex(ui, label, &opts);
}

inline bool IconButton(cui_ctx* ui, const char* icon, const char* tooltip, bool disabled = false)
{
	cui_button_opts opts = {};
	opts.tooltip = tooltip;
	opts.disabled = disabled;
	opts.size = CUI_SIZE_SM;
	return cui_icon_button_ex(ui, icon, &opts);
}

}
