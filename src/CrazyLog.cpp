#include <thread>

#include "CrazyLog.h"
#include "StringUtils.h"
#include "CrazyTextFilter.h"

#include "ConsolaTTF.cpp"

#define RING_BUFFER_BACKWARDS(idx, vBuffer) (idx - 1 != -1 ? idx - 1 : vBuffer.Size - 1)
#define BINARIES_URL "https://github.com/matiasjpedro/CrazyTown/releases"
#define VERSION_URL "https://raw.githubusercontent.com/matiasjpedro/CrazyTown/main/VERSION.txt"
#define GIT_URL "https://github.com/matiasjpedro/CrazyTown"
#define ISSUES_URL "https://github.com/matiasjpedro/CrazyTown/issues"
#define FILTERS_FILE_NAME "FILTERS.json"
#define SETTINGS_NAME "SETTINGS.json"
#define VERSION_FILE_NAME "VERSION.txt"
#define FILE_FETCH_INTERVAL 0.5f
#define FOLDER_FETCH_INTERVAL 2.f
#define CONSOLAS_FONT_SIZE 14 
#define MAX_EXTRA_THREADS 31
#define MAX_REMEMBER_PATHS 5

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define clamp(v, mx, mn) (v < mn) ? mn : (v > mx) ? mx : v; 

#define SAVE_ENABLE_MASK 0

static float g_Version = 1.32f;
static char g_NullTerminator = '\0';
static char g_LineEndTerminator = '\n';

void CrazyLog::GetVersions(PlatformContext* pPlatformCtx) 
{
	snprintf(aCurrentVersion, sizeof(aCurrentVersion), "%.2f", g_Version);
	
	FileContent NewVersionFile = { 0 };
	pPlatformCtx->pURLDownloadFileFunc(VERSION_URL, "VERSION_TEMP.TXT", &NewVersionFile);
	
	if (NewVersionFile.Size > 0)
		memcpy(aNewVersion, NewVersionFile.pFile, NewVersionFile.Size);
	
	pPlatformCtx->pFreeFileContentFunc(&NewVersionFile);
	
	bool bIsVersionNewer = strcmp(aCurrentVersion, aNewVersion) != 0;
	
	if (!bIsVersionNewer)
		memset(aNewVersion, 0, sizeof(aNewVersion));
}

