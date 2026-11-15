#include "AppIcon.h"
#ifdef _WIN32
#include <windows.h>
#include <limits.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#include <vector>
#endif

cui_texture LoadAboutIcon(cui_ctx* ui)
{
#ifdef _WIN32
	HMODULE module = GetModuleHandleW(nullptr);
	HRSRC resource = FindResourceW(module, L"PLAYER_ABOUT_ICON", MAKEINTRESOURCEW(10));
	if (!resource) return 0;
	const DWORD size = SizeofResource(module, resource);
	HGLOBAL loaded = LoadResource(module, resource);
	const void* data = loaded ? LockResource(loaded) : nullptr;
	if (!data || !size || size > INT_MAX) return 0;
	return cui_image_load_memory(ui, data, int(size), nullptr, nullptr);
#else
	CFBundleRef bundle = CFBundleGetMainBundle();
	CFURLRef url = bundle ? CFBundleCopyResourceURL(bundle, CFSTR("app-about"), CFSTR("png"), nullptr) : nullptr;
	if (!url) return 0;
	CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
	CFRelease(url);
	if (!path) return 0;
	std::vector<char> utf8(CFStringGetMaximumSizeForEncoding(CFStringGetLength(path), kCFStringEncodingUTF8) + 1);
	const bool valid = CFStringGetCString(path, utf8.data(), utf8.size(), kCFStringEncodingUTF8);
	CFRelease(path);
	return valid ? cui_image_load(ui, utf8.data(), nullptr, nullptr) : 0;
#endif
}
