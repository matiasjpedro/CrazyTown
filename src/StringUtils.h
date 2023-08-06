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
}