void CrazyLog::Init(PlatformContext* pPlatformCtx)
{
	SelectedExtraThreadCount = 0;
	FontScale = 1.f;
	SelectionSize = 1.f;
	FileContentFetchCooldown = -1.f;
	FolderFetchCooldown = -1.f;
	PeekScrollValue = -1.f;
	FindScrollValue = -1.f;
	FiltredScrollValue = -1.f;
	EnableMask = 0xFFFFFFFF;
	ImGui::StyleColorsClassic();
	
    ImGuiStyle* style = &ImGui::GetStyle();
	style->Colors[ImGuiCol_MenuBarBg] = style->Colors[ImGuiCol_FrameBg];
	
	FileContentFetchSlider = FILE_FETCH_INTERVAL;
	MaxExtraThreadCount = max(0, std::thread::hardware_concurrency() - 1);
	
	// Lets enable it by default
	bIsMultithreadEnabled = true;
	SelectedExtraThreadCount = clamp(3, MaxExtraThreadCount, 0);
	
	for (unsigned i = 0; i < RITT_COUNT; ++i)
	{
		avRecentInputText[i].resize(MAX_REMEMBER_PATHS, { 0 });
		aRecentInputTextTail[i] = -1;
	}
	
	bIsAVXEnabled = true;
	
	GetVersions(pPlatformCtx);
	SetLastCommand("LAST COMMAND");
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
	ClearFindCache(false);
	
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
		
		for (unsigned i = 0; i < RITT_COUNT; ++i)
		{
			
			ImVector<RecentInputText>& vRecentInputText = avRecentInputText[i];
			int& RecentInputTextTail = aRecentInputTextTail[i];
			
			cJSON * pRecentInputTextArray = cJSON_GetObjectItemCaseSensitive(pJsonRoot, RememberInputTextSetting[i]);
			int RecentInputTextCount = cJSON_GetArraySize(pRecentInputTextArray);
			cJSON * pRecentInputText = nullptr;
			if (pRecentInputTextArray) {
				vRecentInputText.resize(RecentInputTextCount);
		
				int Counter = 0;
				cJSON_ArrayForEach(pRecentInputText, pRecentInputTextArray)
				{
					strcpy_s(vRecentInputText[Counter++].aText, sizeof(RecentInputText::aText),
							 pRecentInputText->valuestring);
				}
			}
			
			cJSON * pRecentInputTextsTail = cJSON_GetObjectItemCaseSensitive(pJsonRoot, RememberInputTextTailSetting[i]);
			if (pRecentInputTextsTail)
				RecentInputTextTail = (int)pRecentInputTextsTail->valuedouble;
			else if (RecentInputTextCount > 0)
				RecentInputTextTail = RecentInputTextCount -1;
		}
		
		
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

void CrazyLog::RememberInputText(PlatformContext* pPlatformCtx, RecentInputTextType Type, char* pText) {
	
	const char* pTailSettingName = RememberInputTextTailSetting[Type];;
	const char* pBufferSettingName = RememberInputTextSetting[Type];
	
	ImVector<RecentInputText>& vTargetRecentInputText = avRecentInputText[Type]; 
	int& Tail = aRecentInputTextTail[Type];
	
	int AlreadyExistIdx = -1;
	// Check if this path already exist in our buffer
	for (unsigned i = 0; i < (unsigned)vTargetRecentInputText.Size; i++)
	{
		if (vTargetRecentInputText[i].aText[0] == '\0')
			continue;
		
		if (strncmp(vTargetRecentInputText[i].aText, pText, sizeof(RecentInputText::aText)) == 0)
		{
			AlreadyExistIdx = i;
			break;
		}
	}
	
	// If it's already the latest one, then don't do nothing. 
	if (AlreadyExistIdx != -1 && AlreadyExistIdx == Tail)
		return;

	Tail = ++Tail % MAX_REMEMBER_PATHS;
	
	// If it already exist then offset the buffer by 1 idx starting from the located idx
	// by doing that we will be removing the older entry and bump the other entries 1 position.
	if (AlreadyExistIdx != -1)
	{
		for (int i = AlreadyExistIdx; i != Tail; i = RING_BUFFER_BACKWARDS(i, vTargetRecentInputText))
		{
			int PrevIdx = RING_BUFFER_BACKWARDS(i, vTargetRecentInputText);
			strcpy_s(vTargetRecentInputText[i].aText, sizeof(RecentInputText::aText), vTargetRecentInputText[PrevIdx].aText);
		}
	}
	
	// Then push the path to the head of the buffer
	strcpy_s(vTargetRecentInputText[Tail].aText, sizeof(RecentInputText::aText), pText);
	
	cJSON * pJsonPathsTail = cJSON_CreateNumber(Tail);
	
	FileContent OutFile = {0};
	
	FileContent File = pPlatformCtx->pReadFileFunc(SETTINGS_NAME);
	cJSON * pJsonRoot = nullptr;
	
	cJSON * pJsonRecentPathArray = cJSON_CreateArray();
		
	for (unsigned j = 0; j < (unsigned)vTargetRecentInputText.Size; ++j)
	{
		RecentInputText& Path = vTargetRecentInputText[j];
			
		cJSON * pFilePathValue = cJSON_CreateString(Path.aText);
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
		
		ClearFindCache(true);
		
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
	ClearFindCache(false);
	vFiltredLinesCached.reserve_discard(vLineOffsets.Size);
	vFindFullViewLinesCached.reserve_discard(vLineOffsets.Size);
	vFindFiltredLinesCached.reserve_discard(vLineOffsets.Size);
}

void CrazyLog::ClearFindCache(bool bOnlyFilter) {
	
	vFindFiltredLinesCached.clear();
	FindFiltredProccesedLinesCount = 0;
	CurrentFindFiltredIdx = 0;
	
	if (bOnlyFilter) return;
	
	vFindFullViewLinesCached.clear();
	FindFullViewProccesedLinesCount = 0;
	CurrentFindFullViewIdx = 0;
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
	ImGuiIO& Io = ImGui::GetIO();

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
	
	if (aNewVersion[0] != 0) {
		float buttonWidth = 200.0f; // The width of your button
		float availableWidth = ImGui::GetContentRegionAvail().x;

		// Set the cursor position so the button is aligned to the right
		ImGui::SetCursorPosX(availableWidth - buttonWidth);
		
		if (ImGui::Button("New Version available!", ImVec2(buttonWidth, 0))) {
			pPlatformCtx->pOpenURLFunc(BINARIES_URL);
		}
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
	
	DrawFind(DeltaTime, pPlatformCtx);
	
	if (!bIsShiftPressed && !bIsAltPressed && SelectionSize > 1.f) {
		SelectionSize = 0.f;
	}
	
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
	
	OutputTextLineHeight = ImGui::GetTextLineHeightWithSpacing();
	
	if (ImGui::BeginChild("Output", ImVec2(0, -25), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ExtraFlags))
	{
		if (bIsFindOpen) 
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (bIsCtrlressed && ImGui::IsKeyPressed(ImGuiKey_F))) 
			{
				bIsFindOpen = false;
				
				FindTextLen = 0;
				memset(aFindText, 0, sizeof(aFindText));
				ClearFindCache(false);
				
				SetLastCommand("FIND CLOSED");
			}
			
		} else {
			
			if (bIsCtrlressed && ImGui::IsKeyPressed(ImGuiKey_F)) {
				bIsFindOpen = true;
				SetLastCommand("FIND OPENED");
			}
		}
		
		if (bIsCtrlressed && Io.MouseWheel != 0.f)
		{
			if (ImGui::IsWindowHovered())
			{
				FontScale += Io.MouseWheel;
				bWantsToScaleFont = true;
				SetLastCommand("FONT SCALED");
			}
		}
		
		bool bWantsToAddFilter = false;
		bool bWantsToCopy = false;
		bool bWantsToPaste = false;

		if (bIsCtrlressed && !Io.WantTextInput)
		{
			if(ImGui::IsKeyReleased(ImGuiKey_C))
				bWantsToCopy = true;

			if(ImGui::IsKeyReleased(ImGuiKey_V))
				bWantsToPaste = true;

			if (ImGui::IsKeyReleased(ImGuiKey_A))
			{
				CurrentSelectionMode = SM_Normal;
				Selection.Start = { 0, 0 };
				if (vLineOffsets.size()) {
					size_t Column = Buf.end() - (Buf.begin() + vLineOffsets[vLineOffsets.size() - 1]);
					Selection.End = { vLineOffsets.size() - 1, (int)Column };
				} else {
					Selection.End = { 0, 0 };
				}
			}
		}

		if (ImGui::IsKeyReleased(ImGuiKey_F5))
		{
			LoadFile(pPlatformCtx);
			SetLastCommand("FILE REFRESHED");
		}
		
		bool bWantsToSnapScroll = false;
		if (!bIsAltPressed && ImGui::BeginPopupContextWindow())
		{
			if (Selection.Start != Selection.End && ImGui::Selectable("Add Filter")) 
				bWantsToAddFilter = true;

			if (ImGui::Selectable("Copy")) 
				bWantsToCopy = true;

			if (ImGui::Selectable("Paste")) 
				bWantsToPaste = true;

			//if (ImGui::Selectable("Clear")) 
			//	Clear();

			ImGui::Separator();

			ImGui::Checkbox("Show Line number", &bShowLineNum);
			if (ImGui::Checkbox("Auto-scroll", &bAutoScroll))
			{
				bWantsToSnapScroll = bAutoScroll;
			}

			ImGui::Separator();
		
			ImGui::EndPopup();
		}

		bool bWantsToExtractSelection = Selection.Start != Selection.End && (bWantsToCopy || bWantsToAddFilter);
		if (bWantsToExtractSelection) 
		{
			ImGuiTextBuffer CopyBuffer;
			if (!bIsPeeking && AnyFilterActive()) // Copy Filtred view 
			{
				const char* pSelectionStart = vLineOffsets.size() > Selection.Start.Line ? 
					Buf.begin() + vLineOffsets[Selection.Start.Line] + Selection.Start.Column : nullptr;

				const char* pSelectionEnd = vLineOffsets.size() > Selection.End.Line ? 
					Buf.begin() + vLineOffsets[Selection.End.Line] + Selection.End.Column : nullptr;

				for (int j = 0; j < vFiltredLinesCached.size(); j++) {
		
					int FilteredLineNo = vFiltredLinesCached[j];
					if (FilteredLineNo < Selection.Start.Line)
						continue;

					const char* pFilteredLineStart = Buf.begin() + vLineOffsets[FilteredLineNo];
					const char* pFilteredLineEnd = FilteredLineNo + 1 < vLineOffsets.Size ? (Buf.begin() + vLineOffsets[FilteredLineNo + 1] - 1) : Buf.end();

					const char* pStart = max(pSelectionStart, pFilteredLineStart);
					const char* pEnd = min(pSelectionEnd, pFilteredLineEnd);

					CopyBuffer.append(pStart, pEnd);
					if (pEnd != pSelectionEnd)
						CopyBuffer.append(&g_LineEndTerminator);

					if (pEnd == pSelectionEnd) {
						break;
					}
				}
			}
			else // Copy from full view
			{
				const char* pSelectionStart = vLineOffsets.size() > Selection.Start.Line ? Buf.begin() + vLineOffsets[Selection.Start.Line] + Selection.Start.Column : nullptr;
				const char* pSelectionEnd = vLineOffsets.size() > Selection.End.Line ? Buf.begin() + vLineOffsets[Selection.End.Line] + Selection.End.Column : nullptr;

				CopyBuffer.append(pSelectionStart, pSelectionEnd);
			}

			if (bWantsToAddFilter)
			{
				bool bCanFit = strlen(Filter.aInputBuf) + CopyBuffer.size() + 4 < sizeof(Filter.aInputBuf) ;
				if (bCanFit) {
					if(Filter.vFilters.size() > 0)
						strcat_s(Filter.aInputBuf, " || ");

					strcat_s(Filter.aInputBuf, CopyBuffer.begin());
					
					Filter.Build(&vDefaultColors);
					FilterSelectedIdx = 0;
					FiltredLinesCount = 0;
					bAlreadyCached = false;

					ClearFindCache(true);
				}
			}
			else if (bWantsToCopy)
			{
				ImGui::SetClipboardText(CopyBuffer.begin());
			}
		}
		else if (bWantsToCopy)
		{
			if (!bIsPeeking && AnyFilterActive()) // Copy Filtred view 
			{
				ImGuiTextBuffer CopyBuffer;
				for (int j = 0; j < vFiltredLinesCached.size(); j++) {
					int FilteredLineNo = vFiltredLinesCached[j];

					const char* pFilteredLineStart = Buf.begin() + vLineOffsets[FilteredLineNo];
					const char* pFilteredLineEnd = FilteredLineNo + 1 < vLineOffsets.Size ? (Buf.begin() + vLineOffsets[FilteredLineNo + 1] - 1) : Buf.end();

					CopyBuffer.append(pFilteredLineStart, pFilteredLineEnd);
					if (FilteredLineNo != vFiltredLinesCached.size() - 1)
						CopyBuffer.append(&g_LineEndTerminator);
				}

				ImGui::SetClipboardText(CopyBuffer.begin());

			}
			else // Copy from full view
			{
				ImGui::SetClipboardText(Buf.begin());
			}
		}

		if (bWantsToPaste)
		{
			LoadClipboard();
		}
		
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
		
		if (FindScrollValue > -1.f) {
			OneTimeScrollValue = FindScrollValue;
			FindScrollValue = -1.f;
		}
		
		if (!bAlreadyCached && AnyFilterActive()) {
			FilterLines(pPlatformCtx);
		}
		
		if (bIsFindOpen && FindTextLen > 0) {
			FindLines(pPlatformCtx);
		}
		
		if (!bIsPeeking && AnyFilterActive())
		{
			DrawFiltredView(pPlatformCtx);
		}
		else
		{
			DrawFullView(pPlatformCtx);
		}

		HandleMouseInputs();
		
		if (OneTimeScrollValue > -1.f) {
			ImGui::SetScrollY(OneTimeScrollValue);
		}
		
		// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
		// Using a scrollbar or mouse-wheel will take away from the bottom edge.
		bWantsToSnapScroll |= (bAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY());
		if (!bIsPeeking && bWantsToSnapScroll)
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

void CrazyLog::FindLines(PlatformContext* pPlatformCtx) 
{
	const char* pFindTextStart = aFindText;
	const char* pFindTextEnd = &aFindText[FindTextLen];
		
	const char* pBuf = Buf.begin();
	const char* pBufEnd = Buf.end();
	
	// TODO(Matiasp): Make this multithread + avx version of this.
	if (FindFullViewProccesedLinesCount < vLineOffsets.Size) {
		for (int LineNo = FindFullViewProccesedLinesCount; LineNo < vLineOffsets.Size; LineNo++)
		{
			const char* pLineStart = pBuf + vLineOffsets[LineNo];
			const char* pLineEnd = (LineNo + 1 < vLineOffsets.Size) ? (pBuf + vLineOffsets[LineNo + 1] - 1) : pBufEnd;
			if(ImStristr(pLineStart, pLineEnd, pFindTextStart, pFindTextEnd))
				vFindFullViewLinesCached.push_back(LineNo);
		}
		
		FindFullViewProccesedLinesCount = vLineOffsets.Size;
	}
	
	if (FindFiltredProccesedLinesCount < vFiltredLinesCached.Size) {
		for (int i = FindFiltredProccesedLinesCount; i < vFiltredLinesCached.Size; i++)
		{
			int LineNo = vFiltredLinesCached[i];
			
			const char* pLineStart = pBuf + vLineOffsets[LineNo];
			const char* pLineEnd = (LineNo + 1 < vLineOffsets.Size) ? (pBuf + vLineOffsets[LineNo + 1] - 1) : pBufEnd;
			if(ImStristr(pLineStart, pLineEnd, pFindTextStart, pFindTextEnd))
				vFindFiltredLinesCached.push_back(LineNo);
		}
		
		FindFiltredProccesedLinesCount = vFiltredLinesCached.Size;
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
	snprintf(aLastCommand, sizeof(aLastCommand), "ver %s - TotalLines %i ResultLines %i - LastCommand: %s",
	         aCurrentVersion, vLineOffsets.Size, vFiltredLinesCached.Size, pLastCommand);
}

void CrazyLog::HighlightLine(const char* pLineStart, const char* pLineEnd) 
{
	ImVec2 TextPos = ImGui::GetCursorScreenPos();
	ImVec2 TextSize = ImGui::CalcTextSize(pLineStart, pLineEnd);
	ImGui::GetWindowDrawList()->AddRectFilled(TextPos, ImVec2(TextPos.x + TextSize.x, TextPos.y + TextSize.y), 
											  IM_COL32(66, 66, 66, 255)); 
}

void CrazyLog::DrawFiltredView(PlatformContext* pPlatformCtx)
{
	bool bIsShiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	bool bIsCtrlressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
	bool bIsAltPressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);

	const char* pSelectionStart = vLineOffsets.size() > Selection.Start.Line ? Buf.begin() + vLineOffsets[Selection.Start.Line] + Selection.Start.Column : nullptr;
	const char* pSelectionEnd = vLineOffsets.size() > Selection.End.Line ? Buf.begin() + vLineOffsets[Selection.End.Line] + Selection.End.Column : nullptr;
	
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();
	ImGuiListClipper clipper;
	clipper.Begin(vFiltredLinesCached.Size);
	
	TempLineMatches.vLineMatches.reserve(20);
	char aLineNumberBuff[17] = { 0 };
	while (clipper.Step())
	{
		for (int ClipperIdx = clipper.DisplayStart; ClipperIdx < clipper.DisplayEnd; ClipperIdx++)
		{
			int line_no = vFiltredLinesCached[ClipperIdx];
			
			if (bShowLineNum) {
				snprintf(aLineNumberBuff, sizeof(aLineNumberBuff), "[%i] -", line_no);
				ImGui::Text(aLineNumberBuff);
				ImGui::SameLine();
			}
			
			const char* pLineStart = buf + vLineOffsets[line_no];
			const char* pLineEnd = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
			int64_t line_size = pLineEnd - pLineStart;
			
			bool bIsItemHovered = false;
			const char* pLineCursor = pLineStart;
			
			CacheHighlightLineMatches(pLineStart, pLineEnd, &TempLineMatches);
			
			if(vFindFiltredLinesCached.Size > 0 && line_no == vFindFiltredLinesCached[CurrentFindFiltredIdx])
				HighlightLine(pLineStart, pLineEnd);
			
			for (int j = 0; j < TempLineMatches.vLineMatches.Size; j++)
			{
				uint8_t FilterIdx = TempLineMatches.vLineMatches[j].FilterIdxMatching;
				ImVec4 FilterColor = FilterIdx != 255 ? Filter.vSettings[FilterIdx].Color : FindTextColor;
				
				const char* pHighlightWordBegin = pLineStart + TempLineMatches.vLineMatches[j].WordBeginOffset;
				const char* pHighlightWordEnd = pLineStart + TempLineMatches.vLineMatches[j].WordEndOffset + 1;

				if (pLineCursor <= pHighlightWordBegin) 
				{
					// Draw until the highlight begin
					if (pLineCursor != pHighlightWordBegin) // Valid Case, Previous filter could end at the new word begin
					{
						DrawColoredRangeAndSelection(pLineCursor, pHighlightWordBegin, ImVec4(), pSelectionStart, pSelectionEnd, bIsItemHovered);
						pLineCursor = pHighlightWordBegin;

						ImGui::SameLine(0.f,0.f);
					}
					
					// Draw highlight 
					DrawColoredRangeAndSelection(pHighlightWordBegin, pHighlightWordEnd, FilterColor, pSelectionStart, pSelectionEnd, bIsItemHovered);
					pLineCursor = pHighlightWordEnd;

					ImGui::SameLine(0.f,0.f);
				} 
				else if (pLineCursor < pHighlightWordEnd) // In case a previous filter have already highlighted the begin. 
				{
					// Draw highlight, 
					DrawColoredRangeAndSelection(pLineCursor, pHighlightWordEnd, FilterColor, pSelectionStart, pSelectionEnd, bIsItemHovered);
					pLineCursor = pHighlightWordEnd;

					ImGui::SameLine(0.f,0.f);

				}

			}

			// Draw after the highlights
			if (pLineCursor != pLineEnd) // Valid Case, we could have reached the end of the buffer.
				DrawColoredRangeAndSelection(pLineCursor, pLineEnd, ImVec4(), pSelectionStart, pSelectionEnd, bIsItemHovered);

			if (bIsItemHovered)
				MouseOverLineIdx = line_no;
	
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
			/*
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
				float LineNumberTextOffset = 0.f;
				if (bShowLineNum) {
					size_t aLineNumberLen = strlen(aLineNumberBuff) + 1;
					LineNumberTextOffset = ImGui::CalcTextSize(&aLineNumberBuff[0], &aLineNumberBuff[aLineNumberLen]).x;
				}
				
				SelectCharsFromLine(pPlatformCtx, pLineStart, pLineEnd, LineNumberTextOffset);
			}
			*/
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


// https://en.wikipedia.org/wiki/UTF-8
// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
static int UTF8CharLength(char c)
{
	if ((c & 0xFE) == 0xFC)
		return 6;
	if ((c & 0xFC) == 0xF8)
		return 5;
	if ((c & 0xF8) == 0xF0)
		return 4;
	else if ((c & 0xF0) == 0xE0)
		return 3;
	else if ((c & 0xE0) == 0xC0)
		return 2;
	return 1;
}

int CrazyLog::GetLineMaxColumn(int aLine)
{
	if (aLine >= vLineOffsets.size())
		return 0;

	const char* pLineStart = Buf.begin() + vLineOffsets[aLine];
	const char* pLineEnd = (aLine + 1 < vLineOffsets.Size) ? (Buf.begin() + vLineOffsets[aLine + 1] - 1) : Buf.end();
	size_t LineSize = pLineEnd - pLineStart;

	int col = 0;
	for (unsigned i = 0; i < LineSize; )
	{
		char c = pLineStart[i];
		//if (c == '\t')
		//	col = (col / mTabSize) * mTabSize + mTabSize;
		//else
			col++;
		i += UTF8CharLength(c);
	}
	return col;
}

Coordinates CrazyLog::SanitizeCoordinates(const Coordinates & aValue)
{
	int Line = aValue.Line;
	int Column = aValue.Column;
	if (Line >= (int)vLineOffsets.size())
	{
		if (vLineOffsets.empty())
		{
			Line = 0;
			Column = 0;
		}
		else
		{
			Line = (int)vLineOffsets.size() - 1;
			Column = GetLineMaxColumn(Line);
		}
		return Coordinates(Line, Column);
	}
	else
	{
		Column = vLineOffsets.empty() ? 0 : min(Column, GetLineMaxColumn(Line));
		return Coordinates(Line, Column);
	}
}

Coordinates CrazyLog::ScreenPosToCoordinates(const ImVec2& aPosition) {
	ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImVec2 Local(aPosition.x - Origin.x, aPosition.y - Origin.y);

	int LineNo = MouseOverLineIdx;
	int ColumnCoord = 0;
	
	if (LineNo >= 0 && LineNo < (int)vLineOffsets.size())
	{
		const char* pLineStart = Buf.begin() + vLineOffsets[LineNo];
		const char* pLineEnd = (LineNo + 1 < vLineOffsets.Size) ? (Buf.begin() + vLineOffsets[LineNo + 1] - 1) : Buf.end();
		size_t LineSize = pLineEnd - pLineStart;

		int ColumnIndex = 0;
		float ColumnX = 0.0f;

		while ((size_t)ColumnIndex < LineSize)
		{
			float ColumnWidth = 0.0f;

			//if (line_start[columnIndex] == '\t')
			//{
			//	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ").x;
			//	float oldX = columnX;
			//	float newColumnX = (1.0f + ImFloor((1.0f + columnX) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
			//	columnWidth = newColumnX - oldX;
			//	if (mTextStart + columnX + columnWidth * 0.5f > local.x)
			//		break;
			//	columnX = newColumnX;
			//	columnCoord = (columnCoord / mTabSize) * mTabSize + mTabSize;
			//	columnIndex++;
			//}
			//else
			{
				char TempBuff[7];
				int CharLen = UTF8CharLength(pLineStart[ColumnIndex]);
				int i = 0;
				while (i < 6 && CharLen-- > 0)
					TempBuff[i++] = pLineStart[ColumnIndex++];
				TempBuff[i] = '\0';
				ColumnWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, TempBuff).x;
				if (TextStart + ColumnX + ColumnWidth * 0.5f > Local.x)
					break;
				ColumnX += ColumnWidth;
				ColumnCoord++;
			}
		}
	}

	return SanitizeCoordinates(Coordinates(LineNo, ColumnCoord));
}

void CrazyLog::SetSelection(const Coordinates& aStart, const Coordinates& aEnd, SelectionMode aMode) 
{
	Coordinates OldSelStart = Selection.Start;
	Coordinates OldSelEnd = Selection.End;

	Selection.Start = SanitizeCoordinates(aStart);
	Selection.End = SanitizeCoordinates(aEnd);
	if (Selection.Start > Selection.End)
		ImSwap(Selection.Start, Selection.End);

	switch (aMode)
	{
		case SM_Normal:
			break;
		case SM_Word:
		{
			int LineNo = Selection.Start.Line;
			const char* pLineStart = Buf.begin() + vLineOffsets[LineNo];
			const char* pLineEnd = (LineNo + 1 < vLineOffsets.Size) ? (Buf.begin() + vLineOffsets[LineNo + 1] - 1) : Buf.end();

			char* pWordStart = (char*)&pLineStart[Selection.Start.Column];
			pWordStart = GetWordStart(pLineStart, pWordStart);

			char* pWordEnd = (char*)&pLineStart[Selection.Start.Column];
			pWordEnd = GetWordEnd(pLineEnd, pWordEnd, 1);

			Selection.Start = Coordinates(LineNo, (int)(pWordStart - pLineStart));
			Selection.End = Coordinates(LineNo, (int)(pWordEnd - pLineStart));
			
			break;
		}
		case SM_Line:
		{
			const int LineNo = Selection.End.Line;
			Selection.Start = Coordinates(Selection.Start.Line, 0);
			Selection.End = Coordinates(LineNo, GetLineMaxColumn(LineNo));
			break;
		}
		default:
			break;
	}
}



void CrazyLog::HandleMouseInputs() 
{
	ImGuiIO& Io = ImGui::GetIO();
	bool bCtrl =  Io.KeyCtrl;

	TextStart = 0;

	// TODO: support line number
	//char buf[16];
	//snprintf(buf, 16, " %d ", vLineOffsets.size());
	//mTextStart = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x /*+ mLeftMargin */;
	// const float fontSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;

	if (ImGui::IsWindowHovered() )
	{
		if (vLineOffsets.size() > 0)
		{
			bool bClick = ImGui::IsMouseClicked(0);
			bool bDoubleClick = ImGui::IsMouseDoubleClicked(0);
			double Time = ImGui::GetTime();
			bool bTripleClick = bClick && !bDoubleClick && (LastClick != -1.0f && (Time - LastClick) < Io.MouseDoubleClickTime);

			/*
				Left mouse button triple click
				*/

			if (bTripleClick)
			{
				if (!bCtrl)
				{
					Selection.CursorPosition = InteractiveStart = InteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					CurrentSelectionMode = SM_Line;
					SetSelection(InteractiveStart, InteractiveEnd, CurrentSelectionMode);
				}

				LastClick = -1.0f;
			}

			/*
				Left mouse button double click
				*/

			else if (bDoubleClick)
			{
				if (!bCtrl)
				{
					Selection.CursorPosition = InteractiveStart = InteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					if (CurrentSelectionMode == SM_Line)
						CurrentSelectionMode = SM_Normal;
					else
						CurrentSelectionMode = SM_Word;
					SetSelection(InteractiveStart, InteractiveEnd, CurrentSelectionMode);
				}

				LastClick = (float)ImGui::GetTime();
			}

			/*
				Left mouse button click
				*/
			else if (bClick)
			{
				if (!bCtrl)
				{
					Selection.CursorPosition = InteractiveStart = InteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					CurrentSelectionMode = SM_Normal;

					SetSelection(InteractiveStart, InteractiveEnd, CurrentSelectionMode);
				}

				LastClick = (float)ImGui::GetTime();
			}
			// Mouse left button dragging (=> update selection)
			else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0))
			{
				Io.WantCaptureMouse = true;
				Selection.CursorPosition = InteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				SetSelection(InteractiveStart, InteractiveEnd, CurrentSelectionMode);
			}
		}
		else 
		{
			Selection.Start = { 0, 0 };
			Selection.End = { 0, 0 };
		}
	}
}

// Draws Range until and after selection if a selection is found inside;
void CrazyLog::DrawColoredRangeAndSelection(const char* pRangeStart, const char* pRangeEnd, const ImVec4 RangeColor,
								  			const char* pSelectionStart, const char* pSelectionEnd,
								  			bool& bIsItemHovered)
{

	const char* pCursor = pRangeStart;

	if (RangeColor.w != 0)
		ImGui::PushStyleColor(ImGuiCol_Text, RangeColor);

	if (pSelectionStart == pSelectionEnd) {

		assert(pCursor != pRangeEnd);
		ImGui::TextUnformatted(pCursor, pRangeEnd);
		pCursor = pRangeEnd;

		bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
	}
	else
	{
		// Selection starts inside the range
		if (pSelectionStart >= pRangeStart && pSelectionStart < pRangeEnd) 
		{
			//assert(pRangeStart != pSelectionStart);
			if (pRangeStart != pSelectionStart) // VALID: The selection starts where the range starts 
			{
				ImGui::TextUnformatted(pRangeStart, pSelectionStart);
				pCursor = pSelectionStart;

				bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
				ImGui::SameLine(0.f,0.f);
			}

			assert(pCursor != min(pSelectionEnd, pRangeEnd));
			// Write Selection
			ImVec2 TextPos = ImGui::GetCursorScreenPos();
			ImVec2 TextSize = ImGui::CalcTextSize(pCursor, min(pSelectionEnd, pRangeEnd));
			ImGui::GetWindowDrawList()->AddRectFilled(TextPos, ImVec2(TextPos.x + TextSize.x, TextPos.y + TextSize.y), 
													IM_COL32(66, 66, 66, 255)); 
			ImGui::PushStyleColor(ImGuiCol_Text, SelectionTextColor);
			ImGui::TextUnformatted(pCursor, min(pSelectionEnd, pRangeEnd));
			ImGui::PopStyleColor();

			bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);

			pCursor = min(pSelectionEnd, pRangeEnd);

			//assert(pCursor != pRangeEnd);
			if (pCursor != pRangeEnd) // VALID: The selection ends where the range ends
			{ 
				ImGui::SameLine(0.f,0.f);
				ImGui::TextUnformatted(pCursor, pRangeEnd);
				pCursor = pRangeEnd;

				bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
			}
		}
		// Selection ends inside the range
		else if (pSelectionEnd > pRangeStart && pSelectionEnd <= pRangeEnd)
		{
			assert(pRangeStart != min(pSelectionEnd, pRangeEnd));
			// Write Selection
			ImVec2 TextPos = ImGui::GetCursorScreenPos();
			ImVec2 TextSize = ImGui::CalcTextSize(pRangeStart, min(pSelectionEnd, pRangeEnd));
			ImGui::GetWindowDrawList()->AddRectFilled(TextPos, ImVec2(TextPos.x + TextSize.x, TextPos.y + TextSize.y), 
													IM_COL32(66, 66, 66, 255)); 

			ImGui::PushStyleColor(ImGuiCol_Text, SelectionTextColor);
			ImGui::TextUnformatted(pRangeStart, min(pSelectionEnd, pRangeEnd));
			ImGui::PopStyleColor();

			bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);

			pCursor = min(pSelectionEnd, pRangeEnd);

			//assert(pCursor != pRangeEnd);
			if (pCursor != pRangeEnd) // VALID: The selection ends where the range ends
			{
				ImGui::SameLine(0.f,0.f);
				ImGui::TextUnformatted(pCursor, pRangeEnd);
				pCursor = pRangeEnd;

				bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
			}
			
		}
		// All the range is selected
		else if (pSelectionStart < pRangeStart && pSelectionEnd > pRangeEnd) 
		{
			assert(pRangeStart != min(pSelectionEnd, pRangeEnd));
			// Write Selection
			ImVec2 TextPos = ImGui::GetCursorScreenPos();
			ImVec2 TextSize = ImGui::CalcTextSize(pRangeStart, min(pSelectionEnd, pRangeEnd));
			ImGui::GetWindowDrawList()->AddRectFilled(TextPos, ImVec2(TextPos.x + TextSize.x, TextPos.y + TextSize.y), 
													IM_COL32(66, 66, 66, 255)); 

			ImGui::PushStyleColor(ImGuiCol_Text, SelectionTextColor);
			ImGui::TextUnformatted(pRangeStart, min(pSelectionEnd, pRangeEnd));
			pCursor = min(pSelectionEnd, pRangeEnd);
			ImGui::PopStyleColor();

			bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
		}
		// None of the range is selected
		else
		{
			assert(pCursor != pRangeEnd);
			ImGui::TextUnformatted(pCursor, pRangeEnd);
			pCursor = pRangeEnd;

			bIsItemHovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);

		}
	}

	if (RangeColor.w != 0)
		ImGui::PopStyleColor();
}

