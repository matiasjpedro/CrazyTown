#include <thread>

#include "CrazyLog.h"
#include "StringUtils.h"
#include "CrazyTextFilter.h"

#include "ConsolaTTF.cpp"

#define RING_BUFFER_BACKWARDS(idx, vBuffer) (idx - 1 != -1 ? idx - 1 : vBuffer.Size - 1)
#define BINARIES_URL "https://github.com/matiasjpedro/CrazyTown/releases"
#define GIT_URL "https://github.com/matiasjpedro/CrazyTown"
#define ISSUES_URL "https://github.com/matiasjpedro/CrazyTown/issues"
#define FILTERS_FILE_NAME "FILTERS.json"
#define SETTINGS_NAME "SETTINGS.json"
#define FILE_FETCH_INTERVAL 0.5f
#define FOLDER_FETCH_INTERVAL 2.f
#define CONSOLAS_FONT_SIZE 14 
#define MAX_EXTRA_THREADS 31
#define MAX_REMEMBER_PATHS 5

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define clamp(v, mx, mn) (v < mn) ? mn : (v > mx) ? mx : v; 

#define SAVE_ENABLE_MASK 0

static float g_Version = 1.14f;

static char g_NullTerminator = '\0';

void CrazyLog::Init()
{
	SelectedExtraThreadCount = 0;
	FontScale = 1.f;
	SelectionSize = 1.f;
	FileContentFetchCooldown = -1.f;
	FolderFetchCooldown = -1.f;
	PeekScrollValue = -1.f;
	FiltredScrollValue = -1.f;
	EnableMask = 0xFFFFFFFF;
	SetLastCommand("LAST COMMAND");
	ImGui::StyleColorsClassic();
	
    ImGuiStyle* style = &ImGui::GetStyle();
	style->Colors[ImGuiCol_MenuBarBg] = style->Colors[ImGuiCol_FrameBg];
	
	FileContentFetchSlider = FILE_FETCH_INTERVAL;
	MaxExtraThreadCount = max(0, std::thread::hardware_concurrency() - 1);
	
	// Lets enable it by default
	bIsMultithreadEnabled = true;
	SelectedExtraThreadCount = clamp(3, MaxExtraThreadCount, 0);
	
	FilePathsTail = -1;
	StreamPathsTail = -1;
	vRecentFilePaths.resize(MAX_REMEMBER_PATHS, { 0 });
	vRecentStreamPaths.resize(MAX_REMEMBER_PATHS, { 0 });
	
	bIsAVXEnabled = true;
}

void CrazyLog::Clear()
{
	bIsPeeking = false;
	bFileLoaded = false;
	bWantsToSavePreset = false;
	
	Buf.clear();
	vLineOffsets.clear();
	vLineOffsets.push_back(0);

	ClearCache();
	
	SetLastCommand("LOG CLEARED");
}

void CrazyLog::BuildFonts()
{
	ImGuiIO& IO = ImGui::GetIO();
	IO.Fonts->Clear();
	
	IO.Fonts->AddFontDefault();

	// Add the consolas font
	{
		ImFontConfig font_cfg = ImFontConfig();
		font_cfg.OversampleH = font_cfg.OversampleV = 1;
		font_cfg.PixelSnapH = true;
	
		float FontSize = max(1, CONSOLAS_FONT_SIZE + FontScale);
	
		if (font_cfg.SizePixels <= 0.0f)
			font_cfg.SizePixels = FontSize * 1.0f;
		if (font_cfg.Name[0] == '\0')
			ImFormatString(font_cfg.Name, IM_ARRAYSIZE(font_cfg.Name), "Consola.ttf, %dpx", (int)font_cfg.SizePixels);
		font_cfg.EllipsisChar = (ImWchar)0x0085;
		font_cfg.GlyphOffset.y = 1.0f * IM_FLOOR(font_cfg.SizePixels / FontSize);  

		const char* ttf_compressed_base85 = ConsolaTTF_compressed_data_base85;
		const ImWchar* glyph_ranges = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
		IO.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_cfg.SizePixels, &font_cfg, glyph_ranges);
	}
	
	IO.Fonts->Build();
}

void CrazyLog::LoadClipboard() 
{
	bIsPeeking = false;
	bFileLoaded = false;
	bStreamMode = false;

	bFolderQuery = false;
	memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
	
	const char* pClipboardText = ImGui::GetClipboardText();
	if (pClipboardText) {
		size_t TextSize = StringUtils::Length(pClipboardText);
		SetLog(pClipboardText, (int)TextSize);
	}
	
	SelectedTargetMode = TM_StaticText;
	LastChangeReason = TMCR_PasteFromClipboard;
}

bool CrazyLog::FetchFile(PlatformContext* pPlatformCtx) 
{
	if (aFilePathToLoad[0] == 0)
		return false;
	
	// IMP(matiasp): 
	// Avoid virtual alloc, use scratch alloc.
	// keep the file open to avoid read/alloc the entire content.
	FileContent File = pPlatformCtx->pReadFileFunc(aFilePathToLoad);
	bool bNewContent = false;
	if(File.pFile)
	{
		bNewContent = ((int)File.Size - LastFetchFileSize) > 0;
		if (bNewContent) {
			AddLog((const char*)File.pFile + LastFetchFileSize, (int)File.Size - LastFetchFileSize);
			LastFetchFileSize = (int)File.Size;
		}
		
		pPlatformCtx->pFreeFileContentFunc(&File);
	}
	
	
	return bNewContent;
}

void CrazyLog::SearchLatestFile(PlatformContext* pPlatformCtx)
{
	if (aFolderQueryName[0] == 0)
		return;
	
	FileData OutLastFileData = { 0 };
	bool bNewerFile = pPlatformCtx->pFetchLastFileFolderFunc(aFolderQueryName, &LastLoadedFileData, &OutLastFileData);
		
	// There is a newer file
	if (bNewerFile) 
	{
		strcpy_s(aFilePathToLoad, sizeof(aFilePathToLoad), OutLastFileData.aFilePath);
		memcpy(&LastLoadedFileData, &OutLastFileData, sizeof(FileData));
			
		bStreamMode = LoadFile(pPlatformCtx);
		
		if (bStreamMode)
			FileContentFetchCooldown = FILE_FETCH_INTERVAL;
	}
	
	FolderFetchCooldown = FOLDER_FETCH_INTERVAL;
}

void CrazyLog::SaveFilteredView(PlatformContext* pPlatformCtx, char* pFilePath)
{
	if (vFiltredLinesCached.Size == 0)
		return;
	
	FileContent TempFileContent;
	void* pFileHandle = pPlatformCtx->pGetFileHandleFunc(pFilePath, 2 /* CREATE_ALWAYS */);
	
	const char* pBuf = Buf.begin();
	const char* pBufEnd = Buf.end();
	
	for (size_t i = 0; i < vFiltredLinesCached.Size; i++)
	{
		int LineNo = vFiltredLinesCached[(int)i];
		const char* pLineStart = pBuf + vLineOffsets[LineNo];
		const char* pLineEnd = (LineNo + 1 < vLineOffsets.Size) ? (pBuf + vLineOffsets[LineNo + 1] - 1) : pBufEnd;
		
		TempFileContent.pFile = (void*)pLineStart;
		TempFileContent.Size = (pLineEnd + 1) - pLineStart;
		
		bool bShouldClose = i == (vFiltredLinesCached.Size - 1);
		pPlatformCtx->pStreamFileFunc(&TempFileContent, pFileHandle, bShouldClose);
	}
}

bool CrazyLog::LoadFile(PlatformContext* pPlatformCtx) 
{
	if (aFilePathToLoad[0] == 0)
		return false;
		
	bIsPeeking = false;
	
	FileContent File = pPlatformCtx->pReadFileFunc(aFilePathToLoad);
	if(File.pFile)
	{
		bFileLoaded = true;
		
		SetLog((const char*)File.pFile, (int)File.Size);
		pPlatformCtx->pFreeFileContentFunc(&File);
		
		LastFetchFileSize = (int)File.Size;
	}
	
	SetLastCommand("FILE LOADED");
	
	return File.Size > 0;
}

void CrazyLog::LoadFilters(PlatformContext* pPlatformCtx)
{	
	const char* NoneFilterName = "NONE";
	NamedFilter NoneFilter = { 0 };
	strcpy_s(NoneFilter.aName, sizeof(NoneFilter.aName), NoneFilterName);
	
	FileContent File = pPlatformCtx->pReadFileFunc(FILTERS_FILE_NAME);
	if (File.pFile) 
	{
		cJSON * pJsonRoot = cJSON_ParseWithLength((char*)File.pFile, File.Size);
		cJSON * pFiltersArray = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "Filters");
		cJSON * pFilter = nullptr;
		cJSON * pColor = nullptr;
		cJSON * pEnabled = nullptr;
		
		unsigned FiltersCounter = 1;
		int FiltersCount = cJSON_GetArraySize(pFiltersArray);
		LoadedFilters.resize(FiltersCount + 1, { 0 });
		
		//Add dummy so we can reset the selection
		LoadedFilters[0] = NoneFilter;
		
		cJSON_ArrayForEach(pFilter, pFiltersArray)
		{
			cJSON * pFilterName = cJSON_GetObjectItemCaseSensitive(pFilter, "key");
			cJSON * pFilterValue = cJSON_GetObjectItemCaseSensitive(pFilter, "value");
			
			NamedFilter* pNamedFilter = &LoadedFilters[FiltersCounter];
			strcpy_s(pNamedFilter->aName, sizeof(pNamedFilter->aName), pFilterName->valuestring);
			strcpy_s(pNamedFilter->Filter.aInputBuf, sizeof(pNamedFilter->Filter.aInputBuf),  pFilterValue->valuestring);
			
			cJSON * pColorArray = cJSON_GetObjectItemCaseSensitive(pFilter, "colors");
			cJSON * pEnableMaskArray = cJSON_GetObjectItemCaseSensitive(pFilter, "enable_mask");
			
			int ColorsCount = cJSON_GetArraySize(pColorArray);
			int EnablesCount = cJSON_GetArraySize(pColorArray);
			pNamedFilter->Filter.vSettings.resize(max(ColorsCount, EnablesCount));
			
			unsigned ColorCounter = 0;
			cJSON_ArrayForEach(pColor, pColorArray)
			{
				ImVec4 Color;
				StringUtils::HexToRGB(pColor->valuestring, &Color.x);
				Color.x /= 255;
				Color.y /= 255;
				Color.z /= 255;
				Color.w /= 255;
				
				pNamedFilter->Filter.vSettings[ColorCounter].Color = Color;
				ColorCounter++;
			}
			
#if SAVE_ENABLE_MASK
			unsigned EnablesCounter = 0;
			cJSON_ArrayForEach(pEnabled, pEnableMaskArray)
			{
				pNamedFilter->Filter.vSettings[EnablesCounter].bIsEnabled = cJSON_IsTrue(pEnabled);
				EnablesCounter++;
			}
#endif
			
			pNamedFilter->Filter.Build(&vDefaultColors, false);
			
			FiltersCounter++;
		}
		
		free(pJsonRoot);
		pPlatformCtx->pFreeFileContentFunc(&File);
	}
	else
	{
		LoadedFilters.reserve_discard(1);
		LoadedFilters.resize(0);
		
		LoadedFilters.push_back(NoneFilter);
	}
}

