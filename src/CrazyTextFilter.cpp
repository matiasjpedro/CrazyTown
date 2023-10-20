#include "CrazyTextFilter.h"

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
	vFilters.resize(0);
	vScopes.resize(0);
	CrazyTextRange input_range(aInputBuf, aInputBuf + strlen(aInputBuf), 0);
	input_range.Split(&vFilters,&vScopes);

	for (int i = 0; i != vFilters.Size; i++)
	{
		CrazyTextRange& f = vFilters[i];
		
		// Here do the proper cleanup
		while (f.pBegin < f.pEnd 
			&& (ImCharIsBlankA(f.pBegin[0]) || f.pBegin[0] == '('))
			f.pBegin++;
		while (f.pEnd > f.pBegin 
			&& (ImCharIsBlankA(f.pEnd[-1]) || f.pEnd[-1] == ')' || f.pEnd[-1] == 0))
			f.pEnd--;
		
		if (f.Empty())
			continue;
	}
}

bool CrazyTextFilter::PassFilter(uint64_t EnableMask, const char* pText, const char* pTextEnd) const
{
	if (vFilters.empty())
		return true;

	if (pText == NULL)
		pText = "";
	
	//TODO(Matiasp): Come back to this filter, it looks way more complicated of that it should.
	
	bool bFistValueAlreadySet = false;
	bool Result = false;
	for (int i = 0; i < vFilters.Size; i++)
	{
		// Find scope that belongs;
		const CrazyTextRange* pScopeRange = nullptr;
		for (int j = 0; j < vScopes.Size; j++) 
		{
			if (vScopes[j].pBegin < vFilters[i].pBegin && vScopes[j].pEnd >= vFilters[i].pEnd)
			{
				pScopeRange = &vScopes[j];
			}
			// If we already find a scope lets break, since we found the deeper one;
			else if (pScopeRange != nullptr)
			{
				break;
			}
		}
		
		// Process all the filters inside that scope
		if (pScopeRange)
		{
			bool bScopeValueValid = false;
			bool bFirstScopeValueAlreadySet = false;
			bool bScopeResult = false;
			unsigned bScopeOperatorFlag = vFilters[i].OperatorFlags;
			while (i < vFilters.Size
				&& pScopeRange->pBegin < vFilters[i].pBegin
				&& pScopeRange->pEnd >= vFilters[i].pEnd) 
			{
				if (!(EnableMask & (1ull << i)))
				{
					i++;
					continue;
				}
				
				bScopeValueValid = true;
			
				bool bCheckNot = !!(vFilters[i].OperatorFlags & 1 << FO_NOT);
				bool bMatch = bCheckNot ? 
					ImStristr(pText, pTextEnd, vFilters[i].pBegin+1, vFilters[i].pEnd) != NULL:
					ImStristr(pText, pTextEnd, vFilters[i].pBegin, vFilters[i].pEnd) != NULL;
		
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
				
				// The for loop is going to sum again, so we just remove the last one that we checked. 
				i--;
			}
		}
		else
		{
			if (!(EnableMask & (1ull << i)))
			{
				continue;
			}
			
			bool bCheckNot = !!(vFilters[i].OperatorFlags & 1 << FO_NOT);
			bool bMatch = bCheckNot ? 
				ImStristr(pText, pTextEnd, vFilters[i].pBegin+1, vFilters[i].pEnd) != NULL:
				ImStristr(pText, pTextEnd, vFilters[i].pBegin, vFilters[i].pEnd) != NULL;
			
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

void CrazyTextFilter::CrazyTextRange::Split(ImVector<CrazyTextRange>* pvOut, ImVector<CrazyTextRange>* pvScopesOut) const
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
			pvScopesOut->push_back(CrazyTextRange(pCursorEnd, nullptr, 0));
			ScopeCounter = 0;
			
		}
		else if (*pCursorEnd == ')')
		{
			(*pvScopesOut)[pvScopesOut->Size - 1 - ScopeCounter].pEnd = pCursorEnd;
			ScopeCounter++;
		}
			
		for (int i = FO_OR; i <= FO_AND; i++)
		{
			char* pSeparator = apSeparatorStr[i];
			size_t SeparatorLen = 2;
			
			if (memcmp(pCursorEnd, pSeparator, SeparatorLen) == 0)
			{
				uint8_t Flags = 1 << PrevOp;
				
				if (*pCursorBegin == '!')
					Flags |= 1 << FO_NOT;
				
				// -1 to don't count the separator
				pvOut->push_back(CrazyTextRange(pCursorBegin, pCursorEnd - 1, Flags));
				pCursorBegin = pCursorEnd + SeparatorLen;
				
				PrevOp = (FilterOperator)i;
				pCursorEnd++;
			}
		}
		
		pCursorEnd++;
	}
	
	if (pCursorBegin != pCursorEnd)
	{
		uint8_t Flags = 1 << PrevOp;
		
		if (*pCursorBegin == '!')
			Flags |= 1 << FO_NOT;
		
		pvOut->push_back(CrazyTextRange(pCursorBegin, pCursorEnd, Flags));
	}
}
	