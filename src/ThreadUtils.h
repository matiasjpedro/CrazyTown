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
	
	ImVector<T> vThreadsCache[MAX_THREADS + 1];
	
	// TODO(matiasp): Just use scratch allocator that should be enough.
	for (int i = 0; i < MAX_THREADS + 1; ++i)
	{
		int Remaining = size - (itemPerThread * (i + 1));
		if (Remaining < 0)
			break;
		
		vThreadsCache[i].reserve(itemPerThread);;
	}
	
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
		new(threads + i)std::thread(threadJob, data, &vThreadsCache[i]);
		data += itemPerThread;
	}
	
	if (waitUntilFinish)
	{
		while (data < end)
		{
			// work in this thread too
			int idx = size - (int)(end - data);
			f(idx, ctx, &vThreadsCache[MAX_THREADS]);
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
			TotalSize += vThreadsCache[i].Size;
		}
		
		pOutResult->resize(TotalSize);
		
		unsigned CopiedSize = 0;
		
		for (int i = 0; i < MAX_THREADS + 1; ++i)
		{
			if (vThreadsCache[i].Size == 0)
				continue;
			
			memcpy(&(*pOutResult)[CopiedSize], 
			       vThreadsCache[i].Data, 
			       sizeof(int) * vThreadsCache[i].Size);
			
			CopiedSize += vThreadsCache[i].Size;
			
			if (CopiedSize >= TotalSize)
				break;
		}
		
		// Do something with the thread cache?
	}
}