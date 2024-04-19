#pragma once
#include <stdio.h>
#include "SharedDefinitions.h"

typedef void PreInitFunc(size_t* OutPermanentMemorySize, size_t* OutScratchMemorySize);
typedef void InitFunc(PlatformContext*, PlatformReloadContext*);
typedef void PreUpdateFunc(PlatformContext*);
typedef void UpdateFunc(float, PlatformContext*);
typedef void OnHotReloadFunc(bool, PlatformReloadContext*);
typedef void OnDropFunc(PlatformContext*, char*);
typedef void ShutdownFunc(PlatformReloadContext*);

struct HotReloadableDll 
{
	HMODULE DLL;
	FILETIME LastWriteTime;
	PreInitFunc* pPreInitFunc;
	InitFunc* pInitFunc;
	PreUpdateFunc* pPreUpdateFunc;
	UpdateFunc* pUpdateFunc;
	OnHotReloadFunc* pOnHotReloadFunc;
	OnDropFunc* pOnDropFunc;
	ShutdownFunc* pShutdownFunc;

	bool bIsValid;
};

inline FILETIME GetLastWriteTime(char* FileName)
{
	FILETIME LastWriteTime = {};

	WIN32_FILE_ATTRIBUTE_DATA Data;
	if (GetFileAttributesEx(FileName, GetFileExInfoStandard, &Data))
	{
		LastWriteTime = Data.ftLastWriteTime;
	}
	
	return LastWriteTime;
}

inline void GetEXEFileName(char* p_EXEFileNameBuffer, DWORD NameBufferSize, char** p_PastLastSlash)
{
	DWORD SizeOfFilename = GetModuleFileNameA(0, p_EXEFileNameBuffer, NameBufferSize);
	
	*p_PastLastSlash = p_EXEFileNameBuffer;
	for (char *Scan = p_EXEFileNameBuffer; *Scan; Scan++)
	{
		
		if (*Scan == '\\')
		{
			*p_PastLastSlash = Scan + 1;
			*p_PastLastSlash = Scan + 1;
		}
	}
}

inline HotReloadableDll HotReloadDll(char* SourceDLLName, char* TempDLLName)
{
	HotReloadableDll Result = {};
	
	Result.LastWriteTime = GetLastWriteTime(SourceDLLName);
	CopyFileA(SourceDLLName, TempDLLName, FALSE);
	Result.DLL = LoadLibraryA(TempDLLName);
	if (Result.DLL)
	{
		Result.pPreInitFunc = (PreInitFunc *)GetProcAddress(Result.DLL, "AppPreInit");
		Result.pInitFunc = (InitFunc *)GetProcAddress(Result.DLL, "AppInit");
		Result.pPreUpdateFunc = (PreUpdateFunc *)GetProcAddress(Result.DLL, "AppPreUpdate");
		Result.pUpdateFunc = (UpdateFunc *)GetProcAddress(Result.DLL, "AppUpdate");
		Result.pOnHotReloadFunc = (OnHotReloadFunc *)GetProcAddress(Result.DLL, "AppOnHotReload");
		Result.pOnDropFunc = (OnDropFunc *)GetProcAddress(Result.DLL, "AppOnDrop");
		Result.pShutdownFunc = (ShutdownFunc *)GetProcAddress(Result.DLL, "AppShutdown");
		
		Result.bIsValid = Result.pPreInitFunc  
			&& Result.pInitFunc
			&& Result.pPreUpdateFunc 
			&& Result.pUpdateFunc
			&& Result.pOnHotReloadFunc
			&& Result.pOnDropFunc
			&& Result.pShutdownFunc;
	}

	if (!Result.bIsValid) 
	{
		Result.pPreUpdateFunc = nullptr;
		Result.pUpdateFunc = nullptr;
		Result.pInitFunc = nullptr;
		Result.pOnHotReloadFunc = nullptr;
		Result.pShutdownFunc = nullptr;
		Result.pOnDropFunc = nullptr;
	}

	return Result;
}

inline void UnloadHotReloadDLL(HotReloadableDll* p_ReloadableDll)
{
	if (p_ReloadableDll->DLL)
	{
		FreeLibrary(p_ReloadableDll->DLL);
		p_ReloadableDll->DLL = 0;
	}
	
	p_ReloadableDll->bIsValid = false;
	p_ReloadableDll->pUpdateFunc = nullptr;
	p_ReloadableDll->pInitFunc = nullptr;
	p_ReloadableDll->pOnHotReloadFunc = nullptr;
}