void CrazyLog::SaveLoadedFilters(PlatformContext* pPlatformCtx) 
{
	FileContent OutFile = {0};
	
	cJSON * pJsonRoot = cJSON_CreateObject();
	cJSON * pJsonFilterArray = cJSON_CreateArray();
	cJSON * pFilterValue = nullptr;
	cJSON * pFilterName = nullptr;
	cJSON * pFilterColor = nullptr;
	cJSON * pFilterEnabled = nullptr;
	
	cJSON * pFilterObj = nullptr;
	cJSON_AddItemToObject(pJsonRoot, "Filters", pJsonFilterArray);
	
	// Start from 1 to skip the NONE filter
	for (unsigned i = 1; i < (unsigned)LoadedFilters.size(); ++i)
	{
		pFilterObj = cJSON_CreateObject();
		
		pFilterName = cJSON_CreateString(LoadedFilters[i].aName);
		cJSON_AddItemToObject(pFilterObj, "key", pFilterName);
		pFilterValue = cJSON_CreateString(LoadedFilters[i].Filter.aInputBuf);
		cJSON_AddItemToObject(pFilterObj, "value", pFilterValue);
		
		cJSON * pJsonColorArray = cJSON_AddArrayToObject(pFilterObj, "colors");
		
		for (unsigned j = 0; j < (unsigned)LoadedFilters[i].Filter.vSettings.Size; ++j)
		{
			ImVec4& Color = LoadedFilters[i].Filter.vSettings[j].Color;
			
			char aColorBuf[9];
			sprintf(aColorBuf, "#%02X%02X%02X%02X", 
			        (int)(Color.x*255.f), 
			        (int)(Color.y*255.f), 
			        (int)(Color.z*255.f),
			        (int)(Color.w*255.f));
			
			pFilterColor = cJSON_CreateString(aColorBuf);
			cJSON_AddItemToObject(pJsonColorArray, "color", pFilterColor);
		}
		
#if SAVE_ENABLE_MASK
		cJSON * pJsonEnabledMaskArray = cJSON_AddArrayToObject(pFilterObj, "enable_mask");
		for (unsigned j = 0; j < (unsigned)LoadedFilters[i].Filter.vSettings.Size; ++j)
		{
			pFilterEnabled = cJSON_CreateBool(LoadedFilters[i].Filter.vSettings[j].bIsEnabled);
			cJSON_AddItemToObject(pJsonEnabledMaskArray, "enabled", pFilterEnabled);
		}
#endif
		
		cJSON_AddItemToObject(pJsonFilterArray, "filter", pFilterObj);
	}
	
	OutFile.pFile = cJSON_Print(pJsonRoot);
	OutFile.Size = strlen((char*)OutFile.pFile);
	
	pPlatformCtx->pWriteFileFunc(&OutFile, FILTERS_FILE_NAME);
	free(pJsonRoot);
	
}

void CrazyLog::LoadSettings(PlatformContext* pPlatformCtx) {
	
	FileContent File = pPlatformCtx->pReadFileFunc(SETTINGS_NAME);
	if (File.pFile)
	{
		cJSON * pJsonRoot = cJSON_ParseWithLength((char*)File.pFile, File.Size);
		
		cJSON * pIsMtEnabled = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "is_multithread_enabled");
		if (pIsMtEnabled)
			bIsMultithreadEnabled = cJSON_IsTrue(pIsMtEnabled);
		
		cJSON * pIsAVXEnabled = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "is_avx_enabled");
		if (pIsAVXEnabled)
			bIsAVXEnabled = cJSON_IsTrue(pIsAVXEnabled);
		
		cJSON * pSelectedThreadCount = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "selected_thread_count");
		if (pSelectedThreadCount)
			SelectedExtraThreadCount = min(MaxExtraThreadCount, (int)pSelectedThreadCount->valuedouble);
		
		cJSON * pColorArray = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "default_colors");
		
		// Load by default some colors if non are stored 
		if (pColorArray == nullptr) 
		{
			for (unsigned j = 0; j < ArrayCount(aDefaultColors) ; ++j) 
			{
				vDefaultColors.resize(ArrayCount(aDefaultColors));
				vDefaultColors[j] = aDefaultColors[j];
			}
		}
		else
		{
			unsigned ColorsCounter = 0;
			int ColorsCount = cJSON_GetArraySize(pColorArray);
			vDefaultColors.resize(ColorsCount);
			
			cJSON * pColor = nullptr;
			cJSON_ArrayForEach(pColor, pColorArray)
			{
				ImVec4 Color;
				StringUtils::HexToRGB(pColor->valuestring, &Color.x);
				Color.x /= 255;
				Color.y /= 255;
				Color.z /= 255;
				Color.w /= 255;
				vDefaultColors[ColorsCounter] = Color;
				
				ColorsCounter++;
			}
		}
		
		cJSON * pRecentStreamPathsArray = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "recent_stream_paths");
		int StreamsPathsSize = cJSON_GetArraySize(pRecentStreamPathsArray);
		cJSON * pRecentStreamPath = nullptr;
		if (pRecentStreamPathsArray) {
			vRecentStreamPaths.resize(StreamsPathsSize);
		
			int PathsCounter = 0;
			cJSON_ArrayForEach(pRecentStreamPath, pRecentStreamPathsArray)
			{
				strcpy_s(vRecentStreamPaths[PathsCounter++].aFilePath, sizeof(FilePath::aFilePath),
				         pRecentStreamPath->valuestring);
			}
		}
		
		
		cJSON * pRecentFilePathsArray = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "recent_file_paths");
		int FilePathsSize = cJSON_GetArraySize(pRecentFilePathsArray);
		cJSON * pRecentFilePath = nullptr;
		if (pRecentFilePathsArray) {
			vRecentFilePaths.resize(FilePathsSize);
		
			int PathsCounter = 0;
			cJSON_ArrayForEach(pRecentFilePath, pRecentFilePathsArray)
			{
				strcpy_s(vRecentFilePaths[PathsCounter++].aFilePath, sizeof(FilePath::aFilePath),
				         pRecentFilePath->valuestring);
			}
		}
		
		cJSON * pStreamPathsTail = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "stream_paths_tail");
		if (pStreamPathsTail)
			StreamPathsTail = (int)pStreamPathsTail->valuedouble;
		else if (StreamsPathsSize > 0)
			StreamPathsTail = StreamsPathsSize -1;
		
		cJSON * pFilePathsTail = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "file_paths_tail");
		if (pFilePathsTail)
			FilePathsTail = (int)pFilePathsTail->valuedouble;
		else if (FilePathsSize > 0)
			FilePathsTail = FilePathsSize -1;
		
		if(StreamPathsTail != -1)
			strcpy_s(aFolderQueryName, sizeof(aFolderQueryName), vRecentStreamPaths[StreamPathsTail].aFilePath);
		
		if(FilePathsTail != -1)
			strcpy_s(aFilePathToLoad, sizeof(aFilePathToLoad), vRecentFilePaths[FilePathsTail].aFilePath);
		
		
		free(pJsonRoot);
		pPlatformCtx->pFreeFileContentFunc(&File);
	}
	else
	{
		// Load by default some colors if non are stored
		for (unsigned j = 0; j < ArrayCount(aDefaultColors) ; ++j) 
		{
			vDefaultColors.resize(ArrayCount(aDefaultColors));
			vDefaultColors[j] = aDefaultColors[j];
		}
	}
}

void CrazyLog::RememberFilePath(PlatformContext* pPlatformCtx, bool bIsStreamPath, char* pFilePath) {
	
	const char* pTailSettingName = bIsStreamPath ? "stream_paths_tail" : "file_paths_tail";
	const char* pBufferSettingName = bIsStreamPath ? "recent_stream_paths" : "recent_file_paths";
	
	ImVector<FilePath>& vTargetFilePath = bIsStreamPath ? vRecentStreamPaths : vRecentFilePaths;
	int& PathsTail = bIsStreamPath ? StreamPathsTail : FilePathsTail;
	
	int AlreadyExistIdx = -1;
	// Check if this path already exist in our buffer
	for (unsigned i = 0; i < (unsigned)vTargetFilePath.Size; i++)
	{
		if (vTargetFilePath[i].aFilePath[0] == '\0')
			continue;
		
		if (strncmp(vTargetFilePath[i].aFilePath, pFilePath, sizeof(FilePath::aFilePath)) == 0)
		{
			AlreadyExistIdx = i;
			break;
		}
	}
	
	// If it's already the latest one, then don't do nothing. 
	if (AlreadyExistIdx != -1 && AlreadyExistIdx == PathsTail)
		return;

	PathsTail = ++PathsTail % MAX_REMEMBER_PATHS;
	
	// If it already exist then offset the buffer by 1 idx starting from the located idx
	// by doing that we will be removing the older entry and bump the other entries 1 position.
	if (AlreadyExistIdx != -1)
	{
		for (int i = AlreadyExistIdx; i != PathsTail; i = RING_BUFFER_BACKWARDS(i, vTargetFilePath))
		{
			int PrevIdx = RING_BUFFER_BACKWARDS(i, vTargetFilePath);
			strcpy_s(vTargetFilePath[i].aFilePath, sizeof(FilePath::aFilePath), vTargetFilePath[PrevIdx].aFilePath);
		}
	}
	
	// Then push the path to the head of the buffer
	strcpy_s(vTargetFilePath[PathsTail].aFilePath, sizeof(FilePath::aFilePath), pFilePath);
	
	cJSON * pJsonPathsTail = cJSON_CreateNumber(PathsTail);
	
	FileContent OutFile = {0};
	
	FileContent File = pPlatformCtx->pReadFileFunc(SETTINGS_NAME);
	cJSON * pJsonRoot = nullptr;
	
	cJSON * pJsonRecentPathArray = cJSON_CreateArray();
		
	for (unsigned j = 0; j < (unsigned)vTargetFilePath.Size; ++j)
	{
		FilePath& Path = vTargetFilePath[j];
			
		cJSON * pFilePathValue = cJSON_CreateString(Path.aFilePath);
		cJSON_AddItemToArray(pJsonRecentPathArray, pFilePathValue);
	}
	
	if (File.pFile)
	{
		pJsonRoot = cJSON_ParseWithLength((char*)File.pFile, File.Size);
		
		if (cJSON_HasObjectItem(pJsonRoot, pBufferSettingName)) 
		{
			cJSON_ReplaceItemInObjectCaseSensitive(pJsonRoot, pBufferSettingName, pJsonRecentPathArray);
		}
		else
		{
			cJSON_AddItemToObjectCS(pJsonRoot, pBufferSettingName, pJsonRecentPathArray);
		}
		
		if(cJSON_HasObjectItem(pJsonRoot, pTailSettingName))
			cJSON_ReplaceItemInObjectCaseSensitive(pJsonRoot, pTailSettingName, pJsonPathsTail);
		else
			cJSON_AddItemToObjectCS(pJsonRoot, pTailSettingName, pJsonPathsTail);
		
		pPlatformCtx->pFreeFileContentFunc(&File);
	}
	else
	{
		pJsonRoot = cJSON_CreateObject();
		cJSON_AddItemToObjectCS(pJsonRoot, pBufferSettingName, pJsonRecentPathArray);
		cJSON_AddItemToObjectCS(pJsonRoot, pTailSettingName, pJsonPathsTail);
	}
	
	OutFile.pFile = cJSON_Print(pJsonRoot);
	OutFile.Size = strlen((char*)OutFile.pFile);
	
	pPlatformCtx->pWriteFileFunc(&OutFile, SETTINGS_NAME);
	free(pJsonRoot);	
}

