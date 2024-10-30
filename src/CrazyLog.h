#include "CrazyTextFilter.h"

#pragma once

enum RecentInputTextType {
	RITT_StreamPath = 0,
	RITT_FilePath,
	RITT_Find,
	RITT_Filter,
	RITT_COUNT
};

const static char* RememberInputTextSetting[RITT_COUNT] {
	"recent_stream_paths",
	"recent_file_paths",
	"recent_finds",
	"recent_filters"
};

const static char* RememberInputTextTailSetting[RITT_COUNT] {
	"stream_paths_tail",
	"file_paths_tail",
	"finds_tail",
	"filters_tail"
};

enum TargetMode 
{
	TM_StaticText = 0,
	TM_StreamLastModifiedFileFromFolder,
	TM_StreamFromWebSocket,
	TM_COUNT
};

enum TargetModeChangeReason
{
	TMCR_NONE = 0,
	TMCR_NewModeSelected,
	TMCR_DragAndDrop,
	TMCR_RecentSelected,
	TMCR_PasteFromClipboard,
	TMCR_COUNT
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

struct RecentInputText
{
	char aText[MAX_PATH];
};

struct CrazyLog
{
	ImGuiTextBuffer Buf;
	CrazyTextFilter Filter;
	ImVector<int> vLineOffsets; 
	ImVector<int> vFiltredLinesCached;
	ImVector<int> vFindFiltredLinesCached;
	ImVector<int> vFindFullViewLinesCached;
	ImVector<NamedFilter> LoadedFilters;
	ImVector<ImVec4> vDefaultColors;
	ImVector<RecentInputText> avRecentInputText[RITT_COUNT];
	int aRecentInputTextTail[RITT_COUNT];
	
	HighlightLineMatches TempLineMatches;
	
	char aNewVersion[MAX_PATH];
	char aCurrentVersion[MAX_PATH];
	char aFilePathToLoad[MAX_PATH * 2];
	char aFolderQueryName[MAX_PATH * 2];
	char aFilterNameToSave[MAX_PATH];
	char aLastCommand[MAX_PATH * 2];
	char aFindText[MAX_PATH * 2];
	int FindTextLen;
	int FilterToOverrideIdx;
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int FindFiltredProccesedLinesCount;
	int FindFullViewProccesedLinesCount;
	int LastFetchFileSize;
	int LastFrameFiltersCount;
	int SelectedExtraThreadCount;
	int MaxExtraThreadCount;
	int CurrentFindFiltredIdx;
	int CurrentFindFullViewIdx;
	TargetMode SelectedTargetMode;
	TargetModeChangeReason LastChangeReason;

	float OutputTextLineHeight;
	float FontScale;
	float SelectionSize;
	float FileContentFetchCooldown;
	float FileContentFetchSlider;
	float FolderFetchCooldown;
	float PeekScrollValue;
	float FiltredScrollValue;
	float FindScrollValue;

	FileData LastLoadedFileData;
	uint64_t EnableMask;
	
	bool bIsMultithreadEnabled;
	bool bIsAVXEnabled;
	bool bAlreadyCached;
	bool bFileLoaded;
	bool bFolderQuery;
	bool bStreamMode;
	bool bStreamFileLocked;
	bool bWantsToSavePreset;
	bool bWantsToOverridePreset;
	bool bWantsToScaleFont;
	bool bIsPeeking;
	bool bIsEditingColors;
	bool bIsFindOpen;
	
	// Output options
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	void BuildFonts();
	void GetVersions(PlatformContext* pPlatformCtx);
	void Init(PlatformContext* pPlatformCtx);
	void Clear();
	
	void LoadClipboard();
	bool FetchFile(PlatformContext* pPlatformCtx);
	bool LoadFile(PlatformContext* pPlatformCtx);
	void SearchLatestFile(PlatformContext* pPlatformCtx);
	void SaveFilteredView(PlatformContext* pPlatformCtx, char* pFilePath);
	
	void LoadFilters(PlatformContext* pPlatformCtx);
	void SaveLoadedFilters(PlatformContext* pPlatformCtx);
	void PasteFilter(PlatformContext* pPlatformCtx);
	void CopyFilter(PlatformContext* pPlatformCtx, CrazyTextFilter* pFilter);
	void DeleteFilter(PlatformContext* pPlatformCtx);
	void SaveFilter(PlatformContext* pPlatformCtx, char* pFilterName, CrazyTextFilter* pFilter);
	void SaveFilter(PlatformContext* pPlatformCtx, int FilterIdx, CrazyTextFilter* pFilter);
	
	void LoadSettings(PlatformContext* pPlatformCtx);
	void SaveDefaultColorsInSettings(PlatformContext* pPlatformCtx);
	void RememberInputText(PlatformContext* pPlatformCtx, RecentInputTextType Type, char* pText);
	void SaveTypeInSettings(PlatformContext* pPlatformCtx, const char* pKey, int Type, const void* pValue);
	
	void AddLog(const char* pFileContent, int FileSize);
	void SetLog(const char* pFileContent, int FileSize);
	
	void ClearCache();
	void ClearFindCache(bool bOnlyFilter);
	
	void FilterLines(PlatformContext* pPlatformCtx);
	void FindLines(PlatformContext* pPlatformCtx);

	void SetLastCommand(const char* pLastCommand);
	
	void PreDraw(PlatformContext* pPlatformCtx);
	void Draw(float DeltaTime, PlatformContext* pPlatformCtx, const char* title, bool* pOpen = NULL);
	void DrawFiltredView(PlatformContext* pPlatformCtx);
	void DrawFullView(PlatformContext* pPlatformCtx);
	void DrawTarget(float DeltaTime, PlatformContext* pPlatformCtx);
	void DrawFind(float DeltaTime, PlatformContext* pPlatformCtx);
	bool DrawFilters(float DeltaTime, PlatformContext* pPlatformCtx);
	bool DrawPresets(float DeltaTime, PlatformContext* pPlatformCtx);
	bool DrawCherrypick(float DeltaTime, PlatformContext* pPlatformCtx);
	void DrawMainBar(float DeltaTime, PlatformContext* pPlatformCtx);
	
	
	void HighlightLine(const char* pLineStart, const char* pLineEnd);
	
	char* GetWordStart(const char* pLineStart, char* pWordCursor);
	char* GetWordEnd(const char* pLineEnd, char* pWordCursor, int WordAmount);
	void SelectCharsFromLine(PlatformContext* pPlatformCtx, const char* pLineStart, const char* pLineEnd, float xOffset);
		
	//Filters;
	bool AnyFilterActive () const;
	
	void CacheHighlightLineMatches(const char* pLineBegin, const char* pLineEnd,
	                               HighlightLineMatches* pFiltredLineMatch);
	void CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, 
									const char* pWordBegin, const char* pWordEnd, 
									int FilterIdx, HighlightLineMatches* pFiltredLineMatch);

};