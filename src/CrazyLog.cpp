
#define FILTERS_FILE_NAME "FILTERS"
#define FILTER_TOKEN ';'
#define FILTER_INTERVAL 1.f

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
	bool bAlreadyCached;
	bool bAutoScroll;  // Keep scrolling if already at the bottom.
	bool bShowLineNum;
	
	ImVector<NamedFilter> LoadedFilters;
	
	char aFilePathToLoad[MAX_PATH];
	char aFilterNameToSave[MAX_PATH];
	int FilterSelectedIdx;
	bool bWantsToSave;
	float FilterRefreshCooldown;

	CrazyLog()
	{
		bAutoScroll = true;
		bAlreadyCached = true;
		FilterRefreshCooldown = -1.f;
		
		Clear();
	}

	void Clear()
	{
		bAlreadyCached = false;
		bWantsToSave = false;
		FilterRefreshCooldown = -1;
		
		Buf.clear();
		vFiltredLinesCached.clear();
		vLineOffsets.clear();
		vLineOffsets.push_back(0);
	}
	
	void LoadClipboard() 
	{
		bAlreadyCached = false;
		
		const char* pClipboardText = ImGui::GetClipboardText();
		if (pClipboardText) {
			size_t TextSize = StringUtils::Length(pClipboardText);
			SimpleAddLog(pClipboardText, (int)TextSize);
		}
	}
	
	void LoadFile(PlatformContext* pPlatformCtx) 
	{
		bAlreadyCached = false;
		
		FileContent File = pPlatformCtx->pReadFileFunc(aFilePathToLoad);
		if(File.pFile)
		{
			SimpleAddLog((const char*)File.pFile, (int)File.Size);
			pPlatformCtx->pFreeFileContentFunc(&File);
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

	void SimpleAddLog(const char* pFileContent, int file_size) 
	{
		// Reserve new file size,
		// if's bigger then free the old allocation.
		Buf.Buf.reserve_discard(file_size);
		
		// Reset the head
		Buf.Buf.resize(0);
		
		Buf.append(pFileContent, pFileContent + file_size);
		vLineOffsets.clear();
		vLineOffsets.push_back(0);
		
		int old_size = 0;
		for (int new_size = Buf.size(); old_size < new_size; old_size++)
			if (Buf[old_size] == '\n')
				vLineOffsets.push_back(old_size + 1);
		
		vFiltredLinesCached.reserve_discard(vLineOffsets.Size);
	}

	void Draw(float DeltaTime, PlatformContext* pPlatformCtx, const char* title, bool* pOpen = NULL)
	{
		if (!ImGui::Begin(title, pOpen))
		{
			ImGui::End();
			return;
		}
		
		//=============================================================
		// Target
		
		ImGui::SeparatorText("Target");
		
		if (ImGui::SmallButton("LoadFile")) 
		{
			LoadFile(pPlatformCtx);
		}
		
		// Modes 
		// WebSocket
		// Folder
		// File
		//     Static
		//     Dynamic
	
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-100);
		bool value_changed = ImGui::InputText("FilePath", aFilePathToLoad, MAX_PATH);
		//ImGui::SetNextItemWidth(-100);
		//bool value_changed2 = ImGui::InputText("FindPath", aFilePathToLoad, MAX_PATH);
		
		//=============================================================
		// Filters 
		
		ImGui::SeparatorText("Filters");
		
		bool bFilterChanged = Filter.Draw("Filter", -150.0f);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone))
			ImGui::SetTooltip("Filter usage:\n"
			            "  \"\"         display all lines\n"
			            "  \"xxx\"      display lines containing \"xxx\"\n"
			            "  \"xxx,yyy\"  display lines containing \"xxx\" or \"yyy\"\n"
			            "  \"-xxx\"     hide lines containing \"xxx\"");
		
		
	
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
				ImGui::SetNextItemWidth(-150);
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
			
			// If we are typing set the refresh interval
			if (bFilterChanged) {
				FilterRefreshCooldown = FILTER_INTERVAL;
				FilterSelectedIdx = 0;
				
			// If we are selecting from the drop down do it right away
			} else if (bSelectedFilterChanged) {
				bAlreadyCached = false;
			}
		}
			
	
		if (FilterRefreshCooldown > 0) {
			FilterRefreshCooldown -= DeltaTime;
			if (FilterRefreshCooldown <= 0.f) {
				bAlreadyCached = false;
			}
		}

		//=============================================================
		// Output
		
		
		ImGui::SeparatorText("Output");
		const char* pOutputName = "Output";
		if (ImGui::BeginChild(pOutputName, ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
		{
			bool bWantsToCopy = false;
			if (ImGui::BeginPopupContextWindow())
			{
				if (ImGui::Selectable("Edit")) 
				{
				
				}
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
			
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char* buf = Buf.begin();
			const char* buf_end = Buf.end();
			if (bWantsToCopy)
				ImGui::LogToClipboard();
			if (Filter.IsActive())
			{
				if (!bAlreadyCached) 
				{
					vFiltredLinesCached.resize(0);
					
					for (int line_no = 0; line_no < vLineOffsets.Size; line_no++)
					{
						const char* line_start = buf + vLineOffsets[line_no];
						const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
						if (Filter.PassFilter(line_start, line_end)) {
							vFiltredLinesCached.push_back(line_no);
							
							ImGui::TextUnformatted(line_start, line_end);
							
						}
					}
					
					bAlreadyCached = true;
				}
				else
				{
					for (int i = 0; i < vFiltredLinesCached.Size; i++) {
						
						int line_no = vFiltredLinesCached[i];
						char* line_start = const_cast<char*>(buf + vLineOffsets[line_no]);
						char* line_end = const_cast<char*>((line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end);
						int64_t line_size = line_end - line_start;
					
						if (bShowLineNum) {
							char aLineNumberName[10] = { 0 };
							int len = sprintf_s(aLineNumberName, sizeof(aLineNumberName), "%i ", line_no);
						
							ImGui::TextUnformatted(aLineNumberName, &aLineNumberName[len]);
							ImGui::SameLine();
						}
						
						//https://github.com/ocornut/imgui/issues/950
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
			}
			else
			{
				// The simplest and easy way to display the entire buffer:
				//   ImGui::TextUnformatted(buf_begin, buf_end);
				// And it'll just work. TextUnformatted() has specialization for large blob of text and will fast-forward
				// to skip non-visible lines. Here we instead demonstrate using the clipper to only process lines that are
				// within the visible area.
				// If you have tens of thousands of items and their processing cost is non-negligible, coarse clipping them
				// on your side is recommended. Using ImGuiListClipper requires
				// - A) random access into your data
				// - B) items all being the  same height,
				// both of which we can handle since we have an array pointing to the beginning of each line of text.
				// When using the filter (in the block of code above) we don't have random access into the data to display
				// anymore, which is why we don't use the clipper. Storing or skimming through the search result would make
				// it possible (and would be recommended if you want to search through tens of thousands of entries).
				ImGuiListClipper clipper;
				clipper.Begin(vLineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char* line_start = buf + vLineOffsets[line_no];
						const char* line_end = (line_no + 1 < vLineOffsets.Size) ? (buf + vLineOffsets[line_no + 1] - 1) : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
			}
			
			if (bWantsToCopy)
				ImGui::LogFinish();
			
			ImGui::PopStyleVar();

			// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
			// Using a scrollbar or mouse-wheel will take away from the bottom edge.
			if (bAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
			
			
		}
		ImGui::EndChild();
		ImGui::End();
	}
};

#undef FILTERS_FILE_NAME
#undef FILTER_TOKEN
#undef FILTER_INTERVAL