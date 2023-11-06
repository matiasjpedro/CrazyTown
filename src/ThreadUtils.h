#pragma once
#include <thread>
#include <functional>

#define MAX_THREADS 32

template<typename T>
static void ExecuteParallel(const int numThreads, T* data, const int size, std::function<void(int, T*, void*)> f, void* ctx, bool waitUntilFinish = true)
{
	const int itemPerThread = size / (numThreads + waitUntilFinish);
	const T* end = data + size;

	auto threadJob = [f, itemPerThread, end, ctx, size](T* data) -> void
	{
		for (int i = 0; i < itemPerThread; ++i) {
			int idx = size - (int)(end - &data[i]);
			f(idx, &data[i], ctx);
		}
			
	};

	std::thread threads[MAX_THREADS];

	for (int i = 0; i < numThreads; ++i)
	{
		new(threads + i)std::thread(threadJob, data);
		data += itemPerThread;
	}
	if (waitUntilFinish)
	{
		while (data < end)
		{ // work in this thread too
			int idx = size - (int)(end - data);
			f(idx, data++, ctx);
		}
		// wait until all threads finished
		for (int i = 0; i < numThreads; ++i)
		{
			threads[i].join();
		}
	}
}