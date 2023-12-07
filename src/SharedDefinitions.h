#pragma once
#include <cstdint>

struct FileContent 
{
	size_t Size;
	void* pFile;
};

struct FileTimeData
{
	unsigned long aWriteTime[2];
	unsigned long aCreationTime[2];
};

struct FileData 
{
	FileTimeData FileTime;
	char aFilePath[MAX_PATH];
};

struct StratchMemory 
{
	uint64_t Capacity;
	uint64_t Size;
	void* pMemory;

	void Reset ();
	
	void* PushBack(size_t SrcSize, void* pSrc);
	void* Back ();
};

// Function signature for ImGui::SetAllocatorFunctions()
typedef void*   (*AllocFunc)(size_t Size, void* pUserData);               
typedef void    (*FreeFunc)(void* pLocation, void* pUserData); 

typedef FileContent    (*ReadFileFunc)(char* pPath);
typedef bool           (*WriteFileFunc)(FileContent* pFileContent, char* pPath);
typedef void		   (*FreeFileContentFunc)(FileContent* pFileContent);
typedef bool 		   (*FetchLastFileFolderFunc)(char* pFolderPath, FileData* pLastFetchFileData, FileData* pOutLastFileFolder);


struct PlatformReloadContext 
{
	struct ImGuiContext* pImGuiCtx;
	AllocFunc pImGuiAllocFunc; 
	FreeFunc pImGuiFreeFunc; 
};

struct PlatformContext 
{
	uint64_t PermanentMemoryCapacity;
	void* pPermanentMemory;
	
	StratchMemory ScratchMem;
	
	ReadFileFunc pReadFileFunc;
	WriteFileFunc pWriteFileFunc;
	FreeFileContentFunc pFreeFileContentFunc;
	FetchLastFileFolderFunc pFetchLastFileFolderFunc;
	
	bool bWantsToRebuildFontTexture;
};

// NOTE(Matiasp): Silly Simple hash algo with low collision
uint32_t HashString(const char* pStringBegin, const char* pStringEnd, uint32_t seed = 0)
{
	uint32_t hash = seed;
	while (pStringBegin != pStringEnd)
	{
		hash = hash * 101  +  *pStringBegin++;
	}
	
	return hash;
}