#define FILTERS_FILE_NAME "FILTERS"
#define FILTER_TOKEN ';'
#define FILTER_INTERVAL 1.f
#define FILE_FETCH_INTERVAL 1.f
#define FOLDER_FETCH_INTERVAL 2.f

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

struct NamedFilter {
	char aName[256];
	ImGuiTextFilter Filter;
};

static char g_FilterToken = ';';
static char g_NullTerminator = '\0';

struct CrazyLog
{
	ImGuiTextBuffer Buf;
	ImGuiTextFilter Filter;
	ImVector<int> vLineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
	ImVector<int> vFiltredLinesCached;
	ImVector<NamedFilter> LoadedFilters;
	unsigned long aWriteTime[2];
	char aFilePathToLoad[MAX_PATH];
	char aFolderPathToLoad[MAX_PATH];
	char aFilterNameToSave[MAX_PATH];
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int LastFetchFileSize;
	bool bAlreadyCached;
	bool bFileLoaded;
	bool bFolderQuery;
	bool bStreamMode;
	bool bWantsToSave;
	bool bIsPeeking;
	// Output options
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	float SelectionSize;
	float FilterRefreshCooldown;
	float FileContentFetchCooldown;
	float FolderFetchCooldown;
	float PeekScrollValue;
	float FiltredScrollValue;
	
	uint64_t FilterFlags;
	int LastFrameFiltersCount;

	void Init()
	{
		bAutoScroll = true;
		SelectionSize = 1.f;
		FilterRefreshCooldown = -1.f;
		FileContentFetchCooldown = -1.f;
		FolderFetchCooldown = -1.f;
		PeekScrollValue = -1.f;
		FiltredScrollValue = -1.f;
		FilterFlags = 0xFFFFFFFF;
	}
	
	void Clear()
	{
		bIsPeeking = false;
		bFileLoaded = false;
		bWantsToSave = false;
		FilterRefreshCooldown = -1;
		
		Buf.clear();
		vLineOffsets.clear();
		vLineOffsets.push_back(0);
		ClearCache();
	}
	
	void LoadClipboard() 
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

	bool FetchFile(PlatformContext* pPlatformCtx) 
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
		
		FileContentFetchCooldown = FILE_FETCH_INTERVAL;
		
