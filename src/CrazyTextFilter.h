#pragma once 

enum FilterOperator
{
	FO_NONE = 0,
	FO_OR = 1,
	FO_AND = 2,
	FO_NOT = 3,
	
	FO_COUNT = 4,
};

static char* apSeparatorStr[FO_COUNT] =
{
	nullptr,
	"||",
	"&&",
	"!"
};

struct CrazyTextFilter 
{
	CrazyTextFilter(const char* pDefaultFilter = "");
	bool Draw(const char* pLabel = "Filter (inc,-exc)", float Width = 0.0f); 
	bool PassFilter(uint64_t EnableMask, const char* pText, const char* pTextEnd = NULL) const;
	bool PassGroup(uint64_t EnableMask, const char* pText, const char* pTextEnd, 
	               uint8_t GroupNum, uint8_t ScopeNum, uint8_t& IterationIdx) const;
	void Build();
	void Clear() { aInputBuf[0] = 0; Build(); }
	bool IsActive() const { return !vFilters.empty(); }

	// [Internal]
	struct CrazyTextRange 
	{
		const char* pBegin;
		const char* pEnd;
		uint8_t OperatorFlags;
		int8_t ScopeNum;

		CrazyTextRange()
		{ 
			 pBegin = pEnd = NULL; 
			 OperatorFlags = 0;
			 ScopeNum = -1;
		}
		
		CrazyTextRange(const char* _pBegin, const char* _pEnd, uint8_t _Flags) 
		{ 
			pBegin = _pBegin; 
			pEnd = _pEnd; 
			OperatorFlags = _Flags; 
			ScopeNum = -1;
		}
		
		bool Empty() const { return OperatorFlags == 0; }
		void Split(ImVector<CrazyTextRange>* pvOut, ImVector<CrazyTextRange>* pvScopesOut) const;
	};
	
	char aInputBuf[256];
	ImVector<CrazyTextRange> vFilters;
};