void CrazyLog::SaveDefaultColorsInSettings(PlatformContext* pPlatformCtx) {
	FileContent OutFile = {0};
	
	FileContent File = pPlatformCtx->pReadFileFunc(SETTINGS_NAME);
	cJSON * pJsonRoot = nullptr;
	
	cJSON * pJsonColorArray = cJSON_CreateArray();
		
	for (unsigned j = 0; j < (unsigned)vDefaultColors.Size; ++j)
	{
		ImVec4& Color = vDefaultColors[j];
			
		char aColorBuf[9];
		sprintf(aColorBuf, "#%02X%02X%02X%02X", 
		        (int)(Color.x*255.f), 
		        (int)(Color.y*255.f), 
		        (int)(Color.z*255.f),
		        (int)(Color.w*255.f));
			
		cJSON * pColorValue = cJSON_CreateString(aColorBuf);
		cJSON_AddItemToArray(pJsonColorArray, pColorValue);
	}
	
	if (File.pFile)
	{
		pJsonRoot = cJSON_ParseWithLength((char*)File.pFile, File.Size);
		
		if (cJSON_HasObjectItem(pJsonRoot, "default_colors")) 
		{
			cJSON_ReplaceItemInObjectCaseSensitive(pJsonRoot, "default_colors", pJsonColorArray);
		}
		else
		{
			cJSON_AddItemToObjectCS(pJsonRoot,"default_colors", pJsonColorArray);
		}
		
		pPlatformCtx->pFreeFileContentFunc(&File);
	}
	else
	{
		pJsonRoot = cJSON_CreateObject();
		cJSON_AddItemToObjectCS(pJsonRoot,"default_colors", pJsonColorArray);
	}
	
	OutFile.pFile = cJSON_Print(pJsonRoot);
	OutFile.Size = strlen((char*)OutFile.pFile);
	
	pPlatformCtx->pWriteFileFunc(&OutFile, SETTINGS_NAME);
	free(pJsonRoot);	
}

void CrazyLog::SaveTypeInSettings(PlatformContext* pPlatformCtx, const char* pKey, int Type, const void* pValue) {
	FileContent OutFile = {0};
	
	FileContent File = pPlatformCtx->pReadFileFunc(SETTINGS_NAME);
	cJSON * pJsonRoot = nullptr;
	if (File.pFile)
	{
		pJsonRoot = cJSON_ParseWithLength((char*)File.pFile, File.Size);
		cJSON * pJsonValue = nullptr;
		if (Type == cJSON_String) 
		{
			pJsonValue = cJSON_CreateString((const char*)pValue);
		}
		else if (Type == cJSON_Number)
		{
			pJsonValue = cJSON_CreateNumber(*(const int *)pValue);
		}
		else if (Type == cJSON_True || Type == cJSON_False)
		{
			pJsonValue = cJSON_CreateBool(*(const bool *)pValue);
		}
		
		if(cJSON_HasObjectItem(pJsonRoot, pKey))
			cJSON_ReplaceItemInObjectCaseSensitive(pJsonRoot, pKey, pJsonValue);
		else
			cJSON_AddItemToObjectCS(pJsonRoot, pKey, pJsonValue);
		
		pPlatformCtx->pFreeFileContentFunc(&File);
	}
	else
	{
		pJsonRoot = cJSON_CreateObject();
		
		cJSON * pJsonValue = nullptr;
		if (Type == cJSON_String) 
		{
			pJsonValue = cJSON_CreateString((const char*)pValue);
		}
		else if (Type == cJSON_Number)
		{
			pJsonValue = cJSON_CreateNumber(*(const int *)pValue);
		}
		else if (Type == cJSON_True || Type == cJSON_False)
		{
			pJsonValue = cJSON_CreateBool(*(const bool *)pValue);
		}
		
		cJSON_AddItemToObjectCS(pJsonRoot, pKey, pJsonValue);
	}
	
	OutFile.pFile = cJSON_Print(pJsonRoot);
	OutFile.Size = strlen((char*)OutFile.pFile);
	
	pPlatformCtx->pWriteFileFunc(&OutFile, SETTINGS_NAME);
	free(pJsonRoot);	
}

void CrazyLog::PasteFilter(PlatformContext* pPlatformCtx) {
	const char* pClipboardText = ImGui::GetClipboardText();
	cJSON * pFilterObj = cJSON_Parse(pClipboardText);
	if (pFilterObj == nullptr)
		return;
	
	cJSON * pFilterValue = cJSON_GetObjectItemCaseSensitive(pFilterObj, "value");
	strcpy_s(Filter.aInputBuf, sizeof(Filter.aInputBuf),  pFilterValue->valuestring);
			
	cJSON * pColorArray = cJSON_GetObjectItemCaseSensitive(pFilterObj, "colors");
			
	unsigned ColorsCounter = 0;
	int ColorsCount = cJSON_GetArraySize(pColorArray);
	Filter.vSettings.resize(ColorsCount);
	
	cJSON * pColor = nullptr;
			
	cJSON_ArrayForEach(pColor, pColorArray)
	{
		ImVec4 Color;
		StringUtils::HexToRGB(pColor->valuestring, &Color.x);
		Color.x /= 255;
		Color.y /= 255;
		Color.z /= 255;
		Color.w /= 255;
				
		memcpy(&Filter.vSettings[ColorsCounter].Color, &Color, sizeof(ImVec4));
				
		ColorsCounter++;
	}
			
	Filter.Build(&vDefaultColors, false);
		
	free(pFilterObj);
}

void CrazyLog::CopyFilter(PlatformContext* pPlatformCtx, CrazyTextFilter* pFilter) {
	
	cJSON * pFilterObj = cJSON_CreateObject();
		
	cJSON * pFilterValue = cJSON_CreateString(pFilter->aInputBuf);
	cJSON_AddItemToObject(pFilterObj, "value", pFilterValue);
		
	cJSON * pJsonColorArray = cJSON_AddArrayToObject(pFilterObj, "colors");
	for (unsigned j = 0; j < (unsigned)pFilter->vSettings.Size; ++j)
	{
		ImVec4& Color = pFilter->vSettings[j].Color;
			
		char aColorBuf[9];
		sprintf(aColorBuf, "#%02X%02X%02X%02X", 
		        (int)(Color.x*255.f), 
		        (int)(Color.y*255.f), 
		        (int)(Color.z*255.f),
		        (int)(Color.w*255.f));
			
		cJSON* pFilterColor = cJSON_CreateString(aColorBuf);
		cJSON_AddItemToObject(pJsonColorArray, "color", pFilterColor);
	}
	
	ImGui::SetClipboardText(cJSON_Print(pFilterObj));
	free(pFilterObj);
}


void CrazyLog::DeleteFilter(PlatformContext* pPlatformCtx) 
{
	if (FilterSelectedIdx > 0)
	{
		LoadedFilters.erase(&LoadedFilters[FilterSelectedIdx]);
		SaveLoadedFilters(pPlatformCtx);
		
		FilterSelectedIdx = 0;
		Filter.Clear();
		bAlreadyCached = false;
		FiltredLinesCount = 0;
		
		SetLastCommand("FILTER DELETED");
	}
}

void CrazyLog::SaveFilter(PlatformContext* pPlatformCtx, char* pFilterName, CrazyTextFilter* pFilter) 
{
	// Make sure to build it so it construct the filters + colors
	pFilter->Build(&vDefaultColors);
	
	size_t FilterNameLen = StringUtils::Length(pFilterName);
	
	LoadedFilters.resize(LoadedFilters.size() + 1, { 0 });
	memcpy(LoadedFilters[LoadedFilters.size() - 1].aName, pFilterName, FilterNameLen+1);
	LoadedFilters[LoadedFilters.size() - 1].Filter = *pFilter;
	
	SaveLoadedFilters(pPlatformCtx);
	FilterSelectedIdx = LoadedFilters.size() - 1;
	
	SetLastCommand("FILTER SAVED");
}

void CrazyLog::SaveFilter(PlatformContext* pPlatformCtx, int FilterIdx, CrazyTextFilter* pFilter) 
{
	// Make sure to build it so it construct the filters + colors
	pFilter->Build(&vDefaultColors);
	
	LoadedFilters[FilterIdx].Filter = *pFilter;
	
	SaveLoadedFilters(pPlatformCtx);
	FilterSelectedIdx = FilterIdx;
	
	SetLastCommand("FILTER SAVED");
}

// This method will append to the buffer
void CrazyLog::AddLog(const char* pFileContent, int FileSize) 
{
	int OldSize = Buf.size();
	Buf.append(pFileContent, pFileContent + FileSize);
	
	for (int NewSize = Buf.size(); OldSize < NewSize; OldSize++)
	{
		if (Buf[OldSize] == '\n')
		{
			vLineOffsets.push_back(OldSize + 1);
		}
	}
	
	bAlreadyCached = false;
}

// This method will stomp the old buffer;
void CrazyLog::SetLog(const char* pFileContent, int FileSize) 
{
	Buf.Buf.clear();
	vLineOffsets.clear();
	
	Buf.append(pFileContent, pFileContent + FileSize);
	vLineOffsets.push_back(0);
	
	int old_size = 0;
	for (int new_size = Buf.size(); old_size < new_size; old_size++)
	{
		if (Buf[old_size] == '\n')
		{
			vLineOffsets.push_back(old_size + 1);
		}
	}
	
	// Reset the cache and reserve the max amount needed
	ClearCache();
	vFiltredLinesCached.reserve_discard(vLineOffsets.Size);
}