		return bNewContent;
	}
	
	void SearchLatestFile(PlatformContext* pPlatformCtx)
	{
		if (aFolderPathToLoad[0] == 0)
			return;
		
		LastFileFolder OutLastFileFolder = { 0 };
		bool bNewerFile = pPlatformCtx->pFetchLastFileFolderFunc(aFolderPathToLoad, aWriteTime, &OutLastFileFolder);
			
		// There is a newer file
		if (bNewerFile && strcmp(aFilePathToLoad, OutLastFileFolder.aFilePath) != 0) {
			strcpy_s(aFilePathToLoad, sizeof(aFilePathToLoad), OutLastFileFolder.aFilePath);
				
			bStreamMode = true;
			LoadFile(pPlatformCtx);
		}
		
		FolderFetchCooldown = FOLDER_FETCH_INTERVAL;
	}
	
	void LoadFile(PlatformContext* pPlatformCtx) 
	{
		if (aFilePathToLoad[0] == 0)
			return;
			
		bIsPeeking = false;
		
		FileContent File = pPlatformCtx->pReadFileFunc(aFilePathToLoad);
		if(File.pFile)
		{
			bFileLoaded = true;
			
			SetLog((const char*)File.pFile, (int)File.Size);
			pPlatformCtx->pFreeFileContentFunc(&File);
			
			LastFetchFileSize = (int)File.Size;
			
			if (bStreamMode)
				FileContentFetchCooldown = FILE_FETCH_INTERVAL;
		}
	}
	
	void LoadFilter(PlatformContext* pPlatformCtx)
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
	
	void SaveLoadedFilters(PlatformContext* pPlatformCtx) 
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
	
	
	void DeleteFilter(PlatformContext* pPlatformCtx) 
	{
		if (FilterSelectedIdx > 0)
		{
			LoadedFilters.erase(&LoadedFilters[FilterSelectedIdx]);
			SaveLoadedFilters(pPlatformCtx);
			
			FilterSelectedIdx = 0;
			Filter.Clear();
			bAlreadyCached = false;
			FiltredLinesCount = 0;
		}
	}
	
	void SaveFilter(PlatformContext* pPlatformCtx, char* pFilterName, char* pFilterContent) 
	{
		size_t FilterNameLen = StringUtils::Length(pFilterName);
		size_t FilterContentLen = StringUtils::Length(pFilterContent);
		
		LoadedFilters.resize(LoadedFilters.size() + 1);
		memcpy(LoadedFilters[LoadedFilters.size() - 1].aName, pFilterName, FilterNameLen+1);
		memcpy(LoadedFilters[LoadedFilters.size() - 1].Filter.InputBuf, pFilterContent, FilterContentLen+1);
		
		SaveLoadedFilters(pPlatformCtx);
		FilterSelectedIdx = LoadedFilters.size() - 1;
	}

	// This method will append to the buffer
	void AddLog(const char* pFileContent, int FileSize) 
	{
		int old_size = Buf.size();
		
		Buf.Buf.reserve(Buf.Buf.Size + FileSize);
		Buf.append(pFileContent, pFileContent + FileSize);
		
		for (int new_size = Buf.size(); old_size < new_size; old_size++)
			if (Buf[old_size] == '\n')
				vLineOffsets.push_back(old_size + 1);
		
		bAlreadyCached = false;
	}
	
	// This method will stomp the old buffer;
	void SetLog(const char* pFileContent, int FileSize) 
	{
		Buf.Buf.clear();
		vLineOffsets.clear();
		
		Buf.append(pFileContent, pFileContent + FileSize);
		vLineOffsets.push_back(0);
		
		int old_size = 0;
		for (int new_size = Buf.size(); old_size < new_size; old_size++)
			if (Buf[old_size] == '\n')
				vLineOffsets.push_back(old_size + 1);
		
		// Reset the cache and reserve the max amount needed
		ClearCache();
		vFiltredLinesCached.reserve_discard(vLineOffsets.Size);
	}
	
	void ClearCache() {
		vFiltredLinesCached.clear();
		FiltredLinesCount = 0;
		bAlreadyCached = false;
	}

	void Draw(float DeltaTime, PlatformContext* pPlatformCtx, const char* title, bool* pOpen = NULL)
	{
		if (!ImGui::Begin(title, pOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
		{
			ImGui::End();
			return;
		}
		
		//=============================================================
		// Target
		
		ImGui::SeparatorText("Target");
		
#if 0
		ImGui::SetNextItemWidth(-200);
		if (ImGui::InputText("FolderQuery", aFolderPathToLoad, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			bFolderQuery = true;
			SearchLatestFile(pPlatformCtx);
		}
		else if (bFolderQuery)
		{
			if (FolderFetchCooldown > 0.f ) {
				FolderFetchCooldown -= DeltaTime;
				if (FolderFetchCooldown <= 0.f) {
					SearchLatestFile(pPlatformCtx);
				}
			}
		}
		
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
			ImGui::SetTooltip("It will automatically load the last written file that matches the query. \n"
			                  "It will enable stream mode by default, since it will start streaming the last file into the output. \n"
			                  "Expected format example: D:\\logs\\*.extension \n");
		
		ImGui::SameLine();
		bool bFolderQueryChanged = ImGui::Checkbox("Enabled", &bFolderQuery);
		if (bFolderQueryChanged) {
			if (bFolderQuery)
				SearchLatestFile(pPlatformCtx);
			else
				FolderFetchCooldown = -1.f;
		}
#endif
		
		ImGui::SetNextItemWidth(-160);
		if (ImGui::InputText("FilePath", aFilePathToLoad, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			LoadFile(pPlatformCtx);
		}
		else if(bStreamMode)
		{
			if (FileContentFetchCooldown > 0 && bFileLoaded) {
				FileContentFetchCooldown -= DeltaTime;
				if (FileContentFetchCooldown <= 0.f) {
					bAlreadyCached = !FetchFile(pPlatformCtx);
				}
			}
		}
		
		ImGui::SameLine();
		bool bStreamModeChanged = ImGui::Checkbox("StreamMode", &bStreamMode);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
			ImGui::SetTooltip("StreamMode:\n"
			                  "Indicates if we should refetch the content of the file periodically,\n"
			                  "looking for new content added, like a log. \n");
		
		if (bStreamModeChanged) {
			if (bStreamMode)
				FetchFile(pPlatformCtx);
			else
				FileContentFetchCooldown = -1.f;
		}
		
		//=============================================================
		// Filters 
		
		ImGui::SeparatorText("Filters");
		
		bool bFilterChanged = Filter.Draw("Filter", -160.0f);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
			ImGui::SetTooltip("Filter usage:\n"
			            "  \"\"         	display all lines\n"
			            "  \"xxx\"      	display lines containing \"xxx\"\n"
			            "  \"xxx,yyy\"  	display lines containing \"xxx\" or \"yyy\"\n"
			            "  \"xxx,yyy,+zzz\" display lines containing (\"xxx\" or \"yyy\") and \"+zzz\"\n"
			            "  \"-xxx\"     	hide lines containing \"xxx\"");
		
		
		// If the size of the filters changed, make sure to start with those filters enabled.
		if (LastFrameFiltersCount < Filter.Filters.Size) 
		{
			for (int i = LastFrameFiltersCount; i < Filter.Filters.Size; i++) 
			{
				FilterFlags |= 1ull << i;
			}
		}
		
		LastFrameFiltersCount = Filter.Filters.Size;
	
		size_t FilterLen = StringUtils::Length(Filter.InputBuf);
		
		ImGui::SameLine();
		if (ImGui::SmallButton("SavePreset") && FilterLen != 0) {
			memset(aFilterNameToSave, 0, ArrayCount(aFilterNameToSave));
			bWantsToSave = true;
		}
	
		if (bWantsToSave) 
		{
			if (bWantsToSave) 
			{
				ImGui::SetNextItemWidth(-160);
				ImGui::InputText("PresetName", aFilterNameToSave, MAX_PATH);
			}
			
			size_t FilterNameLen = StringUtils::Length(aFilterNameToSave);
			if (ImGui::SmallButton("Accept") && FilterNameLen) 
			{
				SaveFilter(pPlatformCtx, aFilterNameToSave, Filter.InputBuf);
				bWantsToSave = false;
			}
		
			ImGui::SameLine();
			if (ImGui::SmallButton("Cancel")) 
				bWantsToSave = false;
		}
		
		bool bSelectedFilterChanged = false;
		if (!bWantsToSave) {
			if (LoadedFilters.Size > 0)
			{
				struct Funcs { 
					static bool ItemGetter(void* pData, int n, const char** out_ppStr) 
					{ 
						ImVector<NamedFilter>& vrLoadedFilters = *((ImVector<NamedFilter>*)pData);
						NamedFilter& NamedFilter = vrLoadedFilters[n];
						*out_ppStr = NamedFilter.aName; 
						return true; 
					} 
				};
		
				ImGui::SetNextItemWidth(-288);
				bSelectedFilterChanged = ImGui::Combo("Presets", &FilterSelectedIdx, &Funcs::ItemGetter, (void*)&LoadedFilters, LoadedFilters.Size);
				if (bSelectedFilterChanged)
				{
					Filter.Clear();
					memcpy(Filter.InputBuf, LoadedFilters[FilterSelectedIdx].Filter.InputBuf, MAX_PATH);
					Filter.Build();
				}
				
				ImGui::SameLine();
				if (ImGui::SmallButton("DeletePreset")) {
					DeleteFilter(pPlatformCtx);
				}
				
				ImGui::SameLine();
				if (ImGui::SmallButton("ReloadPresets")) {
					LoadFilter(pPlatformCtx);
				}
					
			}
		}
	
		if (Filter.IsActive() ) {
			if (ImGui::TreeNode("Cherrypick"))
			{
				bool bAnyFlagChanged = false;
				for (int i = 0; i != Filter.Filters.Size; i++)
				{
					char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
					size_t FilterSize = Filter.Filters[i].e - Filter.Filters[i].b;
					pPlatformCtx->ScratchMem.PushBack((void*)(Filter.Filters[i].e - FilterSize), FilterSize);
					pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1);
					
					bool bChanged = ImGui::CheckboxFlags(pScratchStart, (ImU64*) &FilterFlags, 1ull << i);
					if (bChanged)
						bAnyFlagChanged = true;
				}
				
				if (bAnyFlagChanged) {
					bAlreadyCached = false;
					FiltredLinesCount = 0;
				}
				
				ImGui::TreePop();
			}
			
			// If we are typing set the refresh interval
			if (bFilterChanged) {
				FilterRefreshCooldown = FILTER_INTERVAL;
				FilterSelectedIdx = 0;
				
			// If we are selecting from the drop down do it right away
			} else if (bSelectedFilterChanged) {
				bAlreadyCached = false;
				FiltredLinesCount = 0;
				FilterFlags = 0xFFFFFFFF;
			}
		}
			
	
		if (FilterRefreshCooldown > 0) {
			FilterRefreshCooldown -= DeltaTime;
			if (FilterRefreshCooldown <= 0.f) {
				bAlreadyCached = false;
				FiltredLinesCount = 0;
			}
		}

		//=============================================================
		// Output
		
		if (bIsPeeking)
		{
			ImGui::SeparatorText("OUTPUT / PEEKING");
		} 
		else if (Filter.IsActive())
		{
			ImGui::SeparatorText("OUTPUT / FILTRED");
		} 
		else 
		{
			ImGui::SeparatorText("OUTPUT / FULLVIEW");
		}
		
		bool bIsShiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift);
		bool bIsCtrlressed = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
		bool bIsAltPressed = ImGui::IsKeyDown(ImGuiKey_LeftAlt);
		unsigned ExtraFlags = bIsShiftPressed || bIsCtrlressed || bIsAltPressed ? ImGuiWindowFlags_NoScrollWithMouse : 0;
		
		if (!bIsShiftPressed && !bIsAltPressed && SelectionSize > 1.f) {
			SelectionSize = 0.f;
		}
		
		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
		if (ImGui::BeginChild("Output", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar | ExtraFlags))
		{
			bool bWantsToCopy = false;
			
			if (bIsCtrlressed && ImGui::IsKeyPressed(ImGuiKey_V))
			{
				if (ImGui::IsWindowFocused())	
					LoadClipboard();
			}
			
			if (bIsCtrlressed && ImGui::IsKeyPressed(ImGuiKey_C))
			{
				if (ImGui::IsWindowFocused())
					bWantsToCopy = true;
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
					ImGui::Checkbox("Show Line Number", &bShowLineNum);
					ImGui::EndMenu();
				}
				
				ImGui::EndPopup();
			}
			
			//ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10));
			const char* buf = Buf.begin();
			const char* buf_end = Buf.end();
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
				
				if (ImGui::IsKeyReleased(ImGuiKey_MouseX1)) 
				{
					OneTimeScrollValue = FiltredScrollValue;
					bIsPeeking = false;
				}
				
				if (!Filter.IsActive()) 
				{
					bIsPeeking = false;	
				}
			}
			
			if (Filter.IsActive() && !bIsPeeking)
			{
				if (!bAlreadyCached) 
				{
					if(FiltredLinesCount == 0)
						vFiltredLinesCached.resize(0);
					
					for (int line_no = FiltredLinesCount; line_no < vLineOffsets.Size; line_no++)
					{
						const char* line_start = buf + vLineOffsets[line_no];
						const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
						// TODO(Make a custom pass filter)
						if (CustomPassFilter(line_start, line_end)) {
							vFiltredLinesCached.push_back(line_no);
							
							ImGui::TextUnformatted(line_start, line_end);
						}
					}
					
					FiltredLinesCount = vLineOffsets.Size;
					bAlreadyCached = true;
				}
				else
				{
					for (int i = 0; i < vFiltredLinesCached.Size; i++) 
					{
						int line_no = vFiltredLinesCached[i];
						char* line_start = const_cast<char*>(buf + vLineOffsets[line_no]);
						char* line_end = const_cast<char*>((line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end);
						int64_t line_size = line_end - line_start;
					
						if (bShowLineNum) {
							char aLineNumberName[10] = { 0 };
							int len = sprintf_s(aLineNumberName, sizeof(aLineNumberName), "%i - ", line_no);
						
							ImGui::TextUnformatted(aLineNumberName, &aLineNumberName[len]);
							ImGui::SameLine();
						}
						
						ImGui::TextUnformatted(line_start, line_end);
						
						bool bIsItemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
						
						// Peek Full Version
						if (bIsCtrlressed && bIsItemHovered) 
						{
							if (ImGui::IsKeyReleased(ImGuiKey_MouseLeft))
							{
								// TODO(Matiasp): Why it does not work when I scale the font?
								float TopOffset = ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y;
								float ItemPosY = (float)(line_no + 1) * ImGui::GetTextLineHeightWithSpacing();
								
								// We apply the same offset to maintain the same scroll position
								// between peeking and filtred view.
								PeekScrollValue = ItemPosY - TopOffset;
								FiltredScrollValue = ImGui::GetScrollY();
								
								bIsPeeking = true;
							}
						}
						// Select lines from Filtered Version
						else if (bIsShiftPressed && bIsItemHovered) 
						{
							SelectionSize += ImGui::GetIO().MouseWheel;
							SelectionSize = max(SelectionSize, 1);
							
							int BottomLine = max(i - (int)SelectionSize + 1, 0);
							int TopLine = min(i + (int)SelectionSize, vFiltredLinesCached.Size - 1);
							
							bool bWroteOnScratch = false;
							char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
							for (int j = BottomLine; j < TopLine; j++) {
								
								int FilteredLineNo = vFiltredLinesCached[j];
								char* pFilteredLineStart = const_cast<char*>(buf + vLineOffsets[FilteredLineNo]);
								char* pFilteredLineEnd = const_cast<char*>((FilteredLineNo + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[FilteredLineNo + 1] - 1) : buf_end);
								
								int64_t Size = pFilteredLineEnd+1 - pFilteredLineStart;
								bWroteOnScratch |= pPlatformCtx->ScratchMem.PushBack(pFilteredLineStart, Size);
							}
							
							if (bWroteOnScratch) {
								pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1);
								ImGui::SetNextWindowPos(ImGui::GetMousePos()+ ImVec2(20, 0), 0, ImVec2(0, 0.5));
								ImGui::SetTooltip(pScratchStart);
								
								if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
								{
									ImGui::SetClipboardText(pScratchStart);
								}
							}
							
						}
						// Select chars from Filtered Version
						else if (bIsAltPressed && bIsItemHovered) 
						{
							SelectionSize += ImGui::GetIO().MouseWheel;
							SelectionSize = max(SelectionSize, 1);
							
							int FilteredLineNo = vFiltredLinesCached[i];
							char* pFilteredLineStart = const_cast<char*>(buf + vLineOffsets[FilteredLineNo]);
							char* pFilteredLineEnd = const_cast<char*>((FilteredLineNo + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[FilteredLineNo + 1] - 1) : buf_end);
								
							int64_t LineSize = pFilteredLineEnd - pFilteredLineStart;
							
							float TabSize = 0.f;
							int TabCounter = 0;
							for (int j = 0; j < LineSize; ++j)
							{
								if (pFilteredLineStart[j] == '\t')
								{
									TabCounter++;
								}
								else
									break;
							}
						
							float TabOffset = 0.f;
							if (TabCounter > 0) 
							{
								TabOffset = ImGui::CalcTextSize(&pFilteredLineStart[0], &pFilteredLineStart[TabCounter]).x;
							}
							
							TabOffset *= 0.75f;
							
							float MousePosX = ImGui::GetMousePos().x;
							float TextOffset = ImGui::GetCursorScreenPos().x;
							
							for (int j = TabCounter; j < LineSize; ++j)
							{
								float CharSize = ImGui::CalcTextSize(&pFilteredLineStart[j], &pFilteredLineStart[j + 1]).x;
								float CharStartPos = TextOffset + TabOffset + (CharSize * j);
								float CharEndPos = CharStartPos + CharSize;
								
								if (MousePosX >= CharStartPos && MousePosX < CharEndPos)
								{
									int StartChar = j;
									int EndChar = min(j + (int)SelectionSize, (int)LineSize - 1);
									
									char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
									int64_t RequiredSize = EndChar - StartChar;
									
									if (pPlatformCtx->ScratchMem.PushBack(&pFilteredLineStart[StartChar], RequiredSize)
										&& pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1))
									{
										if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
										{
											ImGui::SetClipboardText(pScratchStart);
										}
										
										ImGui::SetTooltip(pScratchStart);
									}
									
									break;
								}
							}
						}
							
					}
				}
			}
			else
			{
				ImGuiListClipper clipper;
				clipper.Begin(vLineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char* line_start = buf + vLineOffsets[line_no];
						const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
						if (bShowLineNum) {
							char aLineNumberName[10] = { 0 };
							int len = sprintf_s(aLineNumberName, sizeof(aLineNumberName), "%i - ", line_no);
						
							ImGui::TextUnformatted(aLineNumberName, &aLineNumberName[len]);
							ImGui::SameLine();
						}
						
						ImGui::TextUnformatted(line_start, line_end);
						
						bool bIsItemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
						
						// Select lines full view
						if (bIsShiftPressed && bIsItemHovered) 
						{
							SelectionSize += ImGui::GetIO().MouseWheel;
							SelectionSize = max(SelectionSize, 1);
							
							int BottomLine = max(line_no - (int)SelectionSize + 1, 0);
							int TopLine = min(line_no + (int)SelectionSize, vLineOffsets.Size - 1);
							int64_t Size = (buf + vLineOffsets[TopLine]) - (buf + vLineOffsets[BottomLine]);
							
							char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
							if (pPlatformCtx->ScratchMem.PushBack(buf + vLineOffsets[BottomLine], Size) &&
								pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1))
							{
							
								ImGui::SetNextWindowPos(ImGui::GetMousePos() + ImVec2(20, 0), 0, ImVec2(0, 0.5));
								ImGui::SetTooltip(pScratchStart);
							}
							
							if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
							{
								ImGui::SetClipboardText(pScratchStart);
							}
						}
						// Select chars from Filtered Version
						else if (bIsAltPressed && bIsItemHovered) 
						{
							SelectionSize += ImGui::GetIO().MouseWheel;
							SelectionSize = max(SelectionSize, 1);
							
							int64_t LineSize = line_end - line_start;
							
							float TabSize = 0.f;
							int TabCounter = 0;
							for (int j = 0; j < LineSize; ++j)
							{
								if (line_start[j] == '\t')
								{
									TabCounter++;
								}
								else
									break;
							}
						
							float TabOffset = 0.f;
							if (TabCounter > 0) 
							{
								TabOffset = ImGui::CalcTextSize(&line_start[0], &line_start[TabCounter]).x;
							}
							
							TabOffset *= 0.75f;
							
							float MousePosX = ImGui::GetMousePos().x;
							float TextOffset = ImGui::GetCursorScreenPos().x;
							
							for (int j = TabCounter; j < LineSize; ++j)
							{
								float CharSize = ImGui::CalcTextSize(&line_start[j], &line_start[j + 1]).x;
								float CharStartPos = TextOffset + TabOffset + (CharSize * j);
								float CharEndPos = CharStartPos + CharSize;
								
								if (MousePosX >= CharStartPos && MousePosX < CharEndPos)
								{
									int StartChar = j;
									int EndChar = min(j + (int)SelectionSize, (int)LineSize - 1);
									
									char* pScratchStart = (char*)pPlatformCtx->ScratchMem.Back();
									int64_t RequiredSize = EndChar+1 - StartChar;
									if (pPlatformCtx->ScratchMem.PushBack(&line_start[StartChar], RequiredSize) &&
										pPlatformCtx->ScratchMem.PushBack(&g_NullTerminator, 1))
									{
										if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
										{
											ImGui::SetClipboardText(pScratchStart);
										}
										
										ImGui::SetTooltip(pScratchStart);
									}
									
									break;
								}
							}
						}
						
					}
				}
				clipper.End();
			}
			
			if (bWantsToCopy)
				ImGui::LogFinish();
			
			//ImGui::PopStyleVar();

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
		ImGui::End();
	}
	
	bool CustomPassFilter(const char* text, const char* text_end) const
	{
		if (Filter.Filters.empty())
			return true;

		if (text == NULL)
			text = "";

		for (int i = 0; i != Filter.Filters.Size; i++)
		{
			
			bool bFilterEnabled = (FilterFlags & (1ull << i));
			if (!bFilterEnabled)
				continue;
			
		
			const ImGuiTextFilter::ImGuiTextRange& f = Filter.Filters[i];
			if (f.empty())
				continue;
		
			if (f.b[0] == '-')
			{
				// Subtract
				if (ImStristr(text, text_end, f.b + 1, f.e) != NULL)
					return false;
			}
			else if (f.b[0] != '+')
			{
				// Grep
				if (ImStristr(text, text_end, f.b, f.e) != NULL) {
					
					// Append
					for (int j = 0; j != Filter.Filters.Size; j++)
					{
						const ImGuiTextFilter::ImGuiTextRange& f2 = Filter.Filters[j];
						if (f2.empty())
							continue;
						
						bool bFilterEnabled2 = (FilterFlags & (1ull << j));
						if (!bFilterEnabled2)
							continue;
						
						if (f2.b[0] == '+')
						{
							if (ImStristr(text, text_end, f2.b + 1, f2.e) == NULL)
								return false;
						}
					}
					
					return true;
				}
			}
		}

		// Implicit * grep
		if (Filter.CountGrep == 0)
			return true;

		return false;
	}
	
};

#undef FILTERS_FILE_NAME
#undef FILTER_TOKEN
#undef FILTER_INTERVAL
#undef FILE_FETCH_INTERVAL
#undef FILTER_TOKEN
#undef FILTER_INTERVAL
#undef FILE_FETCH_INTERVALERVAL
#undef FILE_FETCH_INTERVAL
#undef FOLDER_FETCH_INTERVAL