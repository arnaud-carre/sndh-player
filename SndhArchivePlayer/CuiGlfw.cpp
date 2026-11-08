#include "CuiGlfw.h"

CuiGlfw& CuiGlfw::Get(GLFWwindow* window)
{
	return *static_cast<CuiGlfw*>(glfwGetWindowUserPointer(window));
}

float CuiGlfw::WindowScale(GLFWwindow* window)
{
#ifdef __APPLE__
	return 1.0f; // Cocoa cursor and window coordinates are already logical points.
#else
	float x = 1, y = 1;
	glfwGetWindowContentScale(window, &x, &y);
	return x > 0 ? x : 1;
#endif
}

CuiGlfw::CuiGlfw(GLFWwindow* window, cui_ctx* ui) : m_window(window), m_ui(ui)
{
	glfwSetWindowUserPointer(window, this);
	glfwSetCursorPosCallback(window, MouseMove);
	glfwSetCursorEnterCallback(window, MouseEnter);
	glfwSetMouseButtonCallback(window, MouseButton);
	glfwSetScrollCallback(window, Scroll);
	glfwSetKeyCallback(window, Key);
	glfwSetCharCallback(window, Character);
	glfwSetWindowFocusCallback(window, Focus);
	const int shapes[] = { GLFW_ARROW_CURSOR, GLFW_HAND_CURSOR, GLFW_IBEAM_CURSOR,
		GLFW_HRESIZE_CURSOR, GLFW_VRESIZE_CURSOR, GLFW_ARROW_CURSOR, GLFW_ARROW_CURSOR };
	for (int i = 0; i < CUI_CURSOR_COUNT; i++)
		m_cursors[i] = glfwCreateStandardCursor(shapes[i]);
	cui_platform_iface platform = {};
	platform.user = this;
	platform.set_clipboard = SetClipboard;
	platform.get_clipboard = GetClipboard;
	platform.set_cursor = SetCursor;
	cui_set_platform(ui, &platform);
	cui_input_focus(ui, glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0);
	double x, y;
	glfwGetCursorPos(window, &x, &y);
	MouseMove(window, x, y);
}

CuiGlfw::~CuiGlfw()
{
	glfwSetCursorPosCallback(m_window, nullptr);
	glfwSetCursorEnterCallback(m_window, nullptr);
	glfwSetMouseButtonCallback(m_window, nullptr);
	glfwSetScrollCallback(m_window, nullptr);
	glfwSetKeyCallback(m_window, nullptr);
	glfwSetCharCallback(m_window, nullptr);
	glfwSetWindowFocusCallback(m_window, nullptr);
	glfwSetWindowUserPointer(m_window, nullptr);
	cui_set_platform(m_ui, nullptr);
	glfwSetCursor(m_window, nullptr);
	for (GLFWcursor* cursor : m_cursors)
		if (cursor) glfwDestroyCursor(cursor);
}

void CuiGlfw::UpdateViewport()
{
	int width, height, pixelWidth, pixelHeight;
	glfwGetWindowSize(m_window, &width, &height);
	glfwGetFramebufferSize(m_window, &pixelWidth, &pixelHeight);
	const float scale = WindowScale(m_window);
	const float pixelScale = width > 0 ? float(pixelWidth) / width * scale : scale;
	cui_set_viewport(m_ui, width / scale, height / scale, pixelScale > 0 ? pixelScale : 1);
	// Reapply the pointer conversion when the window changes DPI without a move event.
	double x, y;
	glfwGetCursorPos(m_window, &x, &y);
	MouseMove(m_window, x, y);
}

void CuiGlfw::MouseMove(GLFWwindow* window, double x, double y)
{
	const float scale = WindowScale(window);
	cui_input_mouse_move(Get(window).m_ui, float(x) / scale, float(y) / scale);
}

void CuiGlfw::MouseEnter(GLFWwindow* window, int entered)
{
	if (!entered) cui_input_mouse_leave(Get(window).m_ui);
}

void CuiGlfw::MouseButton(GLFWwindow* window, int button, int action, int)
{
	if (button >= GLFW_MOUSE_BUTTON_LEFT && button <= GLFW_MOUSE_BUTTON_MIDDLE)
		cui_input_mouse_button(Get(window).m_ui, static_cast<cui_mouse_button>(button), action == GLFW_PRESS);
}

void CuiGlfw::Scroll(GLFWwindow* window, double x, double y)
{
	cui_input_scroll(Get(window).m_ui, float(-x), float(y));
}

