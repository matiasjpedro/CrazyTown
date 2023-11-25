#pragma once 

static ImVec4 aDefaultColors[9] =
{
	ImVec4(1.000f, 0.992f, 0.000f, 1.000f),
	ImVec4(0.000f, 0.992f, 0.961f, 1.000f),
	ImVec4(1.000f, 0.647f, 0.000f, 1.000f),
	ImVec4(0.396f, 1.000f, 0.451f, 1.000f),
	ImVec4(1.000f, 0.588f, 0.855f, 1.000f),
	ImVec4(0.686f, 1.000f, 1.000f, 1.000f),
	ImVec4(0.922f, 0.253f, 0.253f, 1.000f),
	ImVec4(0.588f, 1.000f, 0.663f, 1.000f),
	ImVec4(1.000f, 0.953f, 0.588f, 1.000f)
};

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
		uint16_t BeginOffset;
		uint16_t EndOffset;
		uint8_t OperatorFlags;
		int8_t ScopeNum;

		CrazyTextRange()
		{ 
			 BeginOffset = EndOffset = NULL; 
			 BeginOffset = EndOffset = OperatorFlags = 0;
			 ScopeNum = -1;
		}
		
		CrazyTextRange(uint16_t _BeginOffset, uint16_t _EndOffset, uint8_t _Flags) 
		{ 
			BeginOffset = _BeginOffset; 
			EndOffset = _EndOffset; 
			OperatorFlags = _Flags; 
			ScopeNum = -1;
		}
		
		bool Empty() const { return OperatorFlags == 0; }
		
		void Split(const char* pBegin, const char* pEnd, 
		           ImVector<CrazyTextRange>* pvOut, 
		           ImVector<CrazyTextRange>* pvScopesOut) const;
	};
	
	char aInputBuf[256];
	ImVector<CrazyTextRange> vFilters;
	ImVector<ImVec4> vColors;
};