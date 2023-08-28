#pragma once

struct NamedFilter {
	char aName[MAX_PATH];
	ImGuiTextFilter Filter;
};

struct HighlightLineMatch
{
	ImVector<int> vFilterIdxMatching;
	ImVector<const char*> vpWordBegin;
	ImVector<const char*> vpWordEnd;
};

struct CrazyLog
{
	ImGuiTextBuffer Buf;
	ImGuiTextFilter Filter;
	// TODO(matiasp): I should put this color inside the TextFilter
	ImVector<ImVec4> vFilterColor;
	ImVector<int> vLineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
	ImVector<int> vFiltredLinesCached;
	ImVector<NamedFilter> LoadedFilters;
	
	// TODO(matiasp): maybe I should combine this with the line offset and have all the line information in once place
	ImVector<HighlightLineMatch> vHighlightLineMatches;
	
	char aFilePathToLoad[MAX_PATH];
	char aFolderPathToLoad[MAX_PATH];
	char aFilterNameToSave[MAX_PATH];
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int LastFetchFileSize;
	int LastFrameFiltersCount;
	
	float SelectionSize;
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
	void FilterLines(PlatformContext* pPlatformCtx);
	
	void Draw(float DeltaTime, PlatformContext* pPlatformCtx, const char* title, bool* pOpen = NULL);
	void DrawFiltredView(PlatformContext* pPlatformCtx);
	void DrawFullView(PlatformContext* pPlatformCtx);
	void DrawTarget(float DeltaTime, PlatformContext* pPlatformCtx);
	void DrawFilter(float DeltaTime, PlatformContext* pPlatformCtx);
	bool DrawPresets(float DeltaTime, PlatformContext* pPlatformCtx);
	bool DrawCherrypick(float DeltaTime, PlatformContext* pPlatformCtx);
	
	char* GetWordStart(const char* pLineStart, char* pWordCursor);
	char* GetWordEnd(const char* pLineEnd, char* pWordCursor, int WordAmount);
	void SelectCharsFromLine(PlatformContext* pPlatformCtx, const char* pLineStart, const char* pLineEnd);
		
	//Filters;
	bool AnyFilterActive () const;
	bool CustomPassFilter(const char* text, const char* text_end) const;
	
	void CacheHighlightLineMatches(const char* pLineBegin, const char* pLineEnd,
	                               HighlightLineMatch* pFiltredLineMatch);
	void CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, int FilterIdx,
	                                HighlightLineMatch* pFiltredLineMatch);
	
	
	
	bool CustomDrawFilter(const char* label, float width);
};