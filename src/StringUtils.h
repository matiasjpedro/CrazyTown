namespace StringUtils
{
	int Concat(char* pDestBuffer, size_t DestBufferSize,
	                const char* pStringA, size_t BytesA, 
	                const char* pStringB, size_t BytesB)
	{
		int NewLen = 0;
		for (size_t i = 0; i < BytesA; i++)
		{
			*pDestBuffer++ = *pStringA++;
			NewLen++;
		}

		for (size_t i = 0; i < BytesB; i++)
		{
			*pDestBuffer++ = *pStringB++;
			NewLen++;
		}

		*pDestBuffer++ = '\0';
		NewLen++;
		
		return NewLen;
	}
	
	size_t Length(const char* pString)
	{
		size_t Result = 0;
		while (*pString++)
		{
			Result++;
		}

		return Result;
	}
	
	bool IsWhiteSpace(const char * pCharacter) 
	{
		return *pCharacter == ' ' || *pCharacter == '\r' || *pCharacter == '\n' || *pCharacter == '\t';
	}
	
	bool IsWordChar(const char * pCharacter)
	{
		return (*pCharacter >= 'a' &&  *pCharacter <= 'z')
			|| (*pCharacter >= 'A' &&  *pCharacter <= 'Z')
			|| (*pCharacter >= '0' &&  *pCharacter <= '9')
			|| *pCharacter == '_' ;
	}
	
	bool IsWhitspace(const char * pCharacter)
	{
		return *pCharacter == ' ' || *pCharacter == '\t';
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