void CrazyLog::ClearCache() {
	vFiltredLinesCached.clear();
	FiltredLinesCount = 0;
	bAlreadyCached = false;
}


void CrazyLog::PreDraw(PlatformContext* pPlatformCtx)
{
	if (bWantsToScaleFont)
	{
		bWantsToScaleFont = false;
		
		// NOTE(matiasp): In order to the font scaling to work properly we need to build the fonts again
		// and our backend (dx11) needs to rebuild the font texture.
		BuildFonts();
		pPlatformCtx->bWantsToRebuildFontTexture = true;
	}
}

void CrazyLog::Draw(float DeltaTime, PlatformContext* pPlatformCtx, const char* title, bool* pOpen /*= NULL*/)
{
	bool bIsShiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	bool bIsCtrlressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
	bool bIsAltPressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);
	unsigned ExtraFlags = bIsShiftPressed || bIsCtrlressed || bIsAltPressed ? ImGuiWindowFlags_NoScrollWithMouse : 0;
	
	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar | ExtraFlags ;
	
	if (!ImGui::Begin(title, pOpen, WindowFlags))
	{
		ImGui::End();
		return;
	}
	
	DrawMainBar(DeltaTime, pPlatformCtx);
	
	//=============================================================
	// Target
	
	DrawTarget(DeltaTime, pPlatformCtx);
	
	//=============================================================
	// Filters 
	
	bool bSomeFilterChanged = DrawFilters(DeltaTime, pPlatformCtx);
	
	//=============================================================
	// Output
	
	ImVec2 sz = ImVec2(-FLT_MIN, 0.0f);
	
	ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
	if (bIsPeeking)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100,0,0,255)); 
		ImGui::Button("VIEW: PEEKING", sz);
	} 
	else if (AnyFilterActive())
	{
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20,100,38,255));
		ImGui::Button("VIEW: FILTERED", sz);
	} 
	else 
	{
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(66,66,66,255));
		ImGui::Button("VIEW: FULL", sz);
	}
	
	ImGui::PopStyleColor();
	ImGui::PopItemFlag();
	
	ImGui::SeparatorText("OUTPUT");
	
	if (!bIsShiftPressed && !bIsAltPressed && SelectionSize > 1.f) {
		SelectionSize = 0.f;
	}
	
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
	
	if (ImGui::BeginChild("Output", ImVec2(0, -25), false, ImGuiWindowFlags_HorizontalScrollbar | ExtraFlags))
	{
		bool bWantsToCopy = false;
		if (bIsCtrlressed && ImGui::IsKeyPressed(ImGuiKey_V))
		{
			if (ImGui::IsWindowHovered())
			{
				LoadClipboard();
				SetLastCommand("CLIPBOARD LOADED");
			}
		}
		
		if (bIsCtrlressed && ImGui::IsKeyPressed(ImGuiKey_C))
		{
			if (ImGui::IsWindowHovered())
			{
				bWantsToCopy = true;
				SetLastCommand("VIEW COPIED TO CLIPBOARD");
			}
		}
		
		if (bIsCtrlressed && ImGui::GetIO().MouseWheel != 0.f)
		{
			if (ImGui::IsWindowHovered())
			{
				FontScale += ImGui::GetIO().MouseWheel;
				bWantsToScaleFont = true;
				SetLastCommand("FONT SCALED");
			}
		}
		
		if (ImGui::IsKeyReleased(ImGuiKey_F5))
		{
			LoadFile(pPlatformCtx);
			SetLastCommand("FILE REFRESHED");
		}
		
		if (!bIsAltPressed && ImGui::BeginPopupContextWindow())
		{
			ImGui::Checkbox("Auto-scroll", &bAutoScroll);
			ImGui::Separator();
			
			if (ImGui::Selectable("Copy")) 
			{
				bWantsToCopy = true;
			}
			if (ImGui::Selectable("Paste")) 
			{
				LoadClipboard();
			}
			if (ImGui::Selectable("Clear")) 
			{
				Clear();
			}
		
			ImGui::EndPopup();
		}
		
		if (bWantsToCopy)
			ImGui::LogToClipboard();
		
		float OneTimeScrollValue = -1.f;
		
		if (bIsPeeking) 
		{
			if (PeekScrollValue > -1.f) 
			{
				OneTimeScrollValue = PeekScrollValue;
				PeekScrollValue = -1.f;
			}
			
			if (ImGui::IsKeyReleased(ImGuiKey_MouseX1) || bSomeFilterChanged) 
			{
				OneTimeScrollValue = FiltredScrollValue;
				bIsPeeking = false;
				
				SetLastCommand("EXIT PEEK VIEW");
			}
			
			if (!AnyFilterActive()) 
			{
				bIsPeeking = false;	
			}
		}
		
		if (!bAlreadyCached && AnyFilterActive()) 
			FilterLines(pPlatformCtx);
		
		if (!bIsPeeking && AnyFilterActive())
		{
			DrawFiltredView(pPlatformCtx);
		}
		else
		{
			DrawFullView(pPlatformCtx);
		}
		
		
		if (bWantsToCopy)
			ImGui::LogFinish();
		
		if (OneTimeScrollValue > -1.f) {
			ImGui::SetScrollY(OneTimeScrollValue);
		}
		
		// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
		// Using a scrollbar or mouse-wheel will take away from the bottom edge.
		if (bAutoScroll && !bIsPeeking && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);
	}
	ImGui::PopFont();
	ImGui::EndChild();
	
	ImGui::SeparatorText(aLastCommand);
	
	ImGui::End();
}

template<typename T, size_t PADDING>
struct PaddedVector 
{
	ImVector<T> vPaddedVector;
	char aPadding[PADDING > 0 ? PADDING : 1];
};

static void FilterMT(int LineNo, CrazyLog* pLog, ImVector<int>* pOut) 
{
	const char* pBuf = pLog->Buf.begin();
	const char* pBufEnd = pLog->Buf.end();
	
	const char* pLineStart = pBuf + pLog->vLineOffsets[LineNo];
	const char* pLineEnd = (LineNo + 1 < pLog->vLineOffsets.Size) 
		? (pBuf + pLog->vLineOffsets[LineNo + 1] - 1) : pBufEnd;
	
	if (pLog->Filter.PassFilter(pLineStart, pLineEnd, pBufEnd, pLog->bIsAVXEnabled)) 
	{
		pOut->push_back(LineNo);
	}
}

void CrazyLog::FilterLines(PlatformContext* pPlatformCtx)
{
	if (FiltredLinesCount == 0)
	{
		vFiltredLinesCached.resize(0);
	}
	
	if (Filter.vFilters.size() > 0 && vLineOffsets.Size > 0)
	{
		if (bIsMultithreadEnabled)
		{
			LARGE_INTEGER TimestampBeforeFilter = pPlatformCtx->pGetWallClockFunc();
			
			// Parallel Execution
			{
				const int PendingSizeToFilter = vLineOffsets.Size - FiltredLinesCount;
				const int ItemsPerThread = PendingSizeToFilter / (SelectedExtraThreadCount + 1);
				const int* pDataCursor = &vLineOffsets[FiltredLinesCount];
				const int* pEnd = pDataCursor + PendingSizeToFilter;
	
				// Adding Padding to avoid false sharing when increasing the Size/Capacity value of the vectors 
				PaddedVector<int,128> vThreadsBuffer[MAX_EXTRA_THREADS + 1];
				memset(&vThreadsBuffer, 0, sizeof(vThreadsBuffer));
	
				std::thread aThreads[MAX_EXTRA_THREADS];
				CrazyLog* pLog = this;
				auto ThreadJob = [ItemsPerThread, pEnd, pLog, PendingSizeToFilter](const int* pDataCursor, ImVector<int>* pOut) -> void
				{
					for (int i = 0; i < ItemsPerThread; ++i) 
					{
						int LineNo = (PendingSizeToFilter - (int)(pEnd - &pDataCursor[i])) + pLog->FiltredLinesCount;
						FilterMT(LineNo, pLog, pOut);
					}
				};

				for (int i = 0; i < SelectedExtraThreadCount; ++i)
				{
					new(aThreads + i)std::thread(ThreadJob, pDataCursor, &vThreadsBuffer[i].vPaddedVector);
					pDataCursor += ItemsPerThread;
				}
	
				while (pDataCursor < pEnd)
				{
					// work in this thread too
					int LineNo = (PendingSizeToFilter - (int)(pEnd - pDataCursor)) + pLog->FiltredLinesCount;
					FilterMT(LineNo, pLog, &vThreadsBuffer[SelectedExtraThreadCount].vPaddedVector);
					pDataCursor++;
				}
	
				// wait until all threads finished
				for (int i = 0; i < SelectedExtraThreadCount; ++i)
				{
					aThreads[i].join();
				}
	
				// calculate how much I need to dump in the result buffer
				// resize the vector with the final size
				unsigned TotalSize = 0;
				for (int i = 0; i < SelectedExtraThreadCount + 1; ++i)
				{
					TotalSize += vThreadsBuffer[i].vPaddedVector.Size;
				}
	
				unsigned OriginalSize = vFiltredLinesCached.Size;
				vFiltredLinesCached.resize(OriginalSize + TotalSize);
	
				// dump the new content into the filter result
				unsigned CopiedSize = 0;
				for (int i = 0; i < SelectedExtraThreadCount + 1; ++i)
				{
					if (vThreadsBuffer[i].vPaddedVector.Size == 0)
						continue;
		
					memcpy(&vFiltredLinesCached[OriginalSize + CopiedSize], 
						vThreadsBuffer[i].vPaddedVector.Data, 
						sizeof(int) * vThreadsBuffer[i].vPaddedVector.Size);
		
					CopiedSize += vThreadsBuffer[i].vPaddedVector.Size;
		
					if (CopiedSize >= TotalSize)
						break;
				}
			}
			
			
			float FilterTime = pPlatformCtx->pGetSecondsElapsedFunc(TimestampBeforeFilter, pPlatformCtx->pGetWallClockFunc());
			
			char aDeltaTimeBuffer[64];
			snprintf(aDeltaTimeBuffer, sizeof(aDeltaTimeBuffer), "FilterTime %.5f", FilterTime);
			SetLastCommand(aDeltaTimeBuffer);
			
		}
		else
		{
			LARGE_INTEGER TimestampBeforeFilter = pPlatformCtx->pGetWallClockFunc();
			
			const char* pBuf = Buf.begin();
			const char* pBufEnd = Buf.end();
		
			for (int LineNo = FiltredLinesCount; LineNo < vLineOffsets.Size; LineNo++)
			{
				const char* pLineStart = pBuf + vLineOffsets[LineNo];
				const char* pLineEnd = (LineNo + 1 < vLineOffsets.Size) ? (pBuf + vLineOffsets[LineNo + 1] - 1) : pBufEnd;
				if (Filter.PassFilter(pLineStart, pLineEnd, pBufEnd, bIsAVXEnabled)) 
				{
					vFiltredLinesCached.push_back(LineNo);
				}
		
			}
			
			float FilterTime = pPlatformCtx->pGetSecondsElapsedFunc(TimestampBeforeFilter, pPlatformCtx->pGetWallClockFunc());
			
			char aDeltaTimeBuffer[64];
			snprintf(aDeltaTimeBuffer, sizeof(aDeltaTimeBuffer), "FilterTime %.5f ", FilterTime);
			SetLastCommand(aDeltaTimeBuffer);
		}
		
	}
	
	bAlreadyCached = true;
	
	if (bStreamMode)
	{
		const char* pLineStart = Buf.begin() + vLineOffsets[vLineOffsets.Size - 1];
		const char* pLineEnd = Buf.end();
		
		// If we are streaming mode and the last line is empty, don't count it as filtered, 
		// because is going to be written eventually and if we filtered the empty line
		// then we will end up skipping those lines.
		FiltredLinesCount = pLineStart == pLineEnd ? vLineOffsets.Size - 1 : vLineOffsets.Size;
	}
	else
	{
		FiltredLinesCount = vLineOffsets.Size;
	}
}

