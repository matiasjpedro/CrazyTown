#include "CrazyTextFilter.h"

uint32_t __inline ctz( uint32_t value )
{
	DWORD trailing_zero = 0;

	if ( _BitScanForward( &trailing_zero, value ) )
	{
		return trailing_zero;
	}
	else
	{
		// This is undefined, I better choose 32 than 0
		return 32;
	}
}


uint32_t ClearLeftMostSet(const uint32_t value) {
	return value & (value - 1);
}


uint32_t GetFirstBitSet(const uint32_t value) {
	return ctz(value);
}

bool HaystackContainsNeedleAVX(const char* pHaystack, size_t HaystackSize, const char* pNeedle, size_t NeedleSize, const char* pBufEnd)
{
	size_t LastIteration = HaystackSize / 32;
	const bool bWillExceedBufEnd = (pHaystack + (LastIteration*32) + NeedleSize - 1 + 32) >= pBufEnd;
	
	constexpr uint64_t UpcaseMask = 0xdfdfdfdfdfdfdfdfllu; 
	constexpr uint8_t UpcaseMask8 = 0xdf;
	
	const __m256i UpcaseMask256 = _mm256_set1_epi64x(UpcaseMask);
	
	__m256i First = _mm256_set1_epi8(pNeedle[0]);
	__m256i Last  = _mm256_set1_epi8(pNeedle[NeedleSize - 1]);
	First = _mm256_and_si256(First, UpcaseMask256);
	Last = _mm256_and_si256(Last, UpcaseMask256);
	
	size_t LastCharIdxBetween = NeedleSize < 3 ? NeedleSize - 1 : NeedleSize - 2;
	
	if (!bWillExceedBufEnd) 
	{
		for (size_t i = 0; i < HaystackSize; i += 32) 
		{
			const __m256i BlockFirst = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pHaystack + i));
			const __m256i BlockLast  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pHaystack + i + NeedleSize - 1));
	
			const __m256i EqualFirst = _mm256_cmpeq_epi8(First, _mm256_and_si256(BlockFirst, UpcaseMask256));
			const __m256i EqualLast  = _mm256_cmpeq_epi8(Last, _mm256_and_si256(BlockLast, UpcaseMask256));
	
			uint32_t Mask = _mm256_movemask_epi8(_mm256_and_si256(EqualFirst, EqualLast));

			while (Mask != 0) {

				const uint32_t BitPos = GetFirstBitSet(Mask);
		
				const char* pSubStr = pHaystack + i + BitPos + 1;
		
				// This is to avoid bleeding outside of the haystack size
				if (&pSubStr[LastCharIdxBetween] > pHaystack + HaystackSize)
					return false;
		
				const char* pNeedleCursor = NeedleSize == 1 ? &pNeedle[0] : &pNeedle[1];
				bool bMatchEntireWord = true;
				for (size_t z = 0; z < LastCharIdxBetween; ++z) {
					if ((pNeedleCursor[z] & UpcaseMask8) != (pSubStr[z] & UpcaseMask8)) {
						bMatchEntireWord = false;
						break;
					}
				}
		
				if (bMatchEntireWord)
					return true;
		
				Mask = ClearLeftMostSet(Mask);
			}
		
		}
	}
	else 
	{
		return ImStristr(pHaystack, pHaystack + HaystackSize, pNeedle, pNeedle + NeedleSize);
	}
	
	return false;
}

