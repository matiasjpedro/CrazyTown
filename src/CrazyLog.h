#include "CrazyTextFilter.h"

#pragma once

enum TargetMode 
{
	TM_StaticText = 0,
	TM_StreamLastModifiedFileFromFolder,
	TM_StreamFromWebSocket,
	TM_COUNT
};

static char* apTargetModeStr[TM_COUNT] =
{
	"StaticText",
	"StreamLastModifiedFileFromFolder",
	"StreamFromWebSocket - TODO"
};

struct NamedFilter 
{
	char aName[MAX_PATH];
	CrazyTextFilter Filter;
};

struct HighlightLineMatchEntry
{
	const uint16_t WordBeginOffset;
	const uint16_t WordEndOffset;
	
	uint8_t FilterIdxMatching;
	
	HighlightLineMatchEntry(uint8_t in_FilterIdx, const uint16_t in_WordBeginOffset, const uint16_t in_WordEndOffset) :
							FilterIdxMatching(in_FilterIdx),
							WordBeginOffset(in_WordBeginOffset),
							WordEndOffset(in_WordEndOffset)
	{
	}
	
	static int SortFunc(const void * a, const void * b)
	{
		return ((const HighlightLineMatchEntry*)a)->WordBeginOffset 
			> ((const HighlightLineMatchEntry*)b)->WordBeginOffset;
	}
};

struct HighlightLineMatches
{
	ImVector<HighlightLineMatchEntry> vLineMatches;
};

struct CrazyLog
{
	ImGuiTextBuffer Buf;
	CrazyTextFilter Filter;
	ImVector<int> vLineOffsets; 
	ImVector<int> vFiltredLinesCached;
	ImVector<NamedFilter> LoadedFilters;
	ImVector<ImVec4> vDefaultColors;
	
	HighlightLineMatches TempLineMatches;
	
	char aFilePathToLoad[MAX_PATH];
	char aFolderQueryName[MAX_PATH];
	char aFilterNameToSave[MAX_PATH];
	char aLastCommand[MAX_PATH];
	int FilterToOverrideIdx;
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int LastFetchFileSize;
	int LastFrameFiltersCount;
	int SelectedThreadCount;
	int MaxThreadCount;
	TargetMode SelectedTargetMode;

	float FontScale;
	float SelectionSize;
	float FileContentFetchCooldown;
	float FileContentFetchSlider;
	float FolderFetchCooldown;
	float PeekScrollValue;
	float FiltredScrollValue;

	FileData LastLoadedFileData;
	uint64_t FilterFlags;
	
	bool bIsMultithreadEnabled;
	bool bAlreadyCached;
	bool bFileLoaded;
	bool bFolderQuery;
	bool bStreamMode;
	bool bWantsToSavePreset;
	bool bWantsToOverridePreset;
	bool bWantsToScaleFont;
	bool bIsPeeking;
	bool bIsEditingColors;
	
	// Output options
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	void BuildFonts();
	void Init();
	void Clear();
	
	void LoadClipboard();
	bool FetchFile(PlatformContext* pPlatformCtx);
	bool LoadFile(PlatformContext* pPlatformCtx);
	void SearchLatestFile(PlatformContext* pPlatformCtx);
	
	void LoadFilters(PlatformContext* pPlatformCtx);
	void SaveLoadedFilters(PlatformContext* pPlatformCtx);
	void PasteFilter(PlatformContext* pPlatformCtx);
	void CopyFilter(PlatformContext* pPlatformCtx, CrazyTextFilter* pFilter);
	void DeleteFilter(PlatformContext* pPlatformCtx);
	void SaveFilter(PlatformContext* pPlatformCtx, char* pFilterName, CrazyTextFilter* pFilter);
	void SaveFilter(PlatformContext* pPlatformCtx, int FilterIdx, CrazyTextFilter* pFilter);
	
	void LoadSettings(PlatformContext* pPlatformCtx);
	void SaveDefaultColorsInSettings(PlatformContext* pPlatformCtx);
	void SaveTypeInSettings(PlatformContext* pPlatformCtx, const char* pKey, int Type, const void* pValue);
	
	void AddLog(const char* pFileContent, int FileSize);
	void SetLog(const char* pFileContent, int FileSize);
	
	void ClearCache();
	void FilterLines(PlatformContext* pPlatformCtx);

	void SetLastCommand(const char* pLastCommand);
	
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
	
	void CacheHighlightLineMatches(const char* pLineBegin, const char* pLineEnd,
	                               HighlightLineMatches* pFiltredLineMatch);
	void CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, int FilterIdx,
	                                HighlightLineMatches* pFiltredLineMatch);

};