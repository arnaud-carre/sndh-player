#include <thread>
#include <assert.h>
#include <time.h>
#include "jobSystem.h"


JobSystem::JobSystem()
{
	m_runningWorkers = 0;
}

int JobSystem::GetHardwareWorkers(int maxWorkers)
{
	int count = std::thread::hardware_concurrency();
	if ( 0 == count )
		count = 1;
	if (count > maxWorkers)
		count = maxWorkers;
	return count;
}

int	JobSystem::RunJobs(void* items, int itemCount, processingFunction func, int workersCount)
{
	m_itemSucceedCount = 0;

	if (0 == itemCount)
		return 0;

	if (0 == workersCount)
	{
		workersCount = std::thread::hardware_concurrency();
		if ( 0 == workersCount )
			workersCount = 1;
	}

	if (workersCount > itemCount)
		workersCount = itemCount;
	if (workersCount > kMaxWorkers)
		workersCount = kMaxWorkers;

//	clock_t t0 = clock();

	m_items = items;
	m_itemCount = itemCount;
	m_itemIndex = 0;
	m_itemSucceedCount = 0;
	m_processingFunction = func;

	m_runningWorkers = workersCount;

	for (int t = 0; t < workersCount; t++)
		m_workers[t] = new std::thread([this,t] { Start(t); });

	return workersCount;
}

int JobSystem::Join()
{

	// wait for all threads to finish
	for (int t = 0; t < m_runningWorkers; t++)
	{
		m_workers[t]->join();
		delete m_workers[t];
	}

	//	clock_t t1 = clock();
	//	float t = float(t1 - t0) / float(CLOCKS_PER_SEC);
	//	printf("%d jobs succeed in %.02f sec\n", m_itemSucceedCount.load(), t);

	return m_itemSucceedCount;

}

bool JobSystem::Working() const
{
	if (0 == m_runningWorkers)
		return false;

	return (m_itemIndex < m_itemCount);
}

// Grab jobs as fast as possible
void JobSystem::Start(int workerId)
{
	for (;;)
	{
		const int id = m_itemIndex.fetch_add(1);
		if (id >= m_itemCount)
			break;

		if (m_processingFunction(m_items, id, workerId))
			m_itemSucceedCount.fetch_add(1);
	}
}