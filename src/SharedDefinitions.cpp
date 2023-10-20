#include "SharedDefinitions.h"

void* StratchMemory::Back()
{
	return (uint8_t*)pMemory + Size;
}

void StratchMemory::Reset()
{
	Size = 0;
}
	
bool StratchMemory::PushBack(const void* pSrc, size_t SrcSize) 
{
	if (Size + SrcSize > Capacity)
		return false;
		
	memcpy((uint8_t*)pMemory + Size, pSrc, SrcSize);
	Size += SrcSize;
	
	return true;
}