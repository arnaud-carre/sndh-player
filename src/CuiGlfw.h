#pragma once

#include <cui/cui.h>
#include <GLFW/glfw3.h>

class CuiGlfw
{
public:
	CuiGlfw(GLFWwindow* window, cui_ctx* ui);
	~CuiGlfw();
	void UpdateViewport();
	CuiGlfw(const CuiGlfw&) = delete;
	CuiGlfw& operator=(const CuiGlfw&) = delete;

private:
	static CuiGlfw& Get(GLFWwindow* window);
	static float WindowScale(GLFWwindow* window);
	static void MouseMove(GLFWwindow* window, double x, double y);
	static void MouseEnter(GLFWwindow* window, int entered);
	static void MouseButton(GLFWwindow* window, int button, int action, int mods);
	static void Scroll(GLFWwindow* window, double x, double y);
	static void Key(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void Character(GLFWwindow* window, unsigned int codepoint);
	static void Focus(GLFWwindow* window, int focused);
	static void SetClipboard(void* user, const char* text);
	static const char* GetClipboard(void* user);
	static void SetCursor(void* user, cui_cursor cursor);
	GLFWwindow* m_window;
	cui_ctx* m_ui;
	GLFWcursor* m_cursors[CUI_CURSOR_COUNT] = {};
};
