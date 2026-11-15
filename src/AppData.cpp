#define _CRT_SECURE_NO_WARNINGS
#include "AppData.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef _WIN32
bool SaveAppData(const char* keyName, const char* utf8Path)
{
	WritePrivateProfileStringA("SNDH-Archive-Player", keyName, utf8Path, ".\\SNDH_Archive.ini");
	return true;
}

bool LoadAppData(const char* keyName,char* bufferOut, size_t size)
{
	DWORD nc = GetPrivateProfileStringA("SNDH-Archive-Player", keyName, "", bufferOut, DWORD(size), ".\\SNDH_Archive.ini");
	return (nc > 0);
}

#else

static CFStringRef PreferencesDomain()
{
    return CFSTR("com.arnaudcarre.SndhArchivePlayer");
}

bool SaveAppData(const char* keyName, const char* utf8Path)
{
    if (!utf8Path)
        return false;

    CFStringRef key8 = CFStringCreateWithCString(kCFAllocatorDefault, keyName, kCFStringEncodingUTF8);
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, utf8Path, kCFStringEncodingUTF8);
    CFPreferencesSetAppValue(key8, path, PreferencesDomain());
    CFRelease(path);
    CFRelease(key8);
    return CFPreferencesAppSynchronize(PreferencesDomain());
}

bool LoadAppData(const char* keyName,char* buffer, size_t bufferSize)
{
    if (!buffer || bufferSize == 0)
        return false;

    buffer[0] = '\0';

    CFStringRef key8 = CFStringCreateWithCString(kCFAllocatorDefault, keyName, kCFStringEncodingUTF8);
    CFPropertyListRef value = CFPreferencesCopyAppValue(key8, PreferencesDomain());
    if (!value)
    {
        CFRelease(key8);
        return false;
    }

    bool success = false;

    if (CFGetTypeID(value) == CFStringGetTypeID())
    {
        success = CFStringGetCString( static_cast<CFStringRef>(value), buffer, static_cast<CFIndex>(bufferSize), kCFStringEncodingUTF8);
    }

    CFRelease(key8);
    CFRelease(value);

    if (!success)
        buffer[0] = '\0';

    return success;
}
#endif
