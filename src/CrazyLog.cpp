#include "CrazyLog.h"
#include "StringUtils.h"
#include "CrazyTextFilter.h"

#include "ConsolaTTF.cpp"

#define FILTERS_FILE_NAME "FILTERS"
#define FOLDER_QUERY_NAME "QUERIES"
#define FILTER_TOKEN ';'
#define FILE_FETCH_INTERVAL 1.f
#define FOLDER_FETCH_INTERVAL 2.f
#define CONSOLAS_FONT_SIZE 14 

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

static char g_FilterToken = ';';
static char g_NullTerminator = '\0';

void CrazyLog::Init()
{
	FontScale = 1.f;
	SelectionSize = 1.f;
	FileContentFetchCooldown = -1.f;
	FolderFetchCooldown = -1.f;
	PeekScrollValue = -1.f;
	FiltredScrollValue = -1.f;
	FilterFlags = 0xFFFFFFFF;
	SetLastCommand("LAST COMMAND");
	ImGui::StyleColorsClassic();
    ImGuiStyle& style = ImGui::GetStyle();
	style.FrameRounding = 6.f;
	style.GrabRounding = 6.f;
	FileContentFetchSlider = FILE_FETCH_INTERVAL;
}

void CrazyLog::Clear()
{
	bIsPeeking = false;
	bFileLoaded = false;
	bWantsToSavePreset = false;
	
	Buf.clear();
	vLineOffsets.clear();
	vLineOffsets.push_back(0);
	vHighlightLineMatches.clear_destruct();
	vHighlightLineMatches.push_back(HighlightLineMatches());

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

void CrazyLog::LoadFilter(PlatformContext* pPlatformCtx)
{	
	const char* NoneFilterName = "NONE";
	NamedFilter NoneFilter = { 0 };
	strcpy_s(NoneFilter.aName, sizeof(NoneFilter.aName), NoneFilterName);
	
	FileContent File = pPlatformCtx->pReadFileFunc(FILTERS_FILE_NAME);
	if (File.pFile) 
	{
		// TODO(matiasp): Store this at the beggining of the file
		LoadedFilters.reserve_discard(10);
		LoadedFilters.resize(0);
		
		//Add dummy so we can reset the selection
		LoadedFilters.push_back(NoneFilter);
		
		unsigned TokenCounter = 2;
		unsigned CurrentStringSize = 0;
		for (unsigned i = 0; i < File.Size; i++)
		{
			bool bIsIteratingName = TokenCounter % 2 == 0;
			char* pCurrentChar = &((char*)File.pFile)[i];
		
			if (*pCurrentChar == FILTER_TOKEN)
			{
				if (bIsIteratingName) 
				{	
					char* pNameBegin = pCurrentChar - CurrentStringSize;
					
					int FilterIdx = TokenCounter / 2;
					
					LoadedFilters.resize(FilterIdx + 1);
					memcpy(LoadedFilters[FilterIdx].aName, pNameBegin, CurrentStringSize);
					LoadedFilters[FilterIdx].aName[CurrentStringSize] = '\0';
					
					CurrentStringSize = 0;
				}
				else 
				{
					char* pFilterBegin = pCurrentChar - CurrentStringSize;
					
					int FilterIdx = TokenCounter / 2;
					
					memcpy(LoadedFilters[FilterIdx].Filter.InputBuf, pFilterBegin, CurrentStringSize);
					LoadedFilters[FilterIdx].Filter.InputBuf[CurrentStringSize] = '\0';
					
					CurrentStringSize = 0;
				}
			
				TokenCounter++;
			}
			else
			{
				CurrentStringSize++;
			}
		}
	
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
	OutFile.pFile = pPlatformCtx->ScratchMem.Back(); 
	size_t PreviousSize = (size_t)pPlatformCtx->ScratchMem.Size;

	// Start from 1 to skip the NONE filter
	for (unsigned i = 1; i < (unsigned)LoadedFilters.size(); ++i)
	{
		size_t LoadedFilterNameLen = StringUtils::Length(LoadedFilters[i].aName);
		pPlatformCtx->ScratchMem.PushBack(LoadedFilters[i].aName, LoadedFilterNameLen);
		
		// Add the Token Separator
		pPlatformCtx->ScratchMem.PushBack(&g_FilterToken, 1);
		
		// Copy the filter
		size_t LoadedFilterContentLen = StringUtils::Length(LoadedFilters[i].Filter.InputBuf);
		pPlatformCtx->ScratchMem.PushBack(LoadedFilters[i].Filter.InputBuf, LoadedFilterContentLen);
		
		// Add the Token Separator
		pPlatformCtx->ScratchMem.PushBack(&g_FilterToken, 1);
	}
	
	OutFile.Size = (size_t)pPlatformCtx->ScratchMem.Size - PreviousSize;
	pPlatformCtx->pWriteFileFunc(&OutFile, FILTERS_FILE_NAME);
}

void CrazyLog::SaveFolderQuery(PlatformContext* pPlatformCtx)
{
	FileContent OutFile = {0};
	OutFile.pFile = pPlatformCtx->ScratchMem.Back(); 

	size_t Len = StringUtils::Length(aFolderQueryName);
	if (Len > 0)
	{
		pPlatformCtx->ScratchMem.PushBack(aFolderQueryName, Len);
		OutFile.Size = Len;
		pPlatformCtx->pWriteFileFunc(&OutFile, FOLDER_QUERY_NAME);
	}
}

void CrazyLog::LoadFolderQuery(PlatformContext* pPlatformCtx)
{
	FileContent File = pPlatformCtx->pReadFileFunc(FOLDER_QUERY_NAME);
	if (File.Size > 0)
	{
		memcpy(aFolderQueryName, File.pFile, File.Size);
	}
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

void CrazyLog::SaveFilter(PlatformContext* pPlatformCtx, char* pFilterName, char* pFilterContent) 
{
	size_t FilterNameLen = StringUtils::Length(pFilterName);
	size_t FilterContentLen = StringUtils::Length(pFilterContent);
	
	LoadedFilters.resize(LoadedFilters.size() + 1);
	memcpy(LoadedFilters[LoadedFilters.size() - 1].aName, pFilterName, FilterNameLen+1);
	memcpy(LoadedFilters[LoadedFilters.size() - 1].Filter.InputBuf, pFilterContent, FilterContentLen+1);
	
	SaveLoadedFilters(pPlatformCtx);
	FilterSelectedIdx = LoadedFilters.size() - 1;
	
	SetLastCommand("FILTER SAVED");
}

// This method will append to the buffer
void CrazyLog::AddLog(const char* pFileContent, int FileSize) 
{
	int old_size = Buf.size();
	int NewRequiredCapacity = Buf.Buf.Size + FileSize;
	int GrowthCapacity = Buf.Buf._grow_capacity(Buf.Buf.Size + FileSize);

	Buf.Buf.reserve(GrowthCapacity);
	Buf.append(pFileContent, pFileContent + FileSize);
	
	for (int new_size = Buf.size(); old_size < new_size; old_size++)
	{
		if (Buf[old_size] == '\n')
		{
			vLineOffsets.push_back(old_size + 1);
			vHighlightLineMatches.push_back(HighlightLineMatches());
		}
	}
	
	bAlreadyCached = false;
}

// This method will stomp the old buffer;
void CrazyLog::SetLog(const char* pFileContent, int FileSize) 
{
	Buf.Buf.clear();
	vLineOffsets.clear();
	vHighlightLineMatches.clear_destruct();
	
	Buf.append(pFileContent, pFileContent + FileSize);
	vLineOffsets.push_back(0);
	vHighlightLineMatches.push_back(HighlightLineMatches());
	
	int old_size = 0;
	for (int new_size = Buf.size(); old_size < new_size; old_size++)
	{
		if (Buf[old_size] == '\n')
		{
			vLineOffsets.push_back(old_size + 1);
			vHighlightLineMatches.push_back(HighlightLineMatches());
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
	
	if (!ImGui::Begin(title, pOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ExtraFlags))
	{
		ImGui::End();
		return;
	}
	
	//=============================================================
	// Target
	
	DrawTarget(DeltaTime, pPlatformCtx);
	
	//=============================================================
	// Filters 
	
	ImGui::SeparatorText("Filters");
	
	bool bFilterChanged = CustomDrawFilter("Filter", -160.0f);
	ImGui::SameLine();
	HelpMarker(	"Filter usage:\n\n"
	           "  \"\"           display all lines\n"
	           "  \"xxx\"        display lines containing \"xxx\"\n"
	           "  \"xxx,yyy\"    display lines containing \"xxx\" || \"yyy\"\n"
	           "  \"xxx,+zzz\"   display lines containing  \"xxx\" && \"zzz\"\n"
	           "  \"-xxx\"       hide lines containing \"xxx\"");
	
	
	// If the size of the filters changed, make sure to start with those filters enabled.
	if (LastFrameFiltersCount < Filter.vFilters.Size) 
	{
		for (int i = LastFrameFiltersCount; i < Filter.vFilters.Size; i++) 
		{
			FilterFlags |= 1ull << i;
		}
	}
	
	LastFrameFiltersCount = Filter.vFilters.Size;

	size_t FilterLen = StringUtils::Length(Filter.aInputBuf);
	
	ImGui::SameLine();
	if (ImGui::SmallButton("Save") && FilterLen != 0) {
		memset(aFilterNameToSave, 0, ArrayCount(aFilterNameToSave));
		bWantsToSavePreset = true;
	}

	if (bWantsToSavePreset) 
	{
		ImGui::SetNextItemWidth(-160);
		bool bAcceptPresetName = ImGui::InputText("PresetName", aFilterNameToSave, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue);
		
		size_t FilterNameLen = StringUtils::Length(aFilterNameToSave);
		if ((ImGui::SmallButton("Accept")  || bAcceptPresetName) && FilterNameLen) 
		{
			SaveFilter(pPlatformCtx, aFilterNameToSave, Filter.aInputBuf);
			bWantsToSavePreset = false;
		}
	
		ImGui::SameLine();
		if (ImGui::SmallButton("Cancel")) 
			bWantsToSavePreset = false;
	}
	
	bool bSelectedFilterChanged = false;
	if (!bWantsToSavePreset) 
	{
		bSelectedFilterChanged = DrawPresets(DeltaTime, pPlatformCtx);
	}

	bool bCherryPickHasChanged = false;
	if (Filter.IsActive() ) 
	{
		bCherryPickHasChanged = DrawCherrypick(DeltaTime, pPlatformCtx);
		
		if (bFilterChanged) 
		{
			FilterSelectedIdx = 0;
			
			bAlreadyCached = false;
			FiltredLinesCount = 0;
			
			SetLastCommand("FILTER CHANGED");
		} 
		
		if (bSelectedFilterChanged) 
		{
			FilterFlags = 0xFFFFFFFF;
			
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
	
	ImGui::SeparatorText("OUTPUT (?)");
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
	{
		ImGui::SetTooltip("KEYBINDS when hovering the output view: \n\n"
		                  "[F5]                 Will refresh the loaded file. If new content is available it will append it. \n"
		                  "[Ctrl+C]             Will copy the content of the output to the clipboard. \n"
		                  "[Ctrl+V]             Will copy the clipboard into the output view. \n"
		                  "[Ctrl+MouseWheel]    Will scale the font. \n"
		                  "[Ctrl+Click]         Will peek that filtered hovered line in the full view of the logs. \n"
		                  "[MouseButtonBack]    Will go back from peeking into the filtered view. \n"
		                  "[Alt]                Will enter in word selection mode when hovering a word. \n"
		                  "[Shift]              Will enter in line selection mode when hovering a line. \n"
		                  "[MouseWheel]         While in word/line selection mode it will expand/shrink the selection. \n"
						  "[MouseMiddleClick]   While in word/line selection mode it will copy the selection to the clipboard. \n"
		                  "[MouseRightClick]    Will open the context menu with some options. \n");
	}
	
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
		
		if (ImGui::BeginPopupContextWindow())
		{
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
		
			if (ImGui::BeginMenu("Options"))
			{
				ImGui::Checkbox("Auto-scroll", &bAutoScroll);
				ImGui::EndMenu();
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
			
			if (ImGui::IsKeyReleased(ImGuiKey_MouseX1) 
				|| bFilterChanged 
				|| bSelectedFilterChanged 
				|| bCherryPickHasChanged) 
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

void CrazyLog::FilterLines(PlatformContext* pPlatformCtx)
{
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();
	
	if (FiltredLinesCount == 0)
	{
		vFiltredLinesCached.resize(0);
	}
				
	for (int line_no = FiltredLinesCount; line_no < vLineOffsets.Size; line_no++)
	{
		const char* line_start = buf + vLineOffsets[line_no];
		const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
		if (Filter.PassFilter(FilterFlags, line_start, line_end)) 
		{
			vFiltredLinesCached.push_back(line_no);
		}
		
		CacheHighlightLineMatches(line_start, line_end, &vHighlightLineMatches[line_no]);
	}
				
	FiltredLinesCount = vLineOffsets.Size;
	bAlreadyCached = true;
}

void CrazyLog::SetLastCommand(const char* pLastCommand)
{
	strcpy_s(aLastCommand, sizeof(aLastCommand), pLastCommand);
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
			
			for (int j = 0; j < vHighlightLineMatches[line_no].vLineMatches.Size; j++)
			{
				ImVec4 FilterColor = vFilterColor[vHighlightLineMatches[line_no].vLineMatches[j].FilterIdxMatching];
				
				const char* pHighlightWordBegin = buf + vHighlightLineMatches[line_no].vLineMatches[j].WordBeginOffset;
				const char* pHighlightWordEnd = buf + vHighlightLineMatches[line_no].vLineMatches[j].WordEndOffset + 1;
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
		
					int64_t Size = pFilteredLineEnd+1 - pFilteredLineStart;
					bWroteOnScratch |= pPlatformCtx->ScratchMem.PushBack(pFilteredLineStart, Size);
				}
	
				if (bWroteOnScratch) {
					pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1);
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
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			const char* line_start = buf + vLineOffsets[line_no];
			const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
			
			bool bIsItemHovered = false;
			bool bShouldCheckHover = bIsAltPressed || bIsShiftPressed;
					
			if (bIsPeeking && vHighlightLineMatches[line_no].vLineMatches.Size > 0)
			{
				const char* pLineCursor = line_start;
				
				for (int i = 0; i < vHighlightLineMatches[line_no].vLineMatches.Size; i++)
				{
					ImVec4 FilterColor = vFilterColor[vHighlightLineMatches[line_no].vLineMatches[i].FilterIdxMatching];
					
					const char* pHighlightWordBegin = buf + vHighlightLineMatches[line_no].vLineMatches[i].WordBeginOffset;
					const char* pHighlightWordEnd = buf + vHighlightLineMatches[line_no].vLineMatches[i].WordEndOffset + 1;
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
				int64_t Size = TopLineEnd - (buf + vLineOffsets[BottomLine]);
						
				char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
				if (pPlatformCtx->ScratchMem.PushBack(buf + vLineOffsets[BottomLine], Size) &&
					pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1))
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
	
	clipper.End();
}

void CrazyLog::DrawTarget(float DeltaTime, PlatformContext* pPlatformCtx)
{
	ImGui::SeparatorText("Target");
	
	ImGui::SetNextItemWidth(-160);
	bool bModeJustChanged = ImGui::Combo("TargetMode", &(int)SelectedTargetMode, apTargetModeStr, IM_ARRAYSIZE(apTargetModeStr));
	if (SelectedTargetMode == TM_StreamLastModifiedFileFromFolder)
	{
		if (bModeJustChanged)
		{
			bStreamMode = false;
			bFolderQuery = false;
			memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
		}
		
		ImGui::SetNextItemWidth(-160);
		if (ImGui::InputText("FolderQuery", aFolderQueryName, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			memset(&LastLoadedFileData, 0, sizeof(LastLoadedFileData));
			
			bFolderQuery = true;
			SearchLatestFile(pPlatformCtx);
			
			if(aFolderQueryName[0] != 0)
				SaveFolderQuery(pPlatformCtx);
		}
		else if (bFolderQuery)
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
		HelpMarker("Loads the last written file that matches the query. \n"
		           "and it will start streaming it into the output. \n"
		           "Example: D:\\logs\\*.txt \n");
		
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
		
		if(aFilePathToLoad[0] != 0)
			ImGui::Text("Streaming file: %s", aFilePathToLoad);
		
		ImGui::SetNextItemWidth(-160);
		ImGui::SliderFloat("StreamFrequency", &FileContentFetchSlider, 0.1f, 3.0f);
		
	}
	else if (SelectedTargetMode == TM_StaticText)
	{
		if (bModeJustChanged)
		{
			bStreamMode = false;
			bFolderQuery = false;
			memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
			memset(aFolderQueryName, 0, sizeof(aFolderQueryName));
		}
		
		ImGui::SetNextItemWidth(-160);
		if (ImGui::InputText("FilePath", aFilePathToLoad, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			LoadFile(pPlatformCtx);
		}
		
		ImGui::SameLine();
		HelpMarker("Full path of the file to load. \n"
				   "Example: D:\\logs\\file_name.ext \n\n"
		           "You can also drag and drop files.");
	}
	else if (SelectedTargetMode == TM_StreamFromWebSocket)
	{
		if (bModeJustChanged)
		{
			bStreamMode = false;
			bFolderQuery = false;
			memset(aFilePathToLoad, 0, sizeof(aFilePathToLoad));
			memset(aFolderQueryName, 0, sizeof(aFolderQueryName));
		}
	}
	
	
}

void CrazyLog::DrawFilter(float DeltaTime, PlatformContext* pPlatformCtx)
{
	
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
		
		ImGui::SetNextItemWidth(-160);
		bSelectedFilterChanged = ImGui::Combo("Presets", &FilterSelectedIdx, &Funcs::ItemGetter,
		                                      (void*)&LoadedFilters, LoadedFilters.Size);
		if (bSelectedFilterChanged)
		{
			Filter.Clear();
			memcpy(Filter.aInputBuf, LoadedFilters[FilterSelectedIdx].Filter.InputBuf, MAX_PATH);
			Filter.Build();
		}
			
		ImGui::SameLine();
		if (ImGui::SmallButton("Delete")) {
			DeleteFilter(pPlatformCtx);
		}
				
	}
	
	return bSelectedFilterChanged;
}


bool CrazyLog::DrawCherrypick(float DeltaTime, PlatformContext* pPlatformCtx)
{
	// HACKY we should put the color in the filter
	while (Filter.vFilters.Size > vFilterColor.Size)
	{
		vFilterColor.push_back(ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
	}
	
	bool bAnyFlagChanged = false;
	if (ImGui::TreeNode("Cherrypick"))
	{
		for (int i = 0; i != Filter.vFilters.Size; i++)
		{
			char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
			size_t FilterSize = Filter.vFilters[i].pEnd - Filter.vFilters[i].pBegin;
			pPlatformCtx->ScratchMem.PushBack(Filter.vFilters[i].pEnd - FilterSize, FilterSize);
			pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1);
				
			bool bChanged = ImGui::CheckboxFlags(pScratchStart, (ImU64*) &FilterFlags, 1ull << i);
			if (bChanged)
				bAnyFlagChanged = true;
			
			ImGui::SameLine();
			ImGui::ColorEdit3(pScratchStart, (float*)&vFilterColor[i].x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
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
			int64_t RequiredSize = pEndChar - pStartChar;
			if (pPlatformCtx->ScratchMem.PushBack(pStartChar, RequiredSize) &&
				pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1))
			{
				if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
				{
					ImGui::SetClipboardText(pScratchStart);
					SetLastCommand("WORD SELECTION COPIED TO CLIPBOARD");
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
		bool bFilterEnabled = (FilterFlags & (1ull << i));
		if (bFilterEnabled)
			return true;
	}
	
	return false;
}

void CrazyLog::CacheHighlightLineMatches(const char* pLineBegin, const char* pLineEnd, HighlightLineMatches* pFiltredLineMatch)
{
	pFiltredLineMatch->vLineMatches.clear();
	
	for (int i = 0; i != Filter.vFilters.Size; i++)
	{
		bool bFilterEnabled = (FilterFlags & (1ull << i));
		if (!bFilterEnabled)
			continue;
		
		const CrazyTextFilter::CrazyTextRange& f = Filter.vFilters[i];
		if (f.Empty())
			continue;
		if (f.pBegin[0] == '-')
			continue;

		CacheHighlightMatchingWord(pLineBegin, pLineEnd, i, pFiltredLineMatch);
	}
	
	qsort(pFiltredLineMatch->vLineMatches.Data, pFiltredLineMatch->vLineMatches.Size, sizeof(HighlightLineMatchEntry), HighlightLineMatchEntry::SortFunc);
}

void CrazyLog::CacheHighlightMatchingWord(const char* pLineBegin, const char* pLineEnd, int FilterIdx, 
                                          HighlightLineMatches* pFiltredLineMatch)
{
	const CrazyTextFilter::CrazyTextRange& f = Filter.vFilters[FilterIdx];
	
	bool bIsWildCard = f.pBegin[0] == '+';
	const char* pWordBegin = bIsWildCard ? f.pBegin + 1 : f.pBegin;
	const char* pWordEnd = f.pEnd;
	
	if (!pWordEnd)
		pWordEnd = pWordBegin + strlen(pWordBegin);

	const char FirstWordChar = (char)ImToUpper(*pWordBegin);
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
			
			// If we reached the end of the word it means that the entire word is equal
			if (pWordCursor == pWordEnd)
			{
				pFiltredLineMatch->vLineMatches.push_back(HighlightLineMatchEntry(FilterIdx, pLineBegin - Buf.begin(), (pLineCursor - 1) - Buf.begin()));
			}
		}
		
		pLineBegin++;
	}
}

bool CrazyLog::CustomDrawFilter(const char* label, float width)
{
	if (width != 0.0f)
		ImGui::SetNextItemWidth(width);
	bool value_changed = ImGui::InputText(label, Filter.aInputBuf, IM_ARRAYSIZE(Filter.aInputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
	if (value_changed)
		Filter.Build();
	return value_changed;
}

#undef FILTERS_FILE_NAME
#undef FOLDER_QUERY_NAME
#undef FILTER_TOKEN
#undef FILTER_INTERVAL
#undef FILE_FETCH_INTERVAL
#undef FILTER_TOKEN
#undef FILE_FETCH_INTERVALERVAL
#undef FILE_FETCH_INTERVAL
#undef FOLDER_FETCH_INTERVAL
#undef CONSOLAS_FONT_SIZE