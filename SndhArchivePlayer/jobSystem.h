#pragma once
#include <atomic>

static const int	kMaxWorkers = 64;

class JobSystem
{
public:
	JobSystem();

	typedef bool (*processingFunction)(void* userContext, int index, int workerId);
	typedef bool (*completeFunction)(void* userContext, int workerId);
	int	RunJobs(void* userContext,
					int itemCount,
					processingFunction jobFunc,
					completeFunction completeFunc,
					int workersCount);

	bool Running() const { return m_running; }
	int Join();

	static int GetHardwareWorkerCount();

private:
	void	Start(int workerId);
	int m_runningWorkers;
	std::thread* m_workers[kMaxWorkers];

	void* m_userContext;
	int m_itemCount;
	std::atomic<int> m_itemIndex;
	std::atomic<int> m_itemSucceedCount;
	std::atomic<int> m_itemProceed;
	std::atomic<bool> m_running;
	processingFunction m_processingFunction;
	completeFunction m_completeFunction;
};





