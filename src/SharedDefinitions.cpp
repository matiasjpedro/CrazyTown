#include "SharedDefinitions.h"

void* ScratchMemory::Back()
{
	return (uint8_t*)pMemory + Size;
}

void ScratchMemory::Reset()
{
	Size = 0;
}
	
void* ScratchMemory::PushBack(size_t SrcSize, void* pSrc) 
{
	if (Size + SrcSize > Capacity)
		return nullptr;
		
	memcpy((uint8_t*)pMemory + Size, pSrc, SrcSize);
	uint64_t OldSize = Size;
	Size += SrcSize;
	
	return (uint8_t*)pMemory+OldSize;
}