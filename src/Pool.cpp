﻿/*
Copyright (c) 2013, Tolga HOŞGÖR
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met: 

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies, 
either expressed or implied, of the FreeBSD Project.
*/

#include "../include/Pool.h"
#include "../include/Worker.h"

namespace threadpool11
{
	Pool::Pool(WorkerCountType const& workerCount)
	{
		spawnWorkers(workerCount);
	}

	/*Worker& Pool::operator[](unsigned int i)
	{
		auto it = workers.begin();
		for(unsigned int num = 0; num < i; ++num)
		{
			++it;
		}
		return *it;
	}*/
	
	void Pool::postWork(Worker::WorkType const& work)
	{
		workerContainerMutex.lock();
		if(inactiveWorkers.size() > 0)
		{
			//std::cout << "Work posted." << std::endl;
			Worker* freeWorker = inactiveWorkers.front();
			activeWorkers.emplace_front(freeWorker);
			freeWorker->poolIterator = activeWorkers.begin();
			inactiveWorkers.pop_front();

			freeWorker->setWork(work);
		}
		else
		{
			//std::cout << "Work gone to enqueuedWork." << std::endl;
			enqueuedWorkMutex.lock();
			enqueuedWork.emplace_back(work);
			enqueuedWorkMutex.unlock();
		}
		workerContainerMutex.unlock();
	}

	void Pool::waitAll()
	{
		std::unique_lock<std::mutex> lock(notifyAllFinishedMutex);
		workerContainerMutex.lock();
		auto size = activeWorkers.size();
		workerContainerMutex.unlock();
		if(size != 0)
		{
			notifyAllFinished.wait(lock);
		}
	}

	void Pool::joinAll()
	{
		for(auto& it : workers)
		{
			it->terminate = true;
			std::unique_lock<std::mutex> lock(it->workPostedMutex);
			it->workPosted.notify_one();
			lock.unlock();
			it->thread.join();
		};
		workers.resize(0);
		inactiveWorkers.resize(0);
		//No mutex here since no threads left.
		enqueuedWork.resize(0);
	}

	Pool::WorkerCountType Pool::getActiveWorkerCount() const
	{
		std::lock_guard<std::mutex> l(workerContainerMutex);
		WorkerCountType size = activeWorkers.size();
		return size;
	}

	Pool::WorkerCountType Pool::getInactiveWorkerCount() const
	{
		std::lock_guard<std::mutex> l(workerContainerMutex);
		WorkerCountType size = inactiveWorkers.size();
		return size;
	}

	void Pool::increaseWorkerCountBy(WorkerCountType const& n)
	{
		std::lock_guard<std::mutex> l(workerContainerMutex);
		spawnWorkers(n);
	}

	void Pool::spawnWorkers(WorkerCountType const& n)
	{
		//'OR' makes sure the case where one of the variables is zero, is valid.
		assert(static_cast<WorkerCountType>(workers.size() + n) > n || static_cast<WorkerCountType>(workers.size() + n) > workers.size());
		workers.reserve(workers.size() + n);
		for(unsigned int i = 0; i < n; ++i)
		{
			workers.emplace_back(new Worker(this));
			inactiveWorkers.emplace_back(workers.back().get());
			std::unique_lock<std::mutex> lock(workers.back()->initMutex);
			if(workers.back()->init == 0)
			{
				workers.back()->initialized.wait(lock);
			}
		}
	}

	Pool::WorkerCountType Pool::decreaseWorkerCountBy(WorkerCountType n)
	{
		std::lock_guard<std::mutex> l(workerContainerMutex);
		n = std::min(n, inactiveWorkers.size());
		for(unsigned int i = 0; i < n; ++i)
		{
			Worker* worker = inactiveWorkers.front();
			inactiveWorkers.pop_front();
			worker->terminate = true;
			worker->workPosted.notify_one();
			worker->thread.join();
			for(auto& it = workers.begin(); it != workers.end(); ++it)
			{
				if(it->get() == worker)
				{
					workers.erase(it);
					break;
				}
			}
		}
		return n;
	}
}