void CrazyLog::SetLastCommand(const char* pLastCommand)
{
	snprintf(aLastCommand, sizeof(aLastCommand), "ver %.2f - TotalLines %i ResultLines %i - LastCommand: %s",
	         g_Version, vLineOffsets.Size, vFiltredLinesCached.Size, pLastCommand);
}

void CrazyLog::DrawFiltredView(PlatformContext* pPlatformCtx)
{
	bool bIsShiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	bool bIsCtrlressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
	bool bIsAltPressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);
	
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();
	ImGuiListClipper clipper;
	clipper.Begin(vFiltredLinesCached.Size);
	
	TempLineMatches.vLineMatches.reserve(20);
		
	while (clipper.Step())
	{
		for (int ClipperIdx = clipper.DisplayStart; ClipperIdx < clipper.DisplayEnd; ClipperIdx++)
		{
			int line_no = vFiltredLinesCached[ClipperIdx];
			const char* pLineStart = buf + vLineOffsets[line_no];
			const char* pLineEnd = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
			int64_t line_size = pLineEnd - pLineStart;
			
			bool bIsItemHovered = false;
			bool bShouldCheckHover = bIsAltPressed || bIsShiftPressed || bIsCtrlressed;
	
			const char* pLineCursor = pLineStart;
			
			CacheHighlightLineMatches(pLineStart, pLineEnd, &TempLineMatches);
			
			for (int j = 0; j < TempLineMatches.vLineMatches.Size; j++)
			{
				ImVec4 FilterColor = Filter.vSettings[TempLineMatches.vLineMatches[j].FilterIdxMatching].Color;
				
				const char* pHighlightWordBegin = pLineStart + TempLineMatches.vLineMatches[j].WordBeginOffset;
				const char* pHighlightWordEnd = pLineStart + TempLineMatches.vLineMatches[j].WordEndOffset + 1;
				if (pLineCursor <= pHighlightWordBegin)
				{
					ImGui::TextUnformatted(pLineCursor, pHighlightWordBegin);
					bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
					ImGui::SameLine(0.f,0.f);
							
					ImGui::PushStyleColor(ImGuiCol_Text, FilterColor);
					ImGui::TextUnformatted(pHighlightWordBegin, pHighlightWordEnd);
					bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
					ImGui::PopStyleColor();
					
					ImGui::SameLine(0.f,0.f);
					pLineCursor = pHighlightWordEnd;
				}
			}
					
			ImGui::TextUnformatted(pLineCursor, pLineEnd);
			bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
	
			// Peek Full Version
			if (bIsCtrlressed && bIsItemHovered) 
			{
				if (ImGui::IsKeyReleased(ImGuiKey_MouseLeft))
				{
					float TopOffset = ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y;
					float ItemPosY = (float)(line_no + 1) * ImGui::GetTextLineHeightWithSpacing();
		
					// We apply the same offset to maintain the same scroll position
					// between peeking and filtred view.
					PeekScrollValue = ItemPosY - TopOffset;
					FiltredScrollValue = ImGui::GetScrollY();
		
					bIsPeeking = true;
					
					SetLastCommand("ENTER PEEK VIEW");
				}
			}
			// Select lines from Filtered Version
			else if (bIsShiftPressed && bIsItemHovered) 
			{
				SelectionSize += ImGui::GetIO().MouseWheel;
				SelectionSize = max(SelectionSize, 1);
	
				int BottomLine = ClipperIdx;
				int TopLine = min(ClipperIdx + (int)SelectionSize - 1, vFiltredLinesCached.Size - 1);
	
				bool bWroteOnScratch = false;
				char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
				for (int j = BottomLine; j <= TopLine; j++) {
		
					int FilteredLineNo = vFiltredLinesCached[j];
					char* pFilteredLineStart = const_cast<char*>(buf + vLineOffsets[FilteredLineNo]);
					char* pFilteredLineEnd = const_cast<char*>((FilteredLineNo + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[FilteredLineNo + 1] - 1) : buf_end);
		
					size_t Size = pFilteredLineEnd+1 - pFilteredLineStart;
					bWroteOnScratch |= pPlatformCtx->ScratchMem.PushBack(Size, pFilteredLineStart) != nullptr;
				}
	
				if (bWroteOnScratch) {
					pPlatformCtx->ScratchMem.PushBack(1, &g_NullTerminator);
					//ImGui::SetNextWindowPos(ImGui::GetMousePos()+ ImVec2(20, 0), 0, ImVec2(0, 0));
					ImGui::SetTooltip(pScratchStart);
		
					if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
					{
						ImGui::SetClipboardText(pScratchStart);
						SetLastCommand("LINE SELECTION COPIED TO CLIPBOARD");
					}
				}
	
			}
			// Select chars from Filtered Version
			else if (bIsAltPressed && bIsItemHovered) 
			{
				SelectCharsFromLine(pPlatformCtx, pLineStart, pLineEnd);
			}
		}
	}
	
	if (vFiltredLinesCached.Size == 0 && bIsCtrlressed && ImGui::IsKeyReleased(ImGuiKey_MouseLeft))
	{
		if (ImGui::IsWindowHovered())
		{
			PeekScrollValue = ImGui::GetScrollY();
			FiltredScrollValue = ImGui::GetScrollY();
			
			bIsPeeking = true;
		}
			
	}
	
	TempLineMatches.vLineMatches.clear();
	
	clipper.End();
}

void CrazyLog::DrawFullView(PlatformContext* pPlatformCtx)
{
	bool bIsShiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	bool bIsAltPressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);
	
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();
	
	ImGuiListClipper clipper;
	clipper.Begin(vLineOffsets.Size);
	
	TempLineMatches.vLineMatches.reserve(20);
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			const char* line_start = buf + vLineOffsets[line_no];
			const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
			
			bool bIsItemHovered = false;
			bool bShouldCheckHover = bIsAltPressed || bIsShiftPressed;
			
			CacheHighlightLineMatches(line_start, line_end, &TempLineMatches);
					
			if (bIsPeeking && TempLineMatches.vLineMatches.Size > 0)
			{
				const char* pLineCursor = line_start;
				
				for (int i = 0; i < TempLineMatches.vLineMatches.Size; i++)
				{
					ImVec4 FilterColor = Filter.vSettings[TempLineMatches.vLineMatches[i].FilterIdxMatching].Color;
					
					const char* pHighlightWordBegin = line_start + TempLineMatches.vLineMatches[i].WordBeginOffset;
					const char* pHighlightWordEnd = line_start + TempLineMatches.vLineMatches[i].WordEndOffset + 1;
					if (pLineCursor <= pHighlightWordBegin)
					{
						ImGui::TextUnformatted(pLineCursor, pHighlightWordBegin);
						bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
						ImGui::SameLine(0.f,0.f);
						
						ImGui::PushStyleColor(ImGuiCol_Text, FilterColor);
						ImGui::TextUnformatted(pHighlightWordBegin, pHighlightWordEnd);
						bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
						ImGui::PopStyleColor();
						
						ImGui::SameLine(0.f,0.f);
						pLineCursor = pHighlightWordEnd;
					}
				}
				
				ImGui::TextUnformatted(pLineCursor, line_end);
				bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
			}
			else
			{
				ImGui::TextUnformatted(line_start, line_end);
				bIsItemHovered |= bShouldCheckHover && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
			}
					
			// Select lines full view
			if (bIsShiftPressed && bIsItemHovered) 
			{
				SelectionSize += ImGui::GetIO().MouseWheel;
				SelectionSize = max(SelectionSize, 1);
						
				int BottomLine = line_no;
				int TopLine = min(line_no + (int)SelectionSize - 1, vLineOffsets.Size - 1);
				const char* TopLineEnd = (TopLine + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[TopLine + 1] - 1) : buf_end;
				size_t Size = TopLineEnd - (buf + vLineOffsets[BottomLine]);
						
				char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
				if (pPlatformCtx->ScratchMem.PushBack(Size, (void*)(buf + vLineOffsets[BottomLine])) &&
					pPlatformCtx->ScratchMem.PushBack(1, &g_NullTerminator))
				{
					//ImGui::SetNextWindowPos(ImGui::GetMousePos() + ImVec2(20, 0), 0, ImVec2(0, 0.5));
					ImGui::SetTooltip(pScratchStart);
				}
						
				if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
				{
					ImGui::SetClipboardText(pScratchStart);
					SetLastCommand("LINE SELECTION COPIED TO CLIPBOARD");
				}
			}
			else if (bIsAltPressed && bIsItemHovered) 
			{
				SelectCharsFromLine(pPlatformCtx, line_start, line_end);
			}
					
		}
	}
	
	TempLineMatches.vLineMatches.clear();
	
	clipper.End();
}

