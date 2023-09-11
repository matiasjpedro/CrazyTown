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

struct LastFileFolder 
{
	FileTimeData FileTime;
	char aFilePath[256];
};

struct StratchMemory 
{
	uint64_t Capacity;
	uint64_t Size;
	void* pMemory;

	void Reset ();
	bool PushBack (const void* pSrc, size_t SrcSize);
	void* Back ();
};

// Function signature for ImGui::SetAllocatorFunctions()
typedef void*   (*AllocFunc)(size_t Size, void* pUserData);               
typedef void    (*FreeFunc)(void* pLocation, void* pUserData); 

typedef FileContent    (*ReadFileFunc)(char* pPath);
typedef bool           (*WriteFileFunc)(FileContent* pFileContent, char* pPath);
typedef void		   (*FreeFileContentFunc)(FileContent* pFileContent);
typedef bool 		   (*FetchLastFileFolderFunc)(char* pFolderPath, FileTimeData* pLastFetchFileData, LastFileFolder* pOutLastFileFolder);


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
