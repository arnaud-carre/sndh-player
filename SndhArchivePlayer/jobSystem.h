#pragma once
#include <atomic>

static const int	kMaxWorkers = 64;

class JobSystem
{
public:
	JobSystem();

	typedef bool (*processingFunction)(void* firstItem, int index, int workerId);
	int	RunJobs(void* items,
					int itemCount,
					processingFunction func,
					int workersCount = 0);

	bool Working() const;
	int Join();

	static int GetHardwareWorkers(int maxWorkers=64);

private:
	void	Start(int workerId);
	int m_runningWorkers;
	std::thread* m_workers[kMaxWorkers];

	void* m_items;
	int m_itemCount;
	std::atomic<int> m_itemIndex;
	std::atomic<int> m_itemSucceedCount;
	processingFunction m_processingFunction;
};