void CrazyLog::DrawMainBar(float DeltaTime, PlatformContext* pPlatformCtx)
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Menu"))
		{
			if (ImGui::MenuItem("Save", nullptr, nullptr, aFilePathToLoad[0] != 0 && vFiltredLinesCached.Size > 0))
			{
				SaveFilteredView(pPlatformCtx, aFilePathToLoad);
				LoadFile(pPlatformCtx);
			}
			
			if (ImGui::BeginMenu("Save As..", vFiltredLinesCached.Size > 0))
			{
				char aFileName[MAX_PATH] = { 0 };
				if (ImGui::InputText("Name", aFileName, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
				{
					char aExePath[MAX_PATH] = { 0 };
					size_t ExePathLen = 0;
					pPlatformCtx->pGetExePathFunc(aExePath, MAX_PATH, ExePathLen, false);
					pPlatformCtx->pOpenURLFunc(aExePath);
					
					strcpy_s(aExePath + ExePathLen, sizeof(aExePath) - ExePathLen,  aFileName);
					
					SaveFilteredView(pPlatformCtx, aExePath);
				}
				
				ImGui::SameLine();
				
				ImGui::EndMenu();
			}
			
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Settings"))
		{
			if (ImGui::BeginMenu("Keybinds"))
			{
				ImGui::Text("KEYBINDS when hovering the output view: \n\n"
				            "[F5]                 Will refresh the loaded file. If new content is available it will append it. \n"
				            "[Ctrl+C]             Will copy the content of the output to the clipboard. \n"
				            "[Ctrl+V]             Will paste the clipboard into the output view. \n"
				            "[Ctrl+MouseWheel]    Will scale the font. \n"
				            "[Ctrl+Click]         Will peek that filtered hovered line in the full view of the logs. \n"
				            "[MouseButtonBack]    Will go back from peeking into the filtered view. \n"
				            "[Alt]                Will enter in word selection mode when hovering a word. \n"
				            "[Shift]              Will enter in line selection mode when hovering a line. \n"
				            "[MouseWheel]         While in word/line selection mode it will expand/shrink the selection. \n"
				            "[MouseMiddleClick]   While in word/line selection mode it will copy the selection to the clipboard. \n"
				            "[MouseLeftClick]     While in word selection mode it will copy the selection to the filter adding with OR operator. \n"
				            "[MouseRightClick]    While in word selection mode it will copy the selection to the filter adding with AND operator. \n"
				            "[MouseRightClick]    Will open the context menu with some options. \n");
				
				ImGui::EndMenu();
			}
			
			ImGui::Separator();
			
			bool bIsUsingAVXChanged = ImGui::Checkbox("Use AVX Instructions ", &bIsAVXEnabled);
			if (bIsUsingAVXChanged)
				SaveTypeInSettings(pPlatformCtx, "is_avx_enabled", cJSON_True, &bIsAVXEnabled);
			
			ImGui::SameLine();
			HelpMarker("Speeds up 15/10x the filter time. \n");
			
			bool bIsUsingMTChanged = ImGui::Checkbox("Multithread", &bIsMultithreadEnabled);
			if (bIsUsingMTChanged)
				SaveTypeInSettings(pPlatformCtx, "is_multithread_enabled", cJSON_True, &bIsMultithreadEnabled);
					
			if (bIsMultithreadEnabled)
			{
				bool bThreadCountChanged = ImGui::SliderInt("ExtraThreadCount", &SelectedExtraThreadCount, 0, MaxExtraThreadCount);
				if (bThreadCountChanged)
					SaveTypeInSettings(pPlatformCtx, "selected_thread_count", cJSON_Number, &SelectedExtraThreadCount);
			}
			
			ImGui::EndMenu();
		}
		//if (ImGui::MenuItem("MenuItem")) {} // You can also use MenuItem() inside a menu bar!
		if (ImGui::BeginMenu("About"))
		{
			
			ImGui::Text("@2023 Matias Pedro \nLicense: MIT \nVersion %.2f", g_Version);
			
			ImGui::Separator();
			
			if (ImGui::MenuItem("Check updates"))
			{
				pPlatformCtx->pOpenURLFunc(BINARIES_URL);
			}
			
			if (ImGui::MenuItem("Github"))
			{
				pPlatformCtx->pOpenURLFunc(GIT_URL);
			}
			
			if (ImGui::MenuItem("Bugs? - Suggestions?"))
			{
				pPlatformCtx->pOpenURLFunc(ISSUES_URL);
			}
				
			
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

void CrazyLog::DrawTarget(float DeltaTime, PlatformContext* pPlatformCtx)
{
	static TargetModeChangeReason PendingModeChange = TMCR_NONE;
	if (PendingModeChange != TMCR_NONE) {
		LastChangeReason = PendingModeChange;
		PendingModeChange = TMCR_NONE;
	}
	
	ImGui::SeparatorText("Target");
	
	ImGui::SetNextItemWidth(-110);
	if(ImGui::Combo("TargetMode", &(int)SelectedTargetMode, apTargetModeStr, IM_ARRAYSIZE(apTargetModeStr)))
		LastChangeReason = TMCR_NewModeSelected;
	
	ImGui::SameLine();
	if (ImGui::Button(">>")) {
		char aExePath[MAX_PATH] = { 0 };
		size_t PathLen = 0;
		pPlatformCtx->pGetExePathFunc(aExePath, MAX_PATH, PathLen, true);
		pPlatformCtx->pOpenURLFunc(aExePath);
	}
	
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
		ImGui::SetTooltip("Start new CrazyTown window.");
	
	bool bModeJustChanged = LastChangeReason != TMCR_NONE;
	
	if (SelectedTargetMode == TM_StreamLastModifiedFileFromFolder)
	{
		bool bLoadTriggerExternally = LastChangeReason == TMCR_RecentSelected;
		
		if (bModeJustChanged)
		{
			bStreamMode = false;
			bStreamFileLocked = false;
			bFolderQuery = false;
			
			memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
		}
		
		if (ImGui::Button("|")) 
		{
			ImGui::OpenPopup("RecentStreamPaths");
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
            ImGui::SetTooltip("Open list with recent entries.");
		
		ImGui::SameLine();
		
		ImGui::SetNextItemWidth(-110);
		if (ImGui::InputTextWithHint("FolderQuery", "Insert the folder query, ex: D:\\logs\\*.txt", aFolderQueryName, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue) 
			|| bLoadTriggerExternally)
		{
			memset(&LastLoadedFileData, 0, sizeof(LastLoadedFileData));
			
			bStreamFileLocked = false;
			bFolderQuery = true;
			SearchLatestFile(pPlatformCtx);
			
			if (aFolderQueryName[0] != 0) 
				RememberFilePath(pPlatformCtx, true, aFolderQueryName);
		}
		else if (bFolderQuery && !bStreamFileLocked)
		{
			if (FolderFetchCooldown > 0.f ) 
			{
				FolderFetchCooldown -= DeltaTime;
				if (FolderFetchCooldown <= 0.f) 
				{
					SearchLatestFile(pPlatformCtx);
				}
			}
		}
	
		ImGui::SameLine();
		HelpMarker("Loads the last written file that matches the query \n"
		           "and start streaming it into the output. \n");
		
		ImVec2 FilePathPos = ImGui::GetWindowPos() + ImVec2(30, 95);
		ImGui::SetNextWindowPos(FilePathPos);
		if (ImGui::BeginPopup("RecentStreamPaths") && StreamPathsTail != -1)
		{
			bool bFirstDisplay = true;
			for (int i = StreamPathsTail; i != StreamPathsTail || bFirstDisplay; i = RING_BUFFER_BACKWARDS(i, vRecentStreamPaths))
			{
				if (vRecentStreamPaths[i].aFilePath[0] == '\0')
					break;
					
				bFirstDisplay = false;
					
				if (ImGui::MenuItem(vRecentStreamPaths[i].aFilePath))
				{
					memcpy(aFolderQueryName, vRecentStreamPaths[i].aFilePath, sizeof(FilePath::aFilePath));
						
					SelectedTargetMode = TM_StreamLastModifiedFileFromFolder;
					PendingModeChange = TMCR_RecentSelected;
					
					ImGui::CloseCurrentPopup();
				}
			}
			
			ImGui::EndPopup();
		}
		
		if(bStreamMode)
		{
			if (FileContentFetchCooldown > 0 && bFileLoaded) 
			{
				FileContentFetchCooldown -= DeltaTime;
				if (FileContentFetchCooldown <= 0.f) 
				{
					FetchFile(pPlatformCtx);
					FileContentFetchCooldown = FileContentFetchSlider;
				}
			}
		}
		
		if (aFilePathToLoad[0] != 0)
		{
			ImGui::Checkbox("Locked", &bStreamFileLocked);
        	ImGui::SetItemTooltip("if true it will not search for a newer file, but it will keep streaming "
								  "the new content added to the file locked.");
			
			ImGui::SameLine();
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
			if (bStreamMode && bFileLoaded)
			{
				float FileFetchCooldownPercentage = FileContentFetchCooldown / FileContentFetchSlider;
				ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20,100,38,255 * FileFetchCooldownPercentage));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(66,66,66,255));
			}
		
			ImGui::Button("##FrequencyFeedback", ImVec2(20,0));
			ImGui::PopStyleColor();
			ImGui::PopItemFlag();
			
			ImGui::SameLine();
			ImGui::Text("Streaming file: %s", aFilePathToLoad);
		}
	}
	else if (SelectedTargetMode == TM_StaticText)
	{
		if (ImGui::Button("|")) 
		{
			ImGui::OpenPopup("RecentFilePaths");
		}
		
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
            ImGui::SetTooltip("Open list with recent entries.");
		
		ImGui::SameLine();
		
		bool bLoadTriggerExternally = LastChangeReason == TMCR_DragAndDrop || LastChangeReason == TMCR_RecentSelected;
		if (bModeJustChanged)
		{
			bStreamMode = false;
			bStreamFileLocked = false;
			bFolderQuery = false;
			
			// Don't clear the FilePath since we are setting it from the drag and drop logic
			if (!bLoadTriggerExternally) 
				memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
				
		}
		
		ImGui::SetNextItemWidth(-110);
		const char * pHint = "Insert the path to the file to load, you can also drag an drop, ex: D:\\logs\\file_name.txt";
		if (ImGui::InputTextWithHint("FilePath", pHint, aFilePathToLoad, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue) 
			|| bLoadTriggerExternally)
		{
			LoadFile(pPlatformCtx);
			if (aFilePathToLoad[0] != 0)
				RememberFilePath(pPlatformCtx, false, aFilePathToLoad);
		}
		
		ImGui::SameLine();
		
		HelpMarker("Full path of the file to load. \n");
		
		ImVec2 FilePathPos = ImGui::GetWindowPos() + ImVec2(30, 95);
		ImGui::SetNextWindowPos(FilePathPos);
		if (ImGui::BeginPopup("RecentFilePaths") && FilePathsTail != -1)
		{
			bool bFirstDisplay = true;
			for (int i = FilePathsTail; i != FilePathsTail || bFirstDisplay; i = RING_BUFFER_BACKWARDS(i, vRecentFilePaths))
			{
				if (vRecentFilePaths[i].aFilePath[0] == '\0')
					break;
					
				bFirstDisplay = false;
					
				if (ImGui::MenuItem(vRecentFilePaths[i].aFilePath))
				{
					memcpy(aFilePathToLoad, vRecentFilePaths[i].aFilePath, sizeof(FilePath::aFilePath));
						
					SelectedTargetMode = TM_StaticText;
					PendingModeChange = TMCR_RecentSelected;
					
					ImGui::CloseCurrentPopup();
				}
			}
			
			ImGui::EndPopup();
		}
	}
	else if (SelectedTargetMode == TM_StreamFromWebSocket)
	{
		if (bModeJustChanged)
		{
			bStreamMode = false;
			bStreamFileLocked = false;
			bFolderQuery = false;
			
			memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
			memset(aFolderQueryName, 0, sizeof(aFolderQueryName));
		}
	}
	
	LastChangeReason = TMCR_NONE;
}

bool CrazyLog::DrawFilters(float DeltaTime, PlatformContext* pPlatformCtx)
{
	bool bFilterChanged = false;
	bool bSelectedFilterChanged = false; 
	bool bCherryPickHasChanged = false;
	
	ImGui::SeparatorText("Filters");
	
	bFilterChanged = Filter.Draw(&vDefaultColors, "Filter ", -110.0f);
	ImGui::SameLine();
	HelpMarker("Conditions on how to filter the text, "
			   "you can also copy/paste filters to/from the clipboard using the plus button.");
	
	LastFrameFiltersCount = Filter.vFilters.Size;
	if (ImGui::BeginPopup("FilterOptions"))
	{
		
		int SelectableFlags = Filter.IsActive() ? 0 : ImGuiSelectableFlags_Disabled;
		if (ImGui::Selectable("Copy", false, SelectableFlags)) 
			CopyFilter(pPlatformCtx, &Filter);
		
		if (ImGui::Selectable("Paste")) 
			PasteFilter(pPlatformCtx);
		
		ImGui::EndPopup();
	}
	
	ImGui::SameLine();
	if (ImGui::Button("+##Filter", ImVec2(20,0))) {
		ImGui::OpenPopup("FilterOptions");
	}
		
	if (bWantsToSavePreset) 
	{
		ImGui::SetNextItemWidth(-195);
		bool bAcceptPresetName = ImGui::InputText("Provide preset name", aFilterNameToSave, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue);
		
		size_t FilterNameLen = StringUtils::Length(aFilterNameToSave);
		
		bool bCanConfirm = FilterNameLen && Filter.IsActive();
		
		ImGui::SameLine();
		if (bAcceptPresetName && bCanConfirm) 
		{
			SaveFilter(pPlatformCtx, aFilterNameToSave, &Filter);
			bWantsToSavePreset = false;
		}
		
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) 
			bWantsToSavePreset = false;
	}
	
	if (bWantsToOverridePreset)
	{
		struct Funcs 
		{ 
			static bool ItemGetter(void* pData, int n, const char** out_ppStr) 
			{ 
				ImVector<NamedFilter>& vrLoadedFilters = *((ImVector<NamedFilter>*)pData);
				NamedFilter& NamedFilter = vrLoadedFilters[n];
				*out_ppStr = NamedFilter.aName; 
				return true; 
			} 
		};
		
		ImGui::SetNextItemWidth(-235);
		bSelectedFilterChanged = ImGui::Combo("Select preset to override", &FilterToOverrideIdx, &Funcs::ItemGetter, 
											  (void*)&LoadedFilters, LoadedFilters.Size);
		ImGui::SameLine();
		
		if (bSelectedFilterChanged && FilterToOverrideIdx != 0) {
			SaveFilter(pPlatformCtx, FilterToOverrideIdx, &Filter);
			bWantsToOverridePreset = false;
			FilterToOverrideIdx = 0;
		}
		
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			bWantsToOverridePreset = false;
			FilterToOverrideIdx = 0;
		}
	}
	
	if (!bWantsToSavePreset && !bWantsToOverridePreset) 
	{
		bSelectedFilterChanged = DrawPresets(DeltaTime, pPlatformCtx);
	}

	if (bFilterChanged) 
	{
		FilterSelectedIdx = 0;
			
		bAlreadyCached = false;
		FiltredLinesCount = 0;
			
		SetLastCommand("FILTER CHANGED");
	}
	
	if (Filter.IsActive()) 
	{
		bCherryPickHasChanged = DrawCherrypick(DeltaTime, pPlatformCtx);
		if (bSelectedFilterChanged) 
		{
			EnableMask = 0xFFFFFFFF;
			
			bAlreadyCached = false;
			FiltredLinesCount = 0;
			
			SetLastCommand("SELECTED PRESET CHANGED");
		}
		
		if (bCherryPickHasChanged)
		{
			bAlreadyCached = false;
			FiltredLinesCount = 0;
			
			SetLastCommand("CHERRY PICK CHANGED");
		}
	}
	
	return bFilterChanged | bSelectedFilterChanged | bCherryPickHasChanged;
}