void CrazyLog::DrawFullView(PlatformContext* pPlatformCtx)
{
	bool bIsShiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift);
	bool bIsAltPressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);
	
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();

	const char* pSelectionStart = vLineOffsets.size() > Selection.Start.Line ? Buf.begin() + vLineOffsets[Selection.Start.Line] + Selection.Start.Column : nullptr;
	const char* pSelectionEnd = vLineOffsets.size() > Selection.End.Line ? Buf.begin() + vLineOffsets[Selection.End.Line] + Selection.End.Column : nullptr;
	
	ImGuiListClipper clipper;
	clipper.Begin(vLineOffsets.Size);

	TempLineMatches.vLineMatches.reserve(20);
	char aLineNumberBuff[17] = { 0 };
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			if (bShowLineNum) {
				snprintf(aLineNumberBuff, sizeof(aLineNumberBuff), "[%i] -", line_no);
				ImGui::Text(aLineNumberBuff);
				ImGui::SameLine();
			}
			
			const char* line_start = buf + vLineOffsets[line_no];
			const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
			
			bool bIsItemHovered = false;
			
			CacheHighlightLineMatches(line_start, line_end, &TempLineMatches);
			
			if(vFindFullViewLinesCached.Size > 0 && line_no == vFindFullViewLinesCached[CurrentFindFullViewIdx])
				HighlightLine(line_start, line_end);

			const char* pLineCursor = line_start;

			for (int i = 0; i < TempLineMatches.vLineMatches.Size; i++)
			{
				uint8_t FilterIdx = TempLineMatches.vLineMatches[i].FilterIdxMatching;
				ImVec4 FilterColor = FilterIdx != 255 ? Filter.vSettings[FilterIdx].Color : FindTextColor;
					
				const char* pHighlightWordBegin = line_start + TempLineMatches.vLineMatches[i].WordBeginOffset;
				const char* pHighlightWordEnd = line_start + TempLineMatches.vLineMatches[i].WordEndOffset + 1;

				// Draw until the world begin
				if (pLineCursor <= pHighlightWordBegin) 
				{

					// Draw until the highlight begin
					if (pLineCursor != pHighlightWordBegin) // Valid Case, Previous filter could end at the new word begin
					{
						DrawColoredRangeAndSelection(pLineCursor, pHighlightWordBegin, ImVec4(), pSelectionStart, pSelectionEnd, bIsItemHovered);
						pLineCursor = pHighlightWordBegin;

						ImGui::SameLine(0.f,0.f);
					}
					
					// Draw highlight 
					DrawColoredRangeAndSelection(pHighlightWordBegin, pHighlightWordEnd, FilterColor, pSelectionStart, pSelectionEnd, bIsItemHovered);
					pLineCursor = pHighlightWordEnd;

					ImGui::SameLine(0.f,0.f);
				}
				else if (pLineCursor < pHighlightWordEnd) // In case a previous filter have already highlighted the begin. 
				{
					// Draw highlight, 
					DrawColoredRangeAndSelection(pLineCursor, pHighlightWordEnd, FilterColor, pSelectionStart, pSelectionEnd, bIsItemHovered);
					pLineCursor = pHighlightWordEnd;

					ImGui::SameLine(0.f,0.f);

				}
			}
				
			// Draw after the highlights
			if (pLineCursor != line_end) // Valid Case, we could have reached the end of the buffer.
				DrawColoredRangeAndSelection(pLineCursor, line_end, ImVec4(), pSelectionStart, pSelectionEnd, bIsItemHovered);

			if (bIsItemHovered)
				MouseOverLineIdx = line_no;
					
			// Select lines full view
			/*
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
				float LineNumberTextOffset = 0.f;
				if (bShowLineNum) {
					size_t aLineNumberLen = strlen(aLineNumberBuff) + 1;
					LineNumberTextOffset = ImGui::CalcTextSize(&aLineNumberBuff[0], &aLineNumberBuff[aLineNumberLen]).x;
				}
				
				SelectCharsFromLine(pPlatformCtx, line_start, line_end, LineNumberTextOffset);
			}
			*/
					
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

			if (ImGui::MenuItem("Save As..", nullptr, nullptr, aFilePathToLoad[0] != 0 && vFiltredLinesCached.Size > 0))
			{
				char aSavePath[MAX_PATH] = { 0 };
				if (pPlatformCtx->pGetSaveFilePathFunc(aSavePath, sizeof(aSavePath)))
				{
					SaveFilteredView(pPlatformCtx, aSavePath);

					// strip the file name form the path, to open the path.
					size_t PathLen = strlen(aSavePath);
					for (size_t i = PathLen - 1; i > 0; i--)
					{
						if (aSavePath[i] == '\\') 
						{
							aSavePath[i + 1] = '\0';
							break;
						}
			
					}

					pPlatformCtx->pOpenURLFunc(aSavePath);
				}
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
							"[Ctrl+A]			 Will select all the text. \n"
				            "[Ctrl+MouseWheel]    Will scale the font. \n"
				            "[Ctrl+Click]         Will peek that filtered hovered line in the full view of the logs. \n"
				            "[MouseButtonBack]    Will go back from peeking into the filtered view. \n"
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
		
		if (ImGui::BeginMenu("About"))
		{
			ImGui::Text("@2023 Matias Pedro \nLicense: MIT");
			
			ImGui::Separator();
			
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

void CrazyLog::DrawFind(float DeltaTime, PlatformContext* pPlatformCtx) {
	
	static char aFindTextBuffer[ArrayCount(aFindText)] = { 0 };
	static bool bWasOpen = bIsFindOpen;
	static bool bShouldFocusWhenFindFinish = false;
	
	if (bIsFindOpen) {
		
		bool bIsLookingAtFullView = bIsPeeking || !AnyFilterActive();
		
		int& TargetFindIdx = bIsLookingAtFullView ? CurrentFindFullViewIdx : CurrentFindFiltredIdx;
		ImVector<int>& vTargetFindLinesCached = bIsLookingAtFullView ? vFindFullViewLinesCached : vFindFiltredLinesCached;
		
		// After the find is done (previous frame) focus on the first line 
		if (bShouldFocusWhenFindFinish) 
		{
			if (vTargetFindLinesCached.Size > 0) 
			{
				int LineNo = vTargetFindLinesCached[TargetFindIdx];
		
				int ItemOffsetY = bIsLookingAtFullView ? LineNo : vFiltredLinesCached.index_from_ptr(vFiltredLinesCached.find(LineNo));
				float ItemPosY = (float)(ItemOffsetY) * OutputTextLineHeight;
				FindScrollValue = ItemPosY;
			}
			
			bShouldFocusWhenFindFinish = false;
		}
		
		
		if (ImGui::Button("|##RecentFinds") && aRecentInputTextTail[RITT_Find] != -1) 
		{
			ImGui::OpenPopup("RecentFinds");
		}
		
		ImGui::SameLine();
		
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
            ImGui::SetTooltip("Open list with recent entries");
		
		if (!bWasOpen) 
			ImGui::SetKeyboardFocusHere();
		
		ImGui::SetNextItemWidth(200);
		if (ImGui::InputText("Find", aFindTextBuffer, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue)) 
		{
			memcpy(aFindText, aFindTextBuffer, sizeof(aFindTextBuffer));
			
			FindTextLen = (int)strlen(aFindText);
			FindScrollValue = -1.f;
			
			CurrentFindFullViewIdx = 0;
			FindFullViewProccesedLinesCount = 0;
			vFindFullViewLinesCached.resize(0);
			
			CurrentFindFiltredIdx = 0;
			FindFiltredProccesedLinesCount = 0;
			vFindFiltredLinesCached.resize(0);
			
			if (FindTextLen > 0) {
				bShouldFocusWhenFindFinish = true;
				RememberInputText(pPlatformCtx, RITT_Find, aFindText);
			}
		}
		
		ImGui::SameLine();
		if (ImGui::Button("<") && vTargetFindLinesCached.Size > 0) 
		{
			TargetFindIdx = TargetFindIdx > 0 ? TargetFindIdx - 1 : vTargetFindLinesCached.Size - 1;
				
			int LineNo = vTargetFindLinesCached[TargetFindIdx];
			
			int ItemOffsetY = bIsLookingAtFullView ? LineNo : vFiltredLinesCached.index_from_ptr(vFiltredLinesCached.find(LineNo));
			float ItemPosY = (float)(ItemOffsetY) * OutputTextLineHeight;
			FindScrollValue = ItemPosY;
		}
		
		ImGui::SameLine();
		if (ImGui::Button(">") && vTargetFindLinesCached.Size > 0) 
		{
			TargetFindIdx = TargetFindIdx < vTargetFindLinesCached.Size - 1 ? TargetFindIdx + 1 : 0;
				
			int LineNo = vTargetFindLinesCached[TargetFindIdx];
		
			int ItemOffsetY = bIsLookingAtFullView ? LineNo : vFiltredLinesCached.index_from_ptr(vFiltredLinesCached.find(LineNo));
			float ItemPosY = (float)(ItemOffsetY) * OutputTextLineHeight;
			FindScrollValue = ItemPosY;
		}

		ImGui::SameLine();
		
		ImGui::SetNextItemWidth(-200);
		ImGui::Text("%i/%i", vTargetFindLinesCached.Size == 0 ? 0 : TargetFindIdx + 1, vTargetFindLinesCached.Size);

		ImGui::SameLine();
		if (ImGui::Button("X"))
			bIsFindOpen = false;

		ImGui::Dummy({0,0});
		
		ImVec2 InputTextPos = ImGui::GetCurrentWindowRead()->DC.CursorPos; 
		ImGui::SetNextWindowPos(InputTextPos);
		if (ImGui::BeginPopup("RecentFinds"))
		{
			bool bFirstDisplay = true;
			ImVector<RecentInputText>& vRecentInputText = avRecentInputText[RITT_Find];
			int& RecentInputTextTail = aRecentInputTextTail[RITT_Find];
			for (int i = RecentInputTextTail; i != RecentInputTextTail || bFirstDisplay; i = RING_BUFFER_BACKWARDS(i, vRecentInputText))
			{
				if (vRecentInputText[i].aText[0] == '\0')
					break;
					
				bFirstDisplay = false;
					
				if (ImGui::MenuItem(vRecentInputText[i].aText))
				{
					memcpy(aFindTextBuffer, vRecentInputText[i].aText, sizeof(RecentInputText::aText));
					memcpy(aFindText, vRecentInputText[i].aText, sizeof(RecentInputText::aText));
					
					FindTextLen = (int)strlen(aFindText);
					FindScrollValue = -1.f;
			
					CurrentFindFullViewIdx = 0;
					FindFullViewProccesedLinesCount = 0;
					vFindFullViewLinesCached.resize(0);
			
					CurrentFindFiltredIdx = 0;
					FindFiltredProccesedLinesCount = 0;
					vFindFiltredLinesCached.resize(0);
			
					if(FindTextLen > 0)
						bShouldFocusWhenFindFinish = true;
						
					ImGui::CloseCurrentPopup();
				}
			}
			
			ImGui::EndPopup();
		}
	}
	
	bWasOpen = bIsFindOpen;
}

void CrazyLog::DrawTarget(float DeltaTime, PlatformContext* pPlatformCtx)
{
	static TargetModeChangeReason PendingModeChange = TMCR_NONE;
	if (PendingModeChange != TMCR_NONE) {
		LastChangeReason = PendingModeChange;
		PendingModeChange = TMCR_NONE;
	}
	
	ImGui::SeparatorText("Target");
	
	ImGui::SetNextItemWidth(-120);
	if(ImGui::Combo("TargetMode", &(int)SelectedTargetMode, apTargetModeStr, IM_ARRAYSIZE(apTargetModeStr)))
		LastChangeReason = TMCR_NewModeSelected;
	
	ImGui::SameLine();
	ImGui::Dummy({-1,0});
	ImGui::SameLine();
	if (ImGui::Button(">>>")) {
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
		
		if (ImGui::Button("|") && aRecentInputTextTail[RITT_StreamPath] != -1) 
		{
			ImGui::OpenPopup("RecentStreamPaths");
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
            ImGui::SetTooltip("Open list with recent entries.");
		
		ImGui::SameLine();
		
		ImGui::SetNextItemWidth(-120);
		if (ImGui::InputTextWithHint("FolderQuery", "Pick a folder using the \"...\" button or insert the query manually for finer control, ex: D:\\logs\\*.txt ", aFolderQueryName, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue) 
			|| bLoadTriggerExternally)
		{
			memset(&LastLoadedFileData, 0, sizeof(LastLoadedFileData));
			
			bStreamFileLocked = false;
			bFolderQuery = true;
			SearchLatestFile(pPlatformCtx);
			
			if (aFolderQueryName[0] != 0) 
				RememberInputText(pPlatformCtx, RITT_StreamPath, aFolderQueryName);
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
	
		//ImGui::SameLine();
		//HelpMarker("Loads the last written file that matches the query \n"
		//           "and start streaming it into the output. \n");

		ImGui::SameLine();
		if (ImGui::Button("...")) 
		{
			if (pPlatformCtx->pPickFolderFunc(aFolderQueryName, sizeof(aFolderQueryName)))
			{
				memset(&LastLoadedFileData, 0, sizeof(LastLoadedFileData));
			
				bStreamFileLocked = false;
				bFolderQuery = true;

				strcat_s(aFolderQueryName, "\\*.*");

				SearchLatestFile(pPlatformCtx);
				RememberInputText(pPlatformCtx, RITT_StreamPath, aFolderQueryName);
			}
		}
		
		ImVec2 InputTextPos = ImGui::GetCurrentWindowRead()->DC.CursorPos;
		ImGui::SetNextWindowPos(InputTextPos);
		if (ImGui::BeginPopup("RecentStreamPaths"))
		{
			bool bFirstDisplay = true;
			ImVector<RecentInputText>& vRecentStreamPaths = avRecentInputText[RITT_StreamPath];
			int& StreamPathsTail = aRecentInputTextTail[RITT_StreamPath];
			for (int i = StreamPathsTail; i != StreamPathsTail || bFirstDisplay; i = RING_BUFFER_BACKWARDS(i, vRecentStreamPaths))
			{
				if (vRecentStreamPaths[i].aText[0] == '\0')
					break;
					
				bFirstDisplay = false;
					
				if (ImGui::MenuItem(vRecentStreamPaths[i].aText))
				{
					memcpy(aFolderQueryName, vRecentStreamPaths[i].aText, sizeof(RecentInputText::aText));
						
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
			ImGui::Text("Streaming file: [%s]", StringUtils::GetPathPastLastSlash(aFilePathToLoad));
		}
	}
	else if (SelectedTargetMode == TM_StaticText)
	{
		if (ImGui::Button("|") && aRecentInputTextTail[RITT_FilePath] != -1) 
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
		
		ImGui::SetNextItemWidth(-120);
		const char * pHint = "Pick a file using the \"...\" button, or drag and drop the file.";  
		if (ImGui::InputTextWithHint("FilePath", pHint, aFilePathToLoad, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue) 
			|| bLoadTriggerExternally)
		{
			if (aFilePathToLoad[0] != 0) {
				LoadFile(pPlatformCtx);
				RememberInputText(pPlatformCtx, RITT_FilePath, aFilePathToLoad);
			}
		}
		
		ImGui::SameLine();
		ImGui::Dummy({13,0});
		ImGui::SameLine();
		if (ImGui::Button("...")) 
		{
			if (pPlatformCtx->pPickFileFunc(aFilePathToLoad, sizeof(aFilePathToLoad)))
			{
				LoadFile(pPlatformCtx);
				RememberInputText(pPlatformCtx, RITT_FilePath, aFilePathToLoad);
			}
		}

		ImVec2 InputTextPos = ImGui::GetCurrentWindowRead()->DC.CursorPos;
		ImGui::SetNextWindowPos(InputTextPos);
		if (ImGui::BeginPopup("RecentFilePaths"))
		{
			bool bFirstDisplay = true;
			ImVector<RecentInputText>& vRecentFilePaths = avRecentInputText[RITT_FilePath];
			int& FilePathsTail = aRecentInputTextTail[RITT_FilePath];
			for (int i = FilePathsTail; i != FilePathsTail || bFirstDisplay; i = RING_BUFFER_BACKWARDS(i, vRecentFilePaths))
			{
				if (vRecentFilePaths[i].aText[0] == '\0')
					break;
					
				bFirstDisplay = false;
					
				if (ImGui::MenuItem(vRecentFilePaths[i].aText))
				{
					memcpy(aFilePathToLoad, vRecentFilePaths[i].aText, sizeof(RecentInputText::aText));
						
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
	
	if (ImGui::Button("|##RecentFilters") && aRecentInputTextTail[RITT_Filter] != -1) 
	{
		ImGui::OpenPopup("RecentFilters");
	}
	
	ImGui::SameLine();
	
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
		ImGui::SetTooltip("Open list with recent entries.");
	
	bFilterChanged = Filter.Draw(&vDefaultColors, "Filter ", -110.0f);
	if (bFilterChanged && Filter.aInputBuf[0] != 0) {
		RememberInputText(pPlatformCtx, RITT_Filter, Filter.aInputBuf);
	}
	
	
	
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
	
	ImVec2 InputTextPos = ImGui::GetCurrentWindowRead()->DC.CursorPos;
	ImGui::SetNextWindowPos(InputTextPos);
	if (ImGui::BeginPopup("RecentFilters"))
	{
		bool bFirstDisplay = true;
		ImVector<RecentInputText>& vRecentInputText = avRecentInputText[RITT_Filter];
		int& RecentInputTextTail = aRecentInputTextTail[RITT_Filter];
		for (int i = RecentInputTextTail; i != RecentInputTextTail || bFirstDisplay; i = RING_BUFFER_BACKWARDS(i, vRecentInputText))
		{
			if (vRecentInputText[i].aText[0] == '\0')
				break;
					
			bFirstDisplay = false;
					
			if (ImGui::MenuItem(vRecentInputText[i].aText))
			{
				memcpy(Filter.aInputBuf, vRecentInputText[i].aText, sizeof(RecentInputText::aText));
				Filter.Build(&vDefaultColors);
					
				bFilterChanged = true;
			
				ImGui::CloseCurrentPopup();
			}
		}
			
		ImGui::EndPopup();
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
		
		ClearFindCache(true);
			
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
			
			ClearFindCache(true);
			
			SetLastCommand("SELECTED PRESET CHANGED");
		}
		
		if (bCherryPickHasChanged)
		{
			bAlreadyCached = false;
			FiltredLinesCount = 0;
			
			ClearFindCache(true);
			
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

void CrazyLog::SelectCharsFromLine(PlatformContext* pPlatformCtx, const char* pLineStart, const char* pLineEnd, float xOffset)
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
	float TextOffset = ImGui::GetCursorScreenPos().x + xOffset;
	
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
					
					ClearFindCache(true);
					
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
					
					ClearFindCache(true);
					
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

		const char* pWordBegin = &Filter.aInputBuf[f.BeginOffset];
		const char* pWordEnd = &Filter.aInputBuf[f.EndOffset];
		
		CacheHighlightMatchingWord(pLineBegin, pLineEnd, pWordBegin, pWordEnd, i, pFiltredLineMatch);
	}
	
	if (FindTextLen > 0) {
		CacheHighlightMatchingWord(pLineBegin, pLineEnd, aFindText, aFindText + FindTextLen, -1, pFiltredLineMatch);
	}

	if (pFiltredLineMatch->vLineMatches.Size > 0)
		qsort(pFiltredLineMatch->vLineMatches.Data, pFiltredLineMatch->vLineMatches.Size, sizeof(HighlightLineMatchEntry), HighlightLineMatchEntry::SortFunc);
}

void CrazyLog::CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, 
										  const char* pWordBegin, const char* pWordEnd, 
										  int FilterIdx, HighlightLineMatches* pFiltredLineMatch)
{
	
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