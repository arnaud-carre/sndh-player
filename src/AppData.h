#pragma once
#include <stddef.h>

bool SaveAppData(const char* keyName, const char* value);
bool LoadAppData(const char* keyName, char* buffer, size_t size);