bool CrazyLog::DrawPresets(float DeltaTime, PlatformContext* pPlatformCtx)
{
	bool bSelectedFilterChanged = false;
	if (LoadedFilters.Size > 0)
	{
		struct Funcs 
		{ 
			static bool ItemGetter(void* pData, int n, const char** out_ppStr) 
			{ 
				ImVector<NamedFilter>& vrLoadedFilters = *((ImVector<NamedFilter>*)pData);
				NamedFilter& NamedFilter = vrLoadedFilters[n];
				*out_ppStr = NamedFilter.aName; 
				return true; 
			} 
		};
		
		ImGui::SetNextItemWidth(-110);
		bSelectedFilterChanged = ImGui::Combo("Presets", &FilterSelectedIdx, &Funcs::ItemGetter,
		                                      (void*)&LoadedFilters, LoadedFilters.Size);
		
		ImGui::SameLine();
		HelpMarker(	"Those will be stored under FILTER.json");
		
		if (bSelectedFilterChanged)
		{
			Filter = LoadedFilters[FilterSelectedIdx].Filter;
		}
	}
	
	if (ImGui::BeginPopup("PresetsOptions"))
	{
		
		int SelectableFlags = Filter.IsActive() ? 0 : ImGuiSelectableFlags_Disabled;
		if (ImGui::Selectable("Save", false, SelectableFlags)) {
			memset(aFilterNameToSave, 0, ArrayCount(aFilterNameToSave));
			bWantsToSavePreset = true;
		}
		
		SelectableFlags = LoadedFilters.size() ? SelectableFlags : ImGuiSelectableFlags_Disabled;
		if (ImGui::Selectable("Override", false, SelectableFlags)) {
			FilterToOverrideIdx = 0;
			bWantsToOverridePreset = true;
		}
		
		SelectableFlags = FilterSelectedIdx != 0 ? 0 : ImGuiSelectableFlags_Disabled;
		if (ImGui::Selectable("Delete", false, SelectableFlags)) 
			DeleteFilter(pPlatformCtx);
		
		ImGui::EndPopup();
	}
	
	ImGui::SameLine();
	if (ImGui::Button("+##Presets", ImVec2(20,0)))
		ImGui::OpenPopup("PresetsOptions");
	
	return bSelectedFilterChanged;
}


bool CrazyLog::DrawCherrypick(float DeltaTime, PlatformContext* pPlatformCtx)
{
	bool bAnyFlagChanged = false;
	if (ImGui::TreeNode("Cherrypick"))
	{
		static int LastSelectedColorIdx = 0;
		if (ImGui::BeginPopup("ColorPresetsPicker"))
		{
			char ColorIdxStr[16] = { 0 };
			for (int i = 0; i < vDefaultColors.size(); i++) 
			{
				snprintf(ColorIdxStr, sizeof(ColorIdxStr), "##cps%i", i);
				
				ImGui::SameLine();
				if (ImGui::ColorButton(ColorIdxStr, vDefaultColors[i],
				                       ImGuiColorEditFlags_NoPicker 
				                       | ImGuiColorEditFlags_NoInputs 
				                       | ImGuiColorEditFlags_NoTooltip 
				                       | ImGuiColorEditFlags_NoLabel))
				{
					Filter.vSettings[LastSelectedColorIdx].Color = vDefaultColors[i];
				
					if (FilterSelectedIdx != 0) 
					{
						LoadedFilters[FilterSelectedIdx].Filter.vSettings[LastSelectedColorIdx].Color = Filter.vSettings[LastSelectedColorIdx].Color;
						SaveLoadedFilters(pPlatformCtx);
					}
				
					ImGui::CloseCurrentPopup();
				}
				
				ImGui::SameLine();
			}
			
			static ImVec4 SelectedColor = ImVec4(0, 0, 0, 1);
			
			if (bIsEditingColors)
			{
				if (ImGui::Button("Save"))
				{
					SaveDefaultColorsInSettings(pPlatformCtx);
					bIsEditingColors = false;
				}
				
				ImGui::SameLine();
				bool bColorHasChanged = ImGui::ColorEdit4("NewColor", (float*)&SelectedColor.x, 
				                                          ImGuiColorEditFlags_NoInputs 
				                                          | ImGuiColorEditFlags_NoOptions
				                                          | ImGuiColorEditFlags_NoDragDrop
				                                          | ImGuiColorEditFlags_NoTooltip
				                                          | ImGuiColorEditFlags_NoAlpha
				                                          | ImGuiColorEditFlags_NoTooltip);
			
				ImGui::Dummy(ImVec2(0,0));
				ImGui::SameLine();
				for (int i = 0; i < vDefaultColors.size(); i++)
				{
					snprintf(ColorIdxStr, sizeof(ColorIdxStr), "-##cpr%i", i);
					if (ImGui::Button(ColorIdxStr)) {
						vDefaultColors.erase(&vDefaultColors[i]);
					}
				
					ImGui::SameLine(0, 12);
				}
			
				ImGui::Dummy(ImVec2(0,0));
				ImGui::SameLine(0, 43);
				if (ImGui::Button("+##AddDefaultColor",ImVec2(20,0)))
				{
					vDefaultColors.push_back(SelectedColor);
				}
			}
			else
			{
				if (ImGui::Button("Edit"))
				{
					bIsEditingColors = true;
				}
			}
			
			ImGui::EndPopup();
		}
		
		for (int i = 0; i != Filter.vFilters.Size; i++)
		{
			char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
			bool bColorHasChanged = ImGui::ColorEdit4(pScratchStart, (float*)&Filter.vSettings[i].Color.x, 
			                                          ImGuiColorEditFlags_NoPicker 
			                                          | ImGuiColorEditFlags_NoInputs 
			                                          | ImGuiColorEditFlags_NoTooltip 
			                                          | ImGuiColorEditFlags_NoLabel);
	
			if (ImGui::IsKeyPressed(ImGuiKey_MouseLeft) && ImGui::IsItemHovered()) 
			{
				LastSelectedColorIdx = i;
				ImGui::OpenPopup("ColorPresetsPicker");
			}
			
			ImGui::SameLine();
			size_t FilterSize = Filter.vFilters[i].EndOffset - Filter.vFilters[i].BeginOffset;
			
			pPlatformCtx->ScratchMem.PushBack(FilterSize, (void*)(&Filter.aInputBuf[Filter.vFilters[i].BeginOffset]));
			
			char ToggleIdStr[4] = { 0 };
			snprintf(ToggleIdStr, sizeof(ToggleIdStr), "##%i", i);
			size_t ToggleSize = StringUtils::Length(ToggleIdStr);
			pPlatformCtx->ScratchMem.PushBack(ToggleSize, ToggleIdStr);
			
			pPlatformCtx->ScratchMem.PushBack(1, &g_NullTerminator);
				
			bool bChanged = ImGui::CheckboxFlags(pScratchStart, (ImU64*) &EnableMask, 1ull << i);
			if (bChanged) 
			{
				// Keep the filter setting in sync
				Filter.vSettings[i].bIsEnabled = (EnableMask & (1ull << i));
				bAnyFlagChanged = true;
				
#if SAVE_ENABLE_MASK
				if (FilterSelectedIdx != 0) {
					LoadedFilters[FilterSelectedIdx].Filter.vSettings[i].bIsEnabled = Filter.vSettings[i].bIsEnabled;
					SaveLoadedFilters(pPlatformCtx);
				}
#endif
				
			} 
			else
			{
				// Keep the Mask in sync with the setting
				if (Filter.vSettings[i].bIsEnabled)
					EnableMask |= (1ull << i);
				else
					EnableMask &= ~(1ull << i);
			}
		}
			
		ImGui::TreePop();
	}
	
	return bAnyFlagChanged;
}