void CuiGlfw::Key(GLFWwindow* window, int key, int, int action, int mods)
{
	cui_key mapped = CUI_KEY_NONE;
	if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) mapped = cui_key(CUI_KEY_A + key - GLFW_KEY_A);
	else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) mapped = cui_key(CUI_KEY_0 + key - GLFW_KEY_0);
	else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) mapped = cui_key(CUI_KEY_F1 + key - GLFW_KEY_F1);
	else switch (key)
	{
	case GLFW_KEY_TAB: mapped = CUI_KEY_TAB; break;
	case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: mapped = CUI_KEY_ENTER; break;
	case GLFW_KEY_ESCAPE: mapped = CUI_KEY_ESCAPE; break;
	case GLFW_KEY_BACKSPACE: mapped = CUI_KEY_BACKSPACE; break;
	case GLFW_KEY_DELETE: mapped = CUI_KEY_DELETE; break;
	case GLFW_KEY_SPACE: mapped = CUI_KEY_SPACE; break;
	case GLFW_KEY_LEFT: mapped = CUI_KEY_LEFT; break;
	case GLFW_KEY_RIGHT: mapped = CUI_KEY_RIGHT; break;
	case GLFW_KEY_UP: mapped = CUI_KEY_UP; break;
	case GLFW_KEY_DOWN: mapped = CUI_KEY_DOWN; break;
	case GLFW_KEY_HOME: mapped = CUI_KEY_HOME; break;
	case GLFW_KEY_END: mapped = CUI_KEY_END; break;
	case GLFW_KEY_PAGE_UP: mapped = CUI_KEY_PAGE_UP; break;
	case GLFW_KEY_PAGE_DOWN: mapped = CUI_KEY_PAGE_DOWN; break;
	case GLFW_KEY_MINUS: mapped = CUI_KEY_MINUS; break;
	case GLFW_KEY_EQUAL: mapped = CUI_KEY_EQUAL; break;
	case GLFW_KEY_COMMA: mapped = CUI_KEY_COMMA; break;
	case GLFW_KEY_PERIOD: mapped = CUI_KEY_PERIOD; break;
	case GLFW_KEY_SLASH: mapped = CUI_KEY_SLASH; break;
	}
	int modifiers = 0;
	if (mods & GLFW_MOD_SHIFT) modifiers |= CUI_MOD_SHIFT;
	if (mods & GLFW_MOD_CONTROL) modifiers |= CUI_MOD_CTRL;
	if (mods & GLFW_MOD_ALT) modifiers |= CUI_MOD_ALT;
	if (mods & GLFW_MOD_SUPER) modifiers |= CUI_MOD_SUPER;
	cui_input_key(Get(window).m_ui, mapped, action != GLFW_RELEASE, modifiers);
}

void CuiGlfw::Character(GLFWwindow* window, unsigned int cp)
{
	char text[5] = {};
	if (cp < 0x80) text[0] = char(cp);
	else if (cp < 0x800)
	{
		text[0] = char(0xc0 | (cp >> 6)); text[1] = char(0x80 | (cp & 63));
	}
	else if (cp < 0x10000 && !(cp >= 0xd800 && cp <= 0xdfff))
	{
		text[0] = char(0xe0 | (cp >> 12)); text[1] = char(0x80 | ((cp >> 6) & 63)); text[2] = char(0x80 | (cp & 63));
	}
	else if (cp >= 0x10000 && cp <= 0x10ffff)
	{
		text[0] = char(0xf0 | (cp >> 18)); text[1] = char(0x80 | ((cp >> 12) & 63));
		text[2] = char(0x80 | ((cp >> 6) & 63)); text[3] = char(0x80 | (cp & 63));
	}
	cui_input_text(Get(window).m_ui, text);
}

void CuiGlfw::Focus(GLFWwindow* window, int focused)
{
	cui_input_focus(Get(window).m_ui, focused != 0);
}

void CuiGlfw::SetClipboard(void* user, const char* text)
{
	glfwSetClipboardString(static_cast<CuiGlfw*>(user)->m_window, text);
}

const char* CuiGlfw::GetClipboard(void* user)
{
	const char* text = glfwGetClipboardString(static_cast<CuiGlfw*>(user)->m_window);
	return text ? text : "";
}

void CuiGlfw::SetCursor(void* user, cui_cursor cursor)
{
	auto& bridge = *static_cast<CuiGlfw*>(user);
	if (cursor >= 0 && cursor < CUI_CURSOR_COUNT)
		glfwSetCursor(bridge.m_window, bridge.m_cursors[cursor]);
}
