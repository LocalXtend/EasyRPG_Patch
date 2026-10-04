/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _WIN32
#  include <windows.h>
#endif

#if defined(_WIN32) && !defined(_ARM_)

// Headers
#include <algorithm>
#include <string>
#include "registry.h"
#include "utils.h"

/**
 * Adds Manifest depending on architecture.
 */
#ifdef _MSC_VER
	#if defined _M_IX86
	#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#elif defined _M_X64
	#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#else
	#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#endif
#endif

std::string Registry::ReadStrValue(HKEY hkey, std::string_view key, std::string_view val, REGVIEW view) {
	// RocketRPG: RegQueryValueExW gives UTF-16. The old code copied it into a char buffer and dropped the zero
	// bytes, which only works for ASCII paths (an RTP installed under a Korean/Japanese folder name became garbage).
	wchar_t value[1024];
	DWORD size = sizeof(value);
	DWORD type = REG_SZ;
	HKEY key_handle;
	REGSAM desired_access = KEY_QUERY_VALUE;

	switch (view) {
		case KEY32:
			desired_access |= KEY_WOW64_32KEY;
			break;
		case KEY64:
			desired_access |= KEY_WOW64_64KEY;
			break;
		case NATIVE:
		default:
			break;
	}

	std::wstring wkey = Utils::ToWideString(ToString(key));

	if (RegOpenKeyEx(hkey, wkey.c_str(), NULL, desired_access, &key_handle)) {
		return "";
	}

	std::wstring wval = Utils::ToWideString(ToString(val));

	if (RegQueryValueExW(key_handle, wval.c_str(), NULL, &type, reinterpret_cast<LPBYTE>(value), &size)) {
		RegCloseKey(key_handle);
		return "";
	}
	RegCloseKey(key_handle);
	if (type != REG_SZ && type != REG_EXPAND_SZ) {
		return "";
	}

	std::wstring wide(value, std::min<size_t>(size / sizeof(wchar_t), 1024));
	while (!wide.empty() && wide.back() == L'\0') {
		wide.pop_back();
	}
	if (type == REG_EXPAND_SZ) {
		wchar_t expanded[1024];
		DWORD n = ExpandEnvironmentStringsW(wide.c_str(), expanded, 1024);
		if (n > 0 && n <= 1024) wide = expanded;
	}
	return Utils::FromWideString(wide);
}

int Registry::ReadBinValue(HKEY hkey, std::string_view key, std::string_view val, unsigned char* bin, REGVIEW view) {
	DWORD size = 1024;
	DWORD type = REG_BINARY;
	HKEY key_handle;
	REGSAM desired_access = KEY_QUERY_VALUE;

	switch (view) {
		case KEY32:
			desired_access |= KEY_WOW64_32KEY;
			break;
		case KEY64:
			desired_access |= KEY_WOW64_64KEY;
			break;
		case NATIVE:
		default:
			break;
	}

	std::wstring wkey = Utils::ToWideString(ToString(key));

	if (RegOpenKeyEx(hkey, wkey.c_str(), NULL, desired_access, &key_handle)) {
		return 0;
	}

	std::wstring wval = Utils::ToWideString(ToString(val));

	if (RegQueryValueEx(key_handle, wval.c_str(), NULL, &type, bin, &size)) {
		return 0;
	}
	RegCloseKey(key_handle);

	return size;
}

#endif
