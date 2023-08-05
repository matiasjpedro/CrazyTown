#define FILTERS_FILE_NAME "FILTERS"
#define FILTER_TOKEN ';'
#define FILTER_INTERVAL 1.f
#define FILE_FETCH_INTERVAL 1.f

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

struct NamedFilter {
	char aName[256];
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
	char aFilterNameToSave[MAX_PATH];
	int FilterSelectedIdx;
	int FiltredLinesCount;
	int LastFetchFileSize;
	bool bAlreadyCached;
	bool bFileLoaded;
	bool bStreamMode;
	bool bWantsToSave;
	bool bIsPeeking;
	// Output options
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	float SelectionSize;
	float FilterRefreshCooldown;
	float FileContentFetchCooldown;
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
		
		const char* pClipboardText = ImGui::GetClipboardText();
		if (pClipboardText) {
			size_t TextSize = StringUtils::Length(pClipboardText);
			SetLog(pClipboardText, (int)TextSize);
		}
	}

	void FetchFile(PlatformContext* pPlatformCtx) 
	{
		// IMP(matiasp): 
		// Avoid virtual alloc, use scratch alloc.
		// keep the file open to avoid read/alloc the entire content.
		FileContent File = pPlatformCtx->pReadFileFunc(aFilePathToLoad);
		if(File.pFile)
		{
			int NewContentSize = (int)File.Size - LastFetchFileSize;
			if (NewContentSize > 0) {
				AddLog((const char*)File.pFile + LastFetchFileSize, (int)File.Size - LastFetchFileSize);
			}
			
			pPlatformCtx->pFreeFileContentFunc(&File);
			LastFetchFileSize = (int)File.Size;
		}
	}
	
	void LoadFile(PlatformContext* pPlatformCtx) 
	{
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
		OutFile.pFile = (uint8_t*)pPlatformCtx->pScratchMemory + pPlatformCtx->ScratchSize;
	
		// Start from 1 to skip the NONE filter
		for (unsigned i = 1; i < (unsigned)LoadedFilters.size(); ++i)
		{
			size_t LoadedFilterNameLen = StringUtils::Length(LoadedFilters[i].aName);
			memcpy((uint8_t*)OutFile.pFile + OutFile.Size, LoadedFilters[i].aName, LoadedFilterNameLen);
			OutFile.Size += LoadedFilterNameLen;
			
			// Add the Token Separator
			char* TokeDest = (char*)OutFile.pFile + OutFile.Size;
			*TokeDest = FILTER_TOKEN;
			OutFile.Size += 1; // Add one more for the token separator
			
			// Copy the filter
			size_t LoadedFilterContentLen = StringUtils::Length(LoadedFilters[i].Filter.InputBuf);
			memcpy((uint8_t*)OutFile.pFile + OutFile.Size, LoadedFilters[i].Filter.InputBuf, LoadedFilterContentLen);
			OutFile.Size += LoadedFilterContentLen;
			
			// Add the Token Separator
			TokeDest = (char*)OutFile.pFile + OutFile.Size;
			*TokeDest = FILTER_TOKEN;
			OutFile.Size += 1; // Add one more for the token separator
		}
		
		pPlatformCtx->ScratchSize += OutFile.Size;
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
		if (!ImGui::Begin(title, pOpen, ImGuiWindowFlags_NoTitleBar))
		{
			ImGui::End();
			return;
		}
		
		//=============================================================
		// Target
		
		ImGui::SeparatorText("Target");
		
		
		ImGui::SetNextItemWidth(-200);
		if (ImGui::InputText("FilePath", aFilePathToLoad, MAX_PATH, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			LoadFile(pPlatformCtx);
		}
		
		ImGui::SameLine();
		bool bStreamModeChanged = ImGui::Checkbox("StreamMode", &bStreamMode);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
			ImGui::SetTooltip("StreamMode:\n"
			                  "Indicates if we should refetch the content of the file periodically,\n"
			                  "looking for new content added, like a log. \n");
		
		if (bStreamModeChanged) {
			if(bStreamMode)
				FileContentFetchCooldown = FILE_FETCH_INTERVAL;
			else
				FileContentFetchCooldown = -1.f;
		}
		
		if (FileContentFetchCooldown > 0 && bFileLoaded) {
			FileContentFetchCooldown -= DeltaTime;
			if (FileContentFetchCooldown <= 0.f) {
				FetchFile(pPlatformCtx);
				bAlreadyCached = false;
				FileContentFetchCooldown = FILE_FETCH_INTERVAL;
			}
		}
		
		//=============================================================
		// Filters 
		
		ImGui::SeparatorText("Filters");
		
		bool bFilterChanged = Filter.Draw("Filter", -200.0f);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
			ImGui::SetTooltip("Filter usage:\n"
			            "  \"\"         display all lines\n"
			            "  \"xxx\"      display lines containing \"xxx\"\n"
			            "  \"xxx,yyy\"  display lines containing \"xxx\" or \"yyy\"\n"
			            "  \"-xxx\"     hide lines containing \"xxx\"");
		
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
				ImGui::SetNextItemWidth(-200);
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
					char* pScratchStart = (char*)pPlatformCtx->pScratchMemory + pPlatformCtx->ScratchSize;
					size_t FilterSize = Filter.Filters[i].e - Filter.Filters[i].b;
					memcpy(pScratchStart, Filter.Filters[i].e - FilterSize, FilterSize);
					memset(pScratchStart + FilterSize, '\0', 1);
					
					pPlatformCtx->ScratchSize += FilterSize + 1;
					
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
		unsigned ExtraFlags = bIsShiftPressed || bIsCtrlressed ? ImGuiWindowFlags_NoScrollWithMouse : 0;
		
		if (!bIsShiftPressed && SelectionSize > 1.f) {
			SelectionSize = 1.f;
		}
		
		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
		if (ImGui::BeginChild("Output", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar | ExtraFlags))
		{
			bool bWantsToCopy = false;
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
						
						// Peek Full Version
						if (bIsCtrlressed && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone)) {
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
					
						// Select from Filtered Version
						if (bIsShiftPressed && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone)) {
							SelectionSize += ImGui::GetIO().MouseWheel;
							SelectionSize = max(SelectionSize, 1);
							
							int BottomLine = max(i - (int)SelectionSize + 1, 0);
							int TopLine = min(i + (int)SelectionSize, vFiltredLinesCached.Size - 1);
							
							bool bWroteOnScratch = false;
							char* pScratchStart = (char*)pPlatformCtx->pScratchMemory + pPlatformCtx->ScratchSize;
							for (int j = BottomLine; j < TopLine; j++) {
								
								int FilteredLineNo = vFiltredLinesCached[j];
								char* pFilteredLineStart = const_cast<char*>(buf + vLineOffsets[FilteredLineNo]);
								char* pFilteredLineEnd = const_cast<char*>((FilteredLineNo + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[FilteredLineNo + 1] - 1) : buf_end);
								
								int64_t Size = pFilteredLineEnd+1 - pFilteredLineStart;
								if (pPlatformCtx->ScratchSize + Size < pPlatformCtx->ScratchMemoryCapacity) {
									memcpy((char*)pPlatformCtx->pScratchMemory + pPlatformCtx->ScratchSize, pFilteredLineStart, Size);
									
									pPlatformCtx->ScratchSize += Size;
									bWroteOnScratch = true;
								}
								
							}
							
							if (bWroteOnScratch) {
								memset((char*)pPlatformCtx->pScratchMemory + pPlatformCtx->ScratchSize, '\0', 1);
								ImGui::SetNextWindowPos(ImGui::GetMousePos()+ ImVec2(20, 0), 0, ImVec2(0, 0.5));
								ImGui::SetTooltip(pScratchStart);
								
								if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
								{
									ImGui::SetClipboardText(pScratchStart);
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
						
						// Select full view
						if (bIsShiftPressed && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone)) {
							SelectionSize += ImGui::GetIO().MouseWheel;
							SelectionSize = max(SelectionSize, 1);
							
							int BottomLine = max(line_no - (int)SelectionSize + 1, 0);
							int TopLine = min(line_no + (int)SelectionSize, vLineOffsets.Size - 1);
							int64_t Size = (buf + vLineOffsets[TopLine]) - (buf + vLineOffsets[BottomLine]);
							
							char* pScratchStart = (char*)pPlatformCtx->pScratchMemory + pPlatformCtx->ScratchSize;
							if (pPlatformCtx->ScratchSize + Size + 1 < pPlatformCtx->ScratchMemoryCapacity) {
								memcpy(pScratchStart, buf + vLineOffsets[BottomLine], Size);
								memset(pScratchStart + Size, '\0', 1);
								ImGui::SetNextWindowPos(ImGui::GetMousePos() + ImVec2(20, 0), 0, ImVec2(0, 0.5));
								ImGui::SetTooltip(pScratchStart);
								pPlatformCtx->ScratchSize += Size + 1;
							}
							
							if (ImGui::IsKeyReleased(ImGuiKey_MouseMiddle))
							{
								ImGui::SetClipboardText(pScratchStart);
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
			if (bAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
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
			else
			{
				// Grep
				if (ImStristr(text, text_end, f.b, f.e) != NULL)
					return true;
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