char* CrazyLog::GetWordStart(const char* pLineStart, char* pWordCursor) 
{
	bool bWasWhiteSpace = StringUtils::IsWhitspace(pWordCursor);
	bool bWasChar = StringUtils::IsWordChar(pWordCursor);
	while (pLineStart < pWordCursor)
	{
		pWordCursor--;
		bool bIsChar = StringUtils::IsWordChar(pWordCursor);
		bool bIsWhitespace = StringUtils::IsWhitspace(pWordCursor);
		bool bDifferentNonChar = !bIsChar && *pWordCursor != *(pWordCursor+1);
		if (bWasChar != bIsChar || bWasWhiteSpace != bIsWhitespace || bDifferentNonChar) 
		{
			bWasChar = bIsChar;
			bWasWhiteSpace = bIsWhitespace;
			return pWordCursor+1;
		}
	}
	
	return pWordCursor;
}

char* CrazyLog::GetWordEnd(const char* pLineEnd, char* pWordCursor, int WordAmount) 
{
	bool bWasWhiteSpace = StringUtils::IsWhitspace(pWordCursor);
	bool bWasChar = StringUtils::IsWordChar(pWordCursor);
	
	int WordCounter = 0;
	while (pLineEnd > pWordCursor)
	{
		pWordCursor++;
		bool bIsChar = StringUtils::IsWordChar(pWordCursor);
		bool bIsWhitespace = StringUtils::IsWhitspace(pWordCursor);
		bool bDifferentNonChar = !bIsChar && *pWordCursor != *(pWordCursor-1);
		if (bWasChar != bIsChar && bIsChar || bWasWhiteSpace != bIsWhitespace || bDifferentNonChar) 
		{
			bWasChar = bIsChar;
			bWasWhiteSpace = bIsWhitespace;
			WordCounter++;
			if (WordAmount == WordCounter)
			{
				return pWordCursor;
			}
		}
	}
	
	return pWordCursor;
}

void CrazyLog::SelectCharsFromLine(PlatformContext* pPlatformCtx, const char* pLineStart, const char* pLineEnd)
{
	SelectionSize += ImGui::GetIO().MouseWheel;
	SelectionSize = max(SelectionSize, 1);
	
	int64_t LineSize = pLineEnd - pLineStart;
	
	float TabSize = 0.f;
	int TabCounter = 0;
	for (int j = 0; j < LineSize; ++j)
	{
		if (pLineStart[j] == '\t')
			TabCounter++;
		else
			break;
	}

	float TabOffset = 0.f;
	if (TabCounter > 0) 
	{
		TabOffset = ImGui::CalcTextSize(&pLineStart[0], &pLineStart[TabCounter]).x;
	}
	
	TabOffset *= 0.75f;
	
	float MousePosX = ImGui::GetMousePos().x;
	float TextOffset = ImGui::GetCursorScreenPos().x;
	
	for (int j = TabCounter; j < LineSize; ++j)
	{
		float CharSize = ImGui::CalcTextSize(&pLineStart[j], &pLineStart[j + 1]).x;
		float CharStartPos = TextOffset + TabOffset + (CharSize * j);
		float CharEndPos = CharStartPos + CharSize;
		
		if (MousePosX >= CharStartPos && MousePosX < CharEndPos)
		{
			char* pWordCursor = const_cast<char*>(&pLineStart[j]);
			char* pStartChar = GetWordStart(pLineStart, pWordCursor);
			char* pEndChar = GetWordEnd(pLineEnd, pWordCursor, (int)SelectionSize);
			
			char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
			size_t RequiredSize = pEndChar - pStartChar;
			if (pPlatformCtx->ScratchMem.PushBack(RequiredSize, pStartChar) &&
				pPlatformCtx->ScratchMem.PushBack(1, &g_NullTerminator))
			{
				if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
				{
					ImGui::SetClipboardText(pScratchStart);
					SetLastCommand("WORD SELECTION COPIED TO CLIPBOARD");
				}
				else if (ImGui::IsKeyReleased(ImGuiKey_MouseLeft))
				{
					if(Filter.vFilters.size() > 0)
						strcat_s(Filter.aInputBuf, " || ");
					
					strcat_s(Filter.aInputBuf, (char*)pScratchStart);
					Filter.Build(&vDefaultColors);
					
					FilterSelectedIdx = 0;
			
					bAlreadyCached = false;
					FiltredLinesCount = 0;
					
					SetLastCommand("WORD SELECTION ADDED TO FILTER");
				}
				else if (ImGui::IsKeyReleased(ImGuiKey_MouseRight))
				{
					if(Filter.vFilters.size() > 0)
						strcat_s(Filter.aInputBuf, " && ");
					
					strcat_s(Filter.aInputBuf, (char*)pScratchStart);
					Filter.Build(&vDefaultColors);
					
					FilterSelectedIdx = 0;
			
					bAlreadyCached = false;
					FiltredLinesCount = 0;
					
					SetLastCommand("WORD SELECTION ADDED TO FILTER");
				}
				
				ImGui::SetTooltip(pScratchStart);
			}
			
			break;
		}
	}
}

bool CrazyLog::AnyFilterActive () const
{
	for (int i = 0; i != Filter.vFilters.Size; i++)
	{
		bool bFilterEnabled = Filter.vSettings[i].bIsEnabled;
		if (bFilterEnabled)
			return true;
	}
	
	return false;
}

void CrazyLog::CacheHighlightLineMatches(const char* pLineBegin, const char* pLineEnd, HighlightLineMatches* pFiltredLineMatch)
{
	pFiltredLineMatch->vLineMatches.resize(0);
	
	for (int i = 0; i != Filter.vFilters.Size; i++)
	{
		bool bFilterEnabled = Filter.vSettings[i].bIsEnabled;
		if (!bFilterEnabled)
			continue;
		
		const CrazyTextFilter::CrazyTextRange& f = Filter.vFilters[i];
		if (f.Empty())
			continue;
		
		if (Filter.aInputBuf[f.BeginOffset] == '!')
			continue;

		CacheHighlightMatchingWord(pLineBegin, pLineEnd, i, pFiltredLineMatch);
	}
	
	if (pFiltredLineMatch->vLineMatches.Size > 0)
		qsort(pFiltredLineMatch->vLineMatches.Data, pFiltredLineMatch->vLineMatches.Size, sizeof(HighlightLineMatchEntry), HighlightLineMatchEntry::SortFunc);
}

void CrazyLog::CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, int FilterIdx, 
                                          HighlightLineMatches* pFiltredLineMatch)
{
	const CrazyTextFilter::CrazyTextRange& f = Filter.vFilters[FilterIdx];
	
	const char* pWordBegin = &Filter.aInputBuf[f.BeginOffset];
	const char* pWordEnd = &Filter.aInputBuf[f.EndOffset];
	
	if (!pWordEnd)
		pWordEnd = pWordBegin + strlen(pWordBegin);

	const char FirstWordChar = (char)ImToUpper(*pWordBegin);
	
	const char* pLineStart = pLineBegin;
		
	while ((!pLineEnd && *pLineBegin) || (pLineEnd && pLineBegin < pLineEnd))
	{
		// If our line begin cursor match the first char, then try to see if it matches the entire word
		if (ImToUpper(*pLineBegin) == FirstWordChar)
		{
			const char* pWordCursor = pWordBegin + 1;
			const char* pLineCursor = nullptr;
			
			// We iterate the length of our word and break if any character does not match
			for (pLineCursor = pLineBegin + 1; pWordCursor < pWordEnd; pLineCursor++, pWordCursor++)
			{
				if (ImToUpper(*pLineCursor) != ImToUpper(*pWordCursor))
					break;
			}
			
			// NOTE(matiasp): This is quite expensive to do for line
			// If we reached the end of the word it means that the entire word is equal
			if (pWordCursor == pWordEnd)
			{
				pFiltredLineMatch->vLineMatches.push_back(
					HighlightLineMatchEntry((uint8_t)FilterIdx, 
					                        (uint16_t)(pLineBegin - pLineStart), 
					                        (uint16_t)((pLineCursor - 1) - pLineStart)));
			}
		}
		
		pLineBegin++;
	}
}

#undef ISSUES_URL
#undef BINARIES_URL
#undef GIT_URL
#undef FILTERS_FILE_NAME
#undef SETTINGS_NAME
#undef FILTER_INTERVAL
#undef FILE_FETCH_INTERVAL
#undef FILE_FETCH_INTERVALERVAL
#undef FILE_FETCH_INTERVAL
#undef FOLDER_FETCH_INTERVAL
#undef CONSOLAS_FONT_SIZE
#undef MAX_EXTRA_THREADS
#undef SAVE_ENABLE_MASK
#undef MAX_REMEMBER_PATHS