bool HaystackContainsNeedle(const char* pHaystack, size_t HaystackSize, const char* pNeedle, size_t NeedleSize)
{
	constexpr uint64_t FirstBitSet = 0x0101010101010101llu; 	 // each uint8_t -> 0000 0001
	constexpr uint64_t AllButLastBitSet = 0x7f7f7f7f7f7f7f7fllu; // each uint8_t -> 0111 1111
	constexpr uint64_t LastBitSet = 0x8080808080808080llu; 		 // each uint8_t -> 1000 0000
	constexpr uint64_t UpcaseMask = 0xdfdfdfdfdfdfdfdfllu; 		 // each uint8_t -> 1101 1111
	constexpr uint8_t UpcaseMask8 = 0xdf;
	
	const uint64_t First = FirstBitSet * static_cast<uint8_t>(pNeedle[0]);
	const uint64_t Last  = FirstBitSet * static_cast<uint8_t>(pNeedle[NeedleSize - 1]);
	
	uint64_t* BlockFirst = reinterpret_cast<uint64_t*>(const_cast<char*>(pHaystack));
	uint64_t* BlockLast  = reinterpret_cast<uint64_t*>(const_cast<char*>(pHaystack + NeedleSize - 1));
	
	size_t LastCharIdxBetween = NeedleSize < 3 ? NeedleSize - 1 : NeedleSize - 2;

	for (auto i=0u; i < HaystackSize; i+=8, BlockFirst++, BlockLast++) {
		const uint64_t Equal = ((*BlockFirst ^ First) & UpcaseMask) | ((*BlockLast ^ Last) & UpcaseMask);

		const uint64_t T0 = (~Equal & AllButLastBitSet) + FirstBitSet;
		const uint64_t T1 = (~Equal & LastBitSet);
		uint64_t Zeros = T0 & T1;
		
		size_t j = 0;

		while (Zeros) {
			if (Zeros & 0x80) {
				
				const char* pSubStr = reinterpret_cast<char*>(BlockFirst) + j + 1;
				const char* pNeedleCursor = NeedleSize == 1 ? &pNeedle[0] : &pNeedle[1];
				
				// This is to avoid bleeding out of the haystack size
				if (&pSubStr[LastCharIdxBetween] > pHaystack + HaystackSize)
					return false;
				
				bool bMatchEntireWord = true;
				for (size_t z = 0; z < LastCharIdxBetween; ++z) {
					if ((pNeedleCursor[z] & UpcaseMask8) != (pSubStr[z] & UpcaseMask8)) {
						bMatchEntireWord = false;
						break;
					}
				}
				
				if (bMatchEntireWord)
					return true;
			}

			Zeros >>= 8;
			j += 1;
		}
	}
	
	return false;
}

CrazyTextFilter::CrazyTextFilter(const char* pDefaultFilter) 
{
	aInputBuf[0] = 0;
	if (pDefaultFilter)
	{
		ImStrncpy(aInputBuf, pDefaultFilter, IM_ARRAYSIZE(aInputBuf));
		Build();
	}
}

