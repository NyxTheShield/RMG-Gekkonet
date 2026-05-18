#ifndef WriteToRDRAM_H
#define WriteToRDRAM_H


#include <stdio.h>

#include "../Types.h"

static inline bool rollbackWatchRdramAddress(u32 _address, u32 _size)
{
	return _address + _size > 0x00394300u && _address < 0x00394380u;
}

template <typename TDst>
static inline void rollbackLogWriteToRdram(const char* _source, u32 _address, TDst _oldValue, TDst _newValue)
{
	FILE* file;

	if (!rollbackWatchRdramAddress(_address, sizeof(TDst)))
		return;

	file = fopen("rollback_rdram_watch.log", "a");
	if (file == nullptr)
		file = fopen("Bin/Release/rollback_rdram_watch.log", "a");
	if (file == nullptr)
		return;

	fprintf(file, "source=%s address=0x%08x size=%u old=0x%08x new=0x%08x\n",
		_source,
		_address,
		static_cast<unsigned>(sizeof(TDst)),
		static_cast<unsigned>(_oldValue),
		static_cast<unsigned>(_newValue));
	fclose(file);
}

template <typename T, T testValue>
bool valueTester(T _c)
{
	return _c != testValue;
}

template <typename T>
bool dummyTester(T _c)
{
	return true;
}

template <typename TSrc, typename TDst>
void writeToRdram(TSrc* _src, TDst* _dst,
	TDst(*converter)(TSrc _c, u32 x, u32 y),
	bool(*tester)(TSrc _c),
	u32 _xor,
	u32 _width,
	u32 _height,
	u32 _numPixels,
	u32 _startAddress,
	u32 _bufferAddress,
	u32 _bufferSize,
	const char* _logSource = "glide_writeToRdram")
{
	u32 chunkStart = ((_startAddress - _bufferAddress) >> (_bufferSize - 1)) % _width;
	if (chunkStart % 2 != 0) {
		--chunkStart;
		--_dst;
		++_numPixels;
	}

	u32 numStored = 0;
	u32 y = 0;
	TSrc c;
	if (chunkStart > 0) {
		for (u32 x = chunkStart; x < _width; ++x) {
			c = _src[x];
			if (tester(c)) {
				const u32 dstIndex = numStored ^ _xor;
				const u32 dstAddress = _bufferAddress + (dstIndex << (_bufferSize - 1));
				const TDst oldValue = _dst[dstIndex];
				const TDst newValue = converter(c, x, y);
				_dst[dstIndex] = newValue;
				rollbackLogWriteToRdram(_logSource, dstAddress, oldValue, newValue);
			}
			++numStored;
		}
		++y;
		_dst += numStored;
	}

	u32 dsty = 0;
	for (; y < _height; ++y) {
		for (u32 x = 0; x < _width && numStored < _numPixels; ++x) {
			c = _src[x + y *_width];
			if (tester(c)) {
				const u32 dstIndex = (x + dsty*_width) ^ _xor;
				const u32 dstAddress = _bufferAddress + (((_startAddress - _bufferAddress) >> (_bufferSize - 1)) + dstIndex) * sizeof(TDst);
				const TDst oldValue = _dst[dstIndex];
				const TDst newValue = converter(c, x, y);
				_dst[dstIndex] = newValue;
				rollbackLogWriteToRdram(_logSource, dstAddress, oldValue, newValue);
			}
			++numStored;
		}
		++dsty;
	}
}

#endif // WriteToRDRAM_H
