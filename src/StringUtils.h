namespace StringUtils
{
	int Concat(char* p_DestBuffer, size_t DestBufferSize,
	                const char* p_StringA, size_t BytesA, 
	                const char* p_StringB, size_t BytesB)
	{
		int NewLen = 0;
		for (size_t i = 0; i < BytesA; i++)
		{
			*p_DestBuffer++ = *p_StringA++;
			NewLen++;
		}

		for (size_t i = 0; i < BytesB; i++)
		{
			*p_DestBuffer++ = *p_StringB++;
			NewLen++;
		}

		*p_DestBuffer++ = '\0';
		NewLen++;
		
		return NewLen;
	}
	
	size_t Length(const char* p_String)
	{
		size_t Result = 0;
		while (*p_String++)
		{
			Result++;
		}

		return Result;
	}
	
	bool IsWhiteSpace(const char * p_Character) 
	{
		return *p_Character == ' ' || *p_Character == '\r' || *p_Character == '\n' || *p_Character == '\t';
	}
	
	bool IsWordChar(const char * p_Character)
	{
		return (*p_Character >= 'a' &&  *p_Character <= 'z')
			|| (*p_Character >= 'A' &&  *p_Character <= 'Z')
			|| (*p_Character >= '0' &&  *p_Character <= '9')
			|| *p_Character == '_' ;
	}
	
	bool IsWhitspace(const char * p_Character)
	{
		return *p_Character == ' ' || *p_Character == '\t';
	}
	
	int HexCharToInt(char Hex) 
	{
		if (Hex >= '0' && Hex <= '9') 
		{
			return Hex - '0';
		}
		if (Hex >= 'a' && Hex <= 'f') 
		{
			return Hex - 'a' + 10;
		}
		if (Hex >= 'A' && Hex <= 'F') 
		{
			return Hex - 'A' + 10;
		}
		return -1; // Invalid hex character
	}
	
	void HexToRGB(const char* pHexColorStr, float* pOutColor) 
	{
		// Ensure that the input string is at least 6 characters long
		if (Length(pHexColorStr) < 6) 
			return;

		// Skip the '#' symbol if present
		if (pHexColorStr[0] == '#') 
		{
			pHexColorStr++;
		}

		// Parse the hex color string
		for (int i = 0; i < 4; i++) 
		{
			pOutColor[i] = 16 * (float)HexCharToInt(pHexColorStr[i * 2]) + (float)HexCharToInt(pHexColorStr[i * 2 + 1]);
		}
	}
}

