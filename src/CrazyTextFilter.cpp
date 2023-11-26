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


uint32_t clear_leftmost_set(const uint32_t value) {
	return value & (value - 1);
}


uint32_t get_first_bit_set(const uint32_t value) {
	return ctz(value);
}

bool HaystackContainsNeedleAVX(const char* pHaystack, size_t HaystackSize, const char* pNeedle, size_t NeedleSize)
{
	constexpr uint64_t UpcaseMask = 0xdfdfdfdfdfdfdfdfllu; 
	constexpr uint8_t UpcaseMask8 = 0xdf;
	
	const __m256i UpcaseMask256 = _mm256_set1_epi64x(UpcaseMask);
	
	__m256i first = _mm256_set1_epi8(pNeedle[0]);
	__m256i last  = _mm256_set1_epi8(pNeedle[NeedleSize - 1]);
	first = _mm256_and_si256(first, UpcaseMask256);
	last = _mm256_and_si256(last, UpcaseMask256);
	
	for (size_t i = 0; i < HaystackSize; i += 32) {

		const __m256i block_first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pHaystack + i));
		const __m256i block_last  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pHaystack + i + NeedleSize - 1));
		
		const __m256i eq_first= _mm256_cmpeq_epi8(first, _mm256_and_si256(block_first, UpcaseMask256));
		const __m256i eq_last  = _mm256_cmpeq_epi8(last, _mm256_and_si256(block_last, UpcaseMask256));
		
		uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

		while (mask != 0) {

			const uint32_t bitpos = get_first_bit_set(mask);
			
			const char* substr = pHaystack + i + bitpos + 1;
			
			// This is to avoid bleeding outside of the haystack size
			if (&substr[NeedleSize - 2] > pHaystack + HaystackSize)
				return false;
			
			const char* pNeedleCursor = &pNeedle[1];
			bool bMatchEntireWord = true;
			for (int z = 0; z < NeedleSize - 2; ++z) {
				if ((pNeedleCursor[z] & UpcaseMask8) != (substr[z] & UpcaseMask8)) {
					bMatchEntireWord = false;
					break;
				}
			}
			
			if (bMatchEntireWord)
				return true;
			
			mask = clear_leftmost_set(mask);
		}
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
	
	const uint64_t first = FirstBitSet * static_cast<uint8_t>(pNeedle[0]);
	const uint64_t last  = FirstBitSet * static_cast<uint8_t>(pNeedle[NeedleSize - 1]);
	
	uint64_t* block_first = reinterpret_cast<uint64_t*>(const_cast<char*>(pHaystack));
	uint64_t* block_last  = reinterpret_cast<uint64_t*>(const_cast<char*>(pHaystack + NeedleSize - 1));

	for (auto i=0u; i < HaystackSize; i+=8, block_first++, block_last++) {
		const uint64_t eq = ((*block_first ^ first) & UpcaseMask) | ((*block_last ^ last) & UpcaseMask);

		const uint64_t t0 = (~eq & AllButLastBitSet) + FirstBitSet;
		const uint64_t t1 = (~eq & LastBitSet);
		uint64_t zeros = t0 & t1;
		
		size_t j = 0;

		while (zeros) {
			if (zeros & 0x80) {
				
				const char* substr = reinterpret_cast<char*>(block_first) + j + 1;
				const char* pNeedleCursor = &pNeedle[1];
				
				// This is to avoid bleeding out of the haystack size
				if (&substr[NeedleSize - 2] > pHaystack + HaystackSize)
					return false;
				
				bool bMatchEntireWord = true;
				for (int z = 0; z < NeedleSize - 2; ++z) {
					if ((pNeedleCursor[z] & UpcaseMask8) != (substr[z] & UpcaseMask8)) {
						bMatchEntireWord = false;
						break;
					}
				}
				
				if (bMatchEntireWord)
					return true;
			}

			zeros >>= 8;
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

bool CrazyTextFilter::Draw(const char* pLabel, float Width)
{
	if (Width != 0.0f)
		ImGui::SetNextItemWidth(Width);
	bool value_changed = ImGui::InputText(pLabel, aInputBuf, IM_ARRAYSIZE(aInputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
	if (value_changed)
		Build();
	return value_changed;
	
}

void CrazyTextFilter::Build()
{
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
	
	size_t OldColorSize = vColors.size();
	vColors.resize(vFilters.size());
	
	for (int i = (int)OldColorSize; i < vColors.size(); i++) {
		vColors[i] = ImVec4(1.000f, 0.992f, 0.000f, 1.000f);
	}
}

#define USE_SWAR 1
#define USE_AVX 1

bool CrazyTextFilter::PassFilter(uint64_t EnableMask, const char* pText, const char* pTextEnd) const
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
				if (!(EnableMask & (1ull << i)))
				{
					i++;
					continue;
				}
				
				bScopeValueValid = true;
			
				bool bCheckNot = !!(vFilters[i].OperatorFlags & 1 << FO_NOT);
				
				const char* pNeedle = bCheckNot ? &aInputBuf[vFilters[i].BeginOffset] + 1 : &aInputBuf[vFilters[i].BeginOffset];
				size_t NeedleSize = vFilters[i].EndOffset - vFilters[i].BeginOffset;
				
				bool bContainsNeedle = false;
				
				if (NeedleSize < 4) 
				{
					const char* pNeedleEnd = &aInputBuf[vFilters[i].EndOffset];
					bContainsNeedle = ImStristr(pText, pTextEnd, pNeedle, pNeedleEnd) != NULL;
				}
				else
				{
					
					size_t HaystackSize = pTextEnd - pText;
#if USE_AVX
					bContainsNeedle = HaystackContainsNeedleAVX(pText, HaystackSize, pNeedle, NeedleSize);
#else
					bContainsNeedle = HaystackContainsNeedle(pText, HaystackSize, pNeedle, NeedleSize);
#endif
				}
				
				bool bMatch = bCheckNot ? !bContainsNeedle : bContainsNeedle;
		
				if (bFirstScopeValueAlreadySet)
				{
					if (!!(vFilters[i].OperatorFlags & 1 << FO_OR))
					{
						bScopeResult |= bCheckNot ? !bMatch : bMatch;
					}
					else if (!!(vFilters[i].OperatorFlags & 1 << FO_AND))
					{
						bScopeResult &= bCheckNot ? !bMatch : bMatch;
					}
				}
				else
				{
					bScopeResult = bCheckNot ? !bMatch : bMatch;
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
			if (!(EnableMask & (1ull << i)))
			{
				continue;
			}
			
			
			
			bool bCheckNot = !!(vFilters[i].OperatorFlags & 1 << FO_NOT);
			
			const char* pNeedle = bCheckNot ? &aInputBuf[vFilters[i].BeginOffset] + 1 : &aInputBuf[vFilters[i].BeginOffset];
			size_t NeedleSize = vFilters[i].EndOffset - vFilters[i].BeginOffset;
				
			bool bContainsNeedle = false;
				
			if (NeedleSize < 4) 
			{
				const char* pNeedleEnd = &aInputBuf[vFilters[i].EndOffset];
				bContainsNeedle = ImStristr(pText, pTextEnd, pNeedle, pNeedleEnd) != NULL;
			}
			else
			{
					
				size_t HaystackSize = pTextEnd - pText;
#if USE_AVX
				bContainsNeedle = HaystackContainsNeedleAVX(pText, HaystackSize, pNeedle, NeedleSize);
#else
				bContainsNeedle = HaystackContainsNeedle(pText, HaystackSize, pNeedle, NeedleSize);
#endif
			}
				
			bool bMatch = bCheckNot ? !bContainsNeedle : bContainsNeedle;
			
			if (bFistValueAlreadySet)
			{
				if (!!(vFilters[i].OperatorFlags & 1 << FO_OR))
				{
					Result |= bCheckNot ? !bMatch : bMatch;
				}
				else if (!!(vFilters[i].OperatorFlags & 1 << FO_AND))
				{
					Result &= bCheckNot ? !bMatch : bMatch;
				}
			}
			else
			{
				Result = bCheckNot ? !bMatch : bMatch;
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
	