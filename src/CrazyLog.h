#pragma once

struct NamedFilter {
	char aName[MAX_PATH];
	ImGuiTextFilter Filter;
};

struct HighlightLineMatchEntry
{
	int FilterIdxMatching;
	const char* pWordBegin;
	const char* pWordEnd;
	
	HighlightLineMatchEntry(int in_FilterIdx, const char* in_pWordBegin, const char* in_pWordEnd) :
							FilterIdxMatching(in_FilterIdx),
							pWordBegin(in_pWordBegin),
							pWordEnd(in_pWordEnd)
	{
	}
	
	static int SortFunc(const void * a, const void * b)
	{
		return ((const HighlightLineMatchEntry*)a)->pWordBegin 
			> ((const HighlightLineMatchEntry*)b)->pWordBegin;
	}
};
struct HighlightLineMatches
{
	ImVector<HighlightLineMatchEntry> vLineMatches;
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
	ImVector<HighlightLineMatches> vHighlightLineMatches;
	
	char aFilePathToLoad[MAX_PATH];
	char aFolderPathToLoad[MAX_PATH];
	char aFilterNameToSave[MAX_PATH];
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int LastFetchFileSize;
	int LastFrameFiltersCount;

	float FontScale;
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
	bool bWantsToScaleFont;
	bool bIsPeeking;
	
	// Output options
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	void BuildFonts();
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

	void PreDraw(PlatformContext* pPlatformCtx);
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
	                               HighlightLineMatches* pFiltredLineMatch);
	void CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, int FilterIdx,
	                                HighlightLineMatches* pFiltredLineMatch);
	
	
	
	bool CustomDrawFilter(const char* label, float width);
};