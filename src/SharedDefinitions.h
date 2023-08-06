#pragma once
#include <cstdint>

struct FileContent {
	size_t Size;
	void* pFile;
};

struct LastFileFolder {
	unsigned long aWriteTime[2]; // Ugly and not cross platform
	char aFilePath[256];
};

// Function signature for ImGui::SetAllocatorFunctions()
typedef void*   (*AllocFunc)(size_t Size, void* pUserData);               
typedef void    (*FreeFunc)(void* pLocation, void* pUserData); 

typedef FileContent    (*ReadFileFunc)(char* pPath);
typedef bool           (*WriteFileFunc)(FileContent* pFileContent, char* pPath);
typedef void		   (*FreeFileContentFunc)(FileContent* pFileContent);
typedef bool (*FetchLastFileFolderFunc)(char* pFolderPath, unsigned long* pLastWriteTimem, LastFileFolder* pOutLastFileFolder);


struct PlatformReloadContext {
	struct ImGuiContext* pImGuiCtx;
	AllocFunc pImGuiAllocFunc; 
	FreeFunc pImGuiFreeFunc; 
};

struct PlatformContext {
	uint64_t PermanentMemoryCapacity;
	void* pPermanentMemory;
	
	uint64_t ScratchMemoryCapacity;
	uint64_t ScratchSize;
	void* pScratchMemory;
	
	ReadFileFunc pReadFileFunc;
	WriteFileFunc pWriteFileFunc;
	FreeFileContentFunc pFreeFileContentFunc;
	FetchLastFileFolderFunc pFetchLastFileFolderFunc;

};