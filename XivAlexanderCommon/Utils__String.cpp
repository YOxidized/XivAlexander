#include "pch.h"
#include "Utils__String.h"
#include "Utils_Win32.h"

std::wstring Utils::FromUtf8(const std::string& in) {
	const size_t length = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), static_cast<int>(in.size()), nullptr, 0);
	std::wstring u16(length, 0);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.c_str(), static_cast<int>(in.size()), const_cast<LPWSTR>(u16.c_str()), static_cast<int>(u16.size()));
	return u16;
}

std::string Utils::ToUtf8(const std::wstring& u16) {
	const size_t length = WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), static_cast<int>(u16.size()), nullptr, 0, nullptr, nullptr);
	std::string u8(length, 0);
	WideCharToMultiByte(CP_UTF8, 0, u16.c_str(), static_cast<int>(u16.size()), const_cast<LPSTR>(u8.c_str()), static_cast<int>(u8.size()), nullptr, nullptr);
	return u8;
}

std::string Utils::ToString(const sockaddr_in& sa) {
	if (sa.sin_family != AF_INET)
		return FormatString("sockaddr_in?(AF_INET=%d)", sa.sin_family);

	char s[INET_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET, &sa.sin_addr, s, sizeof s);
	return FormatString("%s:%d", s, ntohs(sa.sin_port));
}

std::string Utils::ToString(const sockaddr_in6& sa) {
	if (sa.sin6_family != AF_INET6)
		return FormatString("sockaddr_in6?(AF_INET=%d)", sa.sin6_family);

	char s[INET6_ADDRSTRLEN + 6] = { 0 };
	inet_ntop(AF_INET6, &sa.sin6_addr, s, sizeof s);
	return FormatString("%s:%d", s, ntohs(sa.sin6_port));
}

std::string Utils::ToString(const sockaddr& sa) {
	if (sa.sa_family == AF_INET)
		return ToString(*reinterpret_cast<const sockaddr_in*>(&sa));
	if (sa.sa_family == AF_INET6)
		return ToString(*reinterpret_cast<const sockaddr_in6*>(&sa));
	return FormatString("sockaddr(AF_INET=%d)", sa.sa_family);
}

std::string Utils::ToString(const sockaddr_storage& sa) {
	return ToString(*reinterpret_cast<const sockaddr*>(&sa));
}

std::string Utils::FormatString(const char* format, ...) {
	std::string buf;
	va_list args;

	va_start(args, format);
	buf.resize(static_cast<size_t>(_vscprintf(format, args)) + 1);
	va_end(args);

	va_start(args, format);
	vsprintf_s(&buf[0], buf.size(), format, args);
	va_end(args);

	buf.resize(buf.size() - 1);

	return buf;
}

std::wstring Utils::FormatString(const wchar_t* format, ...) {
	std::wstring buf;
	va_list args;

	va_start(args, format);
	buf.resize(static_cast<size_t>(_vscwprintf(format, args)) + 1);
	va_end(args);

	va_start(args, format);
	vswprintf_s(&buf[0], buf.size(), format, args);
	va_end(args);

	buf.resize(buf.size() - 1);

	return buf;
}

std::vector<std::string> Utils::StringSplit(const std::string& str, const std::string& delimiter) {
	std::vector<std::string> result;
	if (delimiter.empty()) {
		for (size_t i = 0; i < str.size(); ++i)
			result.push_back(str.substr(i, 1));
	} else {
		size_t previousOffset = 0, offset;
		while ((offset = str.find(delimiter, previousOffset)) != std::string::npos) {
			result.push_back(str.substr(previousOffset, offset - previousOffset));
			previousOffset = offset + delimiter.length();
		}
		result.push_back(str.substr(previousOffset));
	}
	return result;
}

std::string Utils::StringTrim(const std::string& str, bool leftTrim, bool rightTrim) {
	size_t left = 0, right = str.length() - 1;
	if (leftTrim)
		while (left < str.length() && std::isspace(str[left]))
			left++;
	if (rightTrim)
		while (right != SIZE_MAX && std::isspace(str[right]))
			right--;
	return str.substr(left, right + 1 - left);
}