namespace StringUtils
{
	void Concat(char* p_DestBuffer, size_t DestBufferSize,
	                const char* p_StringA, size_t BytesA, 
	                const char* p_StringB, size_t BytesB)
	{
		for (size_t i = 0; i < BytesA; i++)
		{
			*p_DestBuffer++ = *p_StringA++;
		}

		for (size_t i = 0; i < BytesB; i++)
		{
			*p_DestBuffer++ = *p_StringB++;
		}

		*p_DestBuffer++ = '\0';
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

