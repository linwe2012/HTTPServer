#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <stdexcept>


constexpr inline bool is_digit(char c) {
	return c <= '9' && c >= '0';
}

constexpr inline int stoi_impl(const char* str, int value = 0) {
	return *str ?
		is_digit(*str) ?
		stoi_impl(str + 1, (*str - '0') + value * 10)
		: throw "compile-time-error: not a digit"
		: value;
}

constexpr int stoi(const char* str) {
	return stoi_impl(str);
}

constexpr inline uint32_t str2ip(const char* s) {
	uint32_t ip = 0;
	
	int shft = 3;
	char buf[10] = { 0 };
	int cur = 0;
	for(; *s; ++s){
		char c = *s;
		if (c == '.') {
			buf[cur] = '\0';
			cur = 0;
			uint32_t b = stoi(buf);
			ip |= b << (shft * 8);
			--shft;
		}
		else if (is_digit(c)) {
			buf[cur++] = c;
		}
		else {
			break;
		}
	}
	buf[cur] = '\0';
	cur = 0;
	uint32_t b = stoi(buf);
	ip |= b;
	return ip;
}

static_assert(str2ip("0.0.0.0") == 0, "");
static_assert(str2ip("127.0.0.1") == 0x7f000001, "");



class BufferStream {
public:
	BufferStream(char* base, uint32_t sz) {
		buffer = current = base;
		end = base + sz;
	}

	uint32_t Size() {
		return static_cast<uint32_t>(end - buffer);
	}

	char* buffer;
	char* current;
	char* end;

	template<typename T>
	bool Read(T* location) {
		if (sizeof(T) > end - current) {
			return false;
		}
		memmove(location, current, sizeof(T));
		current += sizeof(T);
		return true;
	}

	char* StringEndBy(char c) {
		char* begin = current;
		if (current == end) return nullptr;
		while (*current != c)
		{
			++current;
			if (current == end-1) return nullptr;
		}
		*current = '\0';
		++current;
		return begin;
	}

	char* StringEndByNoStrict(char c) {
		char* begin = current;
		if (current == end) return nullptr;
		while (*current != c)
		{
			++current;
			if (current == end - 1) {
				break;
			}
		}
		*current = '\0';
		++current;
		return begin;
	}

	char* StringEndBySpace() {
		return StringEndBy(' ');
	}

	bool IsCRLF() {
		if (current > end - 2) {
			return false;
		}
		if (*current == '\r' && *(current + 1) == '\n') {
			return true;
		}
		return false;
	}

	void SkipBytes(int n) {
		current += n;
	}

	char* StringEndByCRLF() {
		char* begin = current;
		if (current == end) return nullptr;

		while (true)
		{
			while (*current != '\r')
			{
				++current;
				if (current == end) return nullptr;
			}
			++current;
			if (*current == '\n') {
				*(current-1) = '\0';
				++current;
				break;
			}
		}
		
		return begin;
	}
	
};

template<typename T>
inline BufferStream& operator<<(T& arg, BufferStream& s) {
	if (s.Read(&arg) == false) {
		throw std::invalid_argument("unexpected end of stream");
	}
	return s;
}

template<>
inline BufferStream& operator<<(std::string& arg, BufferStream& s) {
	uint32_t size;
	size << s;
	if (size > s.end - s.current) {
		throw std::invalid_argument("unexpected end of stream");
	}
	arg += std::string(s.current, size);
	s.current += size;
	return s;
}