bool CrazyTextFilter::Draw(ImVector<ImVec4>* pvDefaultColors, const char* pLabel, float Width)
{
	if (Width != 0.0f)
		ImGui::SetNextItemWidth(Width);
	bool value_changed = ImGui::InputText(pLabel, aInputBuf, IM_ARRAYSIZE(aInputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
	if (value_changed)
		Build(pvDefaultColors);
	return value_changed;
	
}

void CrazyTextFilter::Build(ImVector<ImVec4>* pvDefaultColors, bool bRememberOldSettings)
{
	ImVector<CrazyTextRangeSettings> vOldSettings = vSettings;
	ImVector<CrazyTextRange> vOldFilters = vFilters;
	
	ImVector<CrazyTextRange> vScopes;
	vScopes.reserve(10);
	
	size_t InputBufferLen = strlen(aInputBuf);
	CrazyTextRange InputRange(0, (uint16_t)InputBufferLen, 0);
	InputRange.Split(&aInputBuf[0], &aInputBuf[InputBufferLen], &vFilters, &vScopes);

	for (int i = 0; i != vFilters.Size; i++)
	{
		CrazyTextRange& f = vFilters[i];
		
		if (f.Empty())
			continue;
		
		char* pFilterBegin = &aInputBuf[f.BeginOffset];
		char* pFilterEnd = &aInputBuf[f.EndOffset];
		
		// Cleanup the text range with the unwanted characters. 
		while (pFilterBegin < pFilterEnd
			&& (ImCharIsBlankA(pFilterBegin[0]) || pFilterBegin[0] == '('))
			pFilterBegin++;
		while (pFilterEnd > pFilterBegin 
			&& (ImCharIsBlankA(pFilterEnd[-1]) || pFilterEnd[-1] == ')' || pFilterEnd[-1] == 0))
			pFilterEnd--;
		
		f.BeginOffset = uint16_t(pFilterBegin - &aInputBuf[0]);
		f.EndOffset = uint16_t(pFilterEnd - &aInputBuf[0]);
		
		// Once I have clear out unwanted character check if I should add the not operator
		if (aInputBuf[f.BeginOffset] == '!')
			vFilters[i].OperatorFlags |= 1 << FO_NOT;
		
		// Assign Scopes
		int8_t ScopeNum = -1;
		for (int j = 0; j < vScopes.Size; j++) 
		{
			if (vScopes[j].BeginOffset < vFilters[i].BeginOffset && vScopes[j].EndOffset >= vFilters[i].EndOffset)
			{
				ScopeNum = (int8_t)j;
			}
			// If we already find a scope lets break, since we found the deeper one;
			else if (ScopeNum != -1)
			{
				break;
			}
		}
		
		vFilters[i].ScopeNum = ScopeNum;
	}
	
	size_t OldSize = vSettings.Size;
	vSettings.resize(vFilters.size());
	for (int i = 0; i != vSettings.Size; i++)
	{
		CrazyTextRange& f = vFilters[i];
		
		char* pFilterBegin = &aInputBuf[f.BeginOffset];
		char* pFilterEnd = &aInputBuf[f.EndOffset];
		
		vSettings[i].Id = HashString(pFilterBegin, pFilterEnd);
	}
	
	for (int i = 0; i != vSettings.Size; i++) 
	{
		// NOTE(matiasp): This is probably the worst thing that I made for this project
		// because I'm rebuilding the entire filter every time that I build
		// I need to remember which colors/toggle was set for each filter
		// this is mainly to avoid offsets when I remove/add a filter in the middle
		if (bRememberOldSettings)
		{
			bool bFoundCandidate = false;
			for (int j = 0; j != vOldSettings.Size; j++) 
			{
				if (vSettings[i].Id != vOldSettings[j].Id) 
					continue;
		
				// We found one with the same id we can copy it
				vSettings[i] = vOldSettings[j];
				
				bFoundCandidate = true;
		
				// In case the same world in multiple scopes this will make sure that
				// pick the correct one because it's a perfect match
				if (vFilters[i].ScopeNum == vOldFilters[j].ScopeNum) {
					break;
				}
			}
			
			if (!bFoundCandidate) {
				
				if (i < vOldSettings.size()) 
				{
					vSettings[i].Color = vOldSettings[i].Color;
					vSettings[i].bIsEnabled = vOldSettings[i].bIsEnabled;
				}
				else
				{
					if (pvDefaultColors && pvDefaultColors->Size > 0) 
					{
						ImVector<ImVec4>& vDefaultColors = *pvDefaultColors;
						int ColorIdx = i % (vDefaultColors.Size);
						vSettings[i].Color = vDefaultColors[ColorIdx];
					}
					else
					{
						vSettings[i].Color = ImVec4(1.000f, 0.992f, 0.000f, 1.000f);
					}
					
					vSettings[i].bIsEnabled = true;
				}
				
			}
		}
		else
		{
			vSettings[i].bIsEnabled = true;
		}
	}
		
}

bool CrazyTextFilter::PassFilter(const char* pText, const char* pTextEnd, const char* pBufEnd, bool bUseAVX) const
{
	if (vFilters.empty())
		return true;

	if (pText == NULL)
		pText = "";
	
	if (*pText == '\r')
		return false;
	
	//TODO(Matiasp): Come back to this filter, it looks way more complicated of that it should.
	bool bFistValueAlreadySet = false;
	bool Result = false;
	for (int i = 0; i < vFilters.Size; i++)
	{
		// Process all the filters inside that scope
		int FilterScopeNum = vFilters[i].ScopeNum;
		if (FilterScopeNum != -1)
		{
			bool bScopeValueValid = false;
			bool bFirstScopeValueAlreadySet = false;
			bool bScopeResult = false;
			unsigned bScopeOperatorFlag = vFilters[i].OperatorFlags;
			while (i < vFilters.Size && FilterScopeNum == vFilters[i].ScopeNum) 
			{
				if (!vSettings[i].bIsEnabled)
				{
					i++;
					continue;
				}
				
				bScopeValueValid = true;
			
				bool bCheckNot = !!(vFilters[i].OperatorFlags & 1 << FO_NOT);
				
				uint16_t BeginOffset = bCheckNot ? vFilters[i].BeginOffset + 1 : vFilters[i].BeginOffset;
				
				const char* pNeedle = &aInputBuf[BeginOffset];
				size_t NeedleSize = vFilters[i].EndOffset - BeginOffset;
				
				bool bContainsNeedle = false;
				
				{
					
					size_t HaystackSize = pTextEnd - pText;
					if (bUseAVX)
					{
						bContainsNeedle = HaystackContainsNeedleAVX(pText, HaystackSize, pNeedle, NeedleSize, pBufEnd);
					}
					else
					{
						bContainsNeedle = ImStristr(pText, pTextEnd, pNeedle, pNeedle + NeedleSize);
					}

				}
				
				bool bMatch = bCheckNot ? !bContainsNeedle : bContainsNeedle;
		
				if (bFirstScopeValueAlreadySet)
				{
					if (!!(vFilters[i].OperatorFlags & 1 << FO_OR))
					{
						bScopeResult |= bMatch;
					}
					else if (!!(vFilters[i].OperatorFlags & 1 << FO_AND))
					{
						bScopeResult &= bMatch;
					}
				}
				else
				{
					bScopeResult = bMatch;
					bFirstScopeValueAlreadySet = true;
				}
				
				i++;
			}
			
			if (bScopeValueValid)
			{
				if (bFistValueAlreadySet)
				{
					if (!!(bScopeOperatorFlag & 1 << FO_OR))
					{
						Result |= bScopeResult;
					}
					else if (!!(bScopeOperatorFlag & 1 << FO_AND))
					{
						Result &= bScopeResult;
					}
				}
				else
				{
					Result = bScopeResult;
					bFistValueAlreadySet = true;
				}
			}
			
			// The for loop is going to sum again, so we just remove the last one that we checked. 
			i--;
		}
		else
		{
			if (!vSettings[i].bIsEnabled)
			{
				continue;
			}
			
			bool bCheckNot = !!(vFilters[i].OperatorFlags & 1 << FO_NOT);
			
			uint16_t BeginOffset = bCheckNot ? vFilters[i].BeginOffset + 1 : vFilters[i].BeginOffset;
				
			const char* pNeedle = &aInputBuf[BeginOffset];
			size_t NeedleSize = vFilters[i].EndOffset - BeginOffset;
			
			bool bContainsNeedle = false;
				
			{
				size_t HaystackSize = pTextEnd - pText;
				
				if (bUseAVX)
				{
					bContainsNeedle = HaystackContainsNeedleAVX(pText, HaystackSize, pNeedle, NeedleSize, pBufEnd);
				}
				else
				{
					bContainsNeedle = ImStristr(pText, pTextEnd, pNeedle, pNeedle + NeedleSize);
				}

			}
				
			bool bMatch = bCheckNot ? !bContainsNeedle : bContainsNeedle;
			
			if (bFistValueAlreadySet)
			{
				if (!!(vFilters[i].OperatorFlags & 1 << FO_OR))
				{
					Result |= bMatch;
				}
				else if (!!(vFilters[i].OperatorFlags & 1 << FO_AND))
				{
					Result &= bMatch;
				}
			}
			else
			{
				Result = bMatch;
				bFistValueAlreadySet = true;
			}
		}
	}
	
	return Result;
}

void CrazyTextFilter::CrazyTextRange::Split(const char* pBegin, const char* pEnd, ImVector<CrazyTextRange>* pvOut, ImVector<CrazyTextRange>* pvScopesOut) const
{
	pvOut->resize(0);
	const char* pCursorBegin = pBegin;
	const char* pCursorEnd = pBegin;
	
	FilterOperator PrevOp = FO_OR;
	int ScopeCounter = 0;
	while (pCursorEnd < pEnd)
	{
		if (*pCursorEnd == '(')
		{
			pvScopesOut->push_back(CrazyTextRange(uint16_t(pCursorEnd - pBegin), 0, 0));
			ScopeCounter = 0;
		}
		else if (*pCursorEnd == ')')
		{
			int ScopeIdx = pvScopesOut->Size - 1 - ScopeCounter;
			// if ScopeIdx is less than 0 it means that we don't have matching brackets
			// should I send an error?
			if (ScopeIdx >= 0)
			{
				(*pvScopesOut)[ScopeIdx].EndOffset = uint16_t(pCursorEnd - pBegin);
			}
			
			ScopeCounter++;
		}
			
		for (int i = FO_OR; i <= FO_AND; i++)
		{
			char* pSeparator = apSeparatorStr[i];
			size_t SeparatorLen = 2;
			
			if (memcmp(pCursorEnd, pSeparator, SeparatorLen) == 0)
			{
				uint8_t Flags = 1 << PrevOp;
				
				// -1 to don't count the separator
				pvOut->push_back(CrazyTextRange(uint16_t(pCursorBegin - pBegin), uint16_t(pCursorEnd - pBegin - 1), Flags));
				
				PrevOp = (FilterOperator)i;
				
				// Move the cursor after the separator 
				pCursorBegin = pCursorEnd + SeparatorLen;
				pCursorEnd++;
			}
		}
		
		pCursorEnd++;
	}
	
	if (pCursorBegin != pCursorEnd)
	{
		uint8_t Flags = 1 << PrevOp;
		pvOut->push_back(CrazyTextRange(uint16_t(pCursorBegin - pBegin), uint16_t(pCursorEnd - pBegin), Flags));
	}
}
	