#pragma once
#include <thread>
#include <functional>

template<typename T, int MAX_THREADS>
static void ExecuteParallel(const int numThreads, T* data, const int size,
                            std::function<void(int, void*, ImVector<T>*)> f,
                            void* ctx, bool waitUntilFinish,
                            ImVector<T>* pOutResult)
{
	const int itemPerThread = size / (numThreads + waitUntilFinish);
	const T* end = data + size;
	
	ImVector<T> vThreadsBuffer[MAX_THREADS + 1];
	memset(&vThreadsBuffer, 0, sizeof(vThreadsBuffer));
	
	// TODO(matiasp): Just use scratch allocator that should be enough.
#if 0
	for (int i = 0; i < MAX_THREADS + 1; ++i)
	{
		int Remaining = size - (itemPerThread * (i + 1));
		if (Remaining < 0)
			break;
		
		vThreadsBuffer[i].reserve(itemPerThread);;
	}
#endif
	
	std::thread threads[MAX_THREADS];

	auto threadJob = [f, itemPerThread, end, ctx, size](T* data, ImVector<T>* pOut) -> void
	{
		for (int i = 0; i < itemPerThread; ++i) 
		{
			int idx = size - (int)(end - &data[i]);
			f(idx, ctx, pOut);
		}
			
	};


	for (int i = 0; i < numThreads; ++i)
	{
		new(threads + i)std::thread(threadJob, data, &vThreadsBuffer[i]);
		data += itemPerThread;
	}
	
	if (waitUntilFinish)
	{
		while (data < end)
		{
			// work in this thread too
			int idx = size - (int)(end - data);
			f(idx, ctx, &vThreadsBuffer[MAX_THREADS]);
			data++;
		}
		
		// wait until all threads finished
		for (int i = 0; i < numThreads; ++i)
		{
			threads[i].join();
		}
		
		unsigned TotalSize = 0;
		for (int i = 0; i < MAX_THREADS + 1; ++i)
		{
			TotalSize += vThreadsBuffer[i].Size;
		}
		
		unsigned OriginalSize = pOutResult->Size;
		pOutResult->resize(OriginalSize + TotalSize);
		
		unsigned CopiedSize = 0;
		
		for (int i = 0; i < MAX_THREADS + 1; ++i)
		{
			if (vThreadsBuffer[i].Size == 0)
				continue;
			
			memcpy(&(*pOutResult)[OriginalSize + CopiedSize], 
			       vThreadsBuffer[i].Data, 
			       sizeof(int) * vThreadsBuffer[i].Size);
			
			CopiedSize += vThreadsBuffer[i].Size;
			
			if (CopiedSize >= TotalSize)
				break;
		}
		
		// Do something with the thread cache?
	}
}