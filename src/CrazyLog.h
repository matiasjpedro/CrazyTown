#pragma once

struct NamedFilter {
	char aName[MAX_PATH];
	ImGuiTextFilter Filter;
};

struct CrazyLog
{
	ImGuiTextBuffer Buf;
	ImGuiTextFilter Filter;
	ImVector<int> vLineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
	ImVector<int> vFiltredLinesCached;
	ImVector<NamedFilter> LoadedFilters;
	
	char aFilePathToLoad[MAX_PATH];
	char aFolderPathToLoad[MAX_PATH];
	char aFilterNameToSave[MAX_PATH];
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int LastFetchFileSize;
	int LastFrameFiltersCount;
	
	float SelectionSize;
	float FilterRefreshCooldown;
	float FileContentFetchCooldown;
	float FolderFetchCooldown;
	float PeekScrollValue;
	float FiltredScrollValue;
	
	unsigned long aWriteTime[2];
	uint64_t FilterFlags;
	
	bool bAlreadyCached;
	bool bFileLoaded;
	bool bFolderQuery;
	bool bStreamMode;
	bool bWantsToSavePreset;
	bool bIsPeeking;
	
	// Output options
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	void Init();
	void Clear();
	
	void LoadClipboard();
	bool FetchFile(PlatformContext* pPlatformCtx);
	void LoadFile(PlatformContext* pPlatformCtx);
	void SearchLatestFile(PlatformContext* pPlatformCtx);
	
	void LoadFilter(PlatformContext* pPlatformCtx);
	void SaveLoadedFilters(PlatformContext* pPlatformCtx);
	void DeleteFilter(PlatformContext* pPlatformCtx);
	void SaveFilter(PlatformContext* pPlatformCtx, char* pFilterName, char* pFilterContent);
	
	void AddLog(const char* pFileContent, int FileSize);
	void SetLog(const char* pFileContent, int FileSize);
	
	void ClearCache();
		
	void Draw(float DeltaTime, PlatformContext* pPlatformCtx, const char* title, bool* pOpen = NULL);
		
	void FilterLines(PlatformContext* pPlatformCtx);
	void DrawFiltredView(PlatformContext* pPlatformCtx);
	void DrawFullView(PlatformContext* pPlatformCtx);
	void DrawTarget(float DeltaTime, PlatformContext* pPlatformCtx);
	void DrawFilter(float DeltaTime, PlatformContext* pPlatformCtx);
	bool DrawPresets(float DeltaTime, PlatformContext* pPlatformCtx);
	void DrawCherrypick(float DeltaTime, PlatformContext* pPlatformCtx);
		
	void SelectCharsFromLine(PlatformContext* pPlatformCtx, const char* pLineStart, const char* pLineEnd);
		
	//Filters;
	bool AnyFilterActive () const;
	bool CustomPassFilter(const char* text, const char* text_end) const;
};