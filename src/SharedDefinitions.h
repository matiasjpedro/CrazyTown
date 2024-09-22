#pragma once
#include <cstdint>

#define ASSERT(Expression) if(!(Expression)) {*(int *)0 = 0;}

#define Kilobytes(X) ((X)*1024LL)
#define Megabytes(X) (Kilobytes(X)*1024LL)
#define Gigabytes(X) (Megabytes(X)*1024LL)
#define Terabytes(X) (Gigabytes(X)*1024LL)

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#define Pi32 3.14159265359f

enum ArgType : unsigned
{
	AT_FlagTest,
	AT_AlwaysOnTop,
	AT_WinPosX,
	AT_WinPosY,
	AT_WinSizeX,
	AT_WinSizeY,
	AT_COUNT
};
	
static const char* aCmdArgs[AT_COUNT]
{
	"flag_test",
	"always_on_top",
	"win_pos_x",
	"win_pos_y",
	"win_size_x",
	"win_size_y"
};

struct CommandArguments
{
	bool bFlagTest;
	bool bAlwaysOnTop;
	int WinPosX;
	int WinPosY;
	int WinSizeX;
	int WinSizeY;
};
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

struct ScratchMemory 
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
typedef bool           (*StreamFileFunc)(FileContent* pFileContent, void* pHandle, bool bShouldCloseHandle);
typedef void           (*GetExePathFunc)(char* pExePathBuffer, size_t BufferSize, size_t& OutPathSize, bool bIncludeFilename);
typedef void*          (*GetFileHandleFunc)(char* pPath, unsigned CreationDisposition);
typedef void		   (*FreeFileContentFunc)(FileContent* pFileContent);
typedef bool 		   (*FetchLastFileFolderFunc)(char* pFolderPath, FileData* pLastFetchFileData, FileData* pOutLastFileFolder);
typedef void 		   (*OpenURLFunc)(const char* pURL);
typedef LARGE_INTEGER  (*GetWallClockFunc)();
typedef float		   (*GetSecondsElapsedFunc)(LARGE_INTEGER Start, LARGE_INTEGER End);


struct PlatformReloadContext 
{
	struct ImGuiContext* pImGuiCtx;
	AllocFunc pImGuiAllocFunc; 
	FreeFunc pImGuiFreeFunc; 
};

struct PlatformContext 
{
	CommandArguments CmdArgs;
	uint64_t PermanentMemoryCapacity;
	void* pPermanentMemory;
	
	ScratchMemory ScratchMem;
	
	ReadFileFunc pReadFileFunc;
	WriteFileFunc pWriteFileFunc;
	StreamFileFunc pStreamFileFunc;
	GetFileHandleFunc pGetFileHandleFunc;
	GetExePathFunc pGetExePathFunc;
	FreeFileContentFunc pFreeFileContentFunc;
	FetchLastFileFolderFunc pFetchLastFileFolderFunc;
	OpenURLFunc pOpenURLFunc;
	GetWallClockFunc pGetWallClockFunc;
	GetSecondsElapsedFunc pGetSecondsElapsedFunc;
	
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