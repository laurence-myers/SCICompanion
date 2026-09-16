/***************************************************************************
	Copyright (c) 2020 Philip Fortier

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
***************************************************************************/
#pragma once

#include <atomic>
#include <deque>

class ITaskStatus
{
public:
	virtual bool IsAborted() = 0;
};

template<typename _TPayload, typename _TResponse = int>
class BackgroundScheduler : public ITaskStatus
{
public:
	BackgroundScheduler() : BackgroundScheduler(nullptr, 0) {}

	BackgroundScheduler(HWND hwndResponse, UINT msgResponse) : _exit(false), _nextId(0), _hwndResponse(hwndResponse), _msgResponse(msgResponse)
	{
		_thread = std::thread(s_ThreadWorker, this);
	}

	~BackgroundScheduler()
	{
		Exit();
	}

	int SubmitTask(std::unique_ptr<_TPayload> task, std::function<std::unique_ptr<_TResponse>(ITaskStatus&, _TPayload&)> func)
	{
		int id;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			id = _nextId++;
			_queue.emplace_back(id, std::move(task), func);
		}

		_conditionWakeUp.notify_one();
		return id;
	}
	int SubmitTask(HWND hwnd, UINT msg, std::unique_ptr<_TPayload> task, std::function<std::unique_ptr<_TResponse>(ITaskStatus&, _TPayload&)> func)
	{
		// This allows us to keep the same scheduler around for different windows.
		// Guard _hwndResponse/_msgResponse with _mutexResponse -- the SAME mutex the
		// worker reads them under in _DoWork and that DeactivateHWND clears them
		// under -- so every access agrees on one mutex (#92). _mutex guards the task
		// queue, a separate concern; the two are never held at once, so there is no
		// lock-ordering hazard.
		{
			std::lock_guard<std::mutex> lock(_mutexResponse);
			_hwndResponse = hwnd;
			_msgResponse = msg;
		}

		return SubmitTask(std::move(task), func);
	}

	std::unique_ptr<_TResponse> RetrieveResponse(int id)
	{

		std::lock_guard<std::mutex> lock(_mutexResponse);
		// Pop off the front until we find the one we want.
		std::unique_ptr<_TResponse> response;
		while (!response && !_responseQueue.empty())
		{
			if (_responseQueue.front().id == id)
			{
				response = move(_responseQueue.front().response);
			}
			_responseQueue.pop_front();
		}
		return response;
	}

	void DeactivateHWND(HWND hwndNoMore)
	{
		// Since multiple windows may use the same scheduler, when a window that submits
		// as task is destroyed, we want to clear the response hwnd out so that we don't
		// post to an invalid hwnd. Take _mutexResponse: this runs on the UI thread
		// while the worker may be reading _hwndResponse in _DoWork, so without the
		// lock the clear races the worker's read -- a torn/stale pointer and, worst
		// case, a PostMessage to a destroyed window (#92).
		std::lock_guard<std::mutex> lock(_mutexResponse);
		if (_hwndResponse == hwndNoMore)
		{
			_hwndResponse = nullptr;
		}
	}

	bool IsAborted() override
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _exit;
	}

	// Waits until background thread exits.
	void Exit()
	{
		bool deleteThings = false;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (!_exit)
			{
				deleteThings = true;
				_exit = true;
			}
		}

		if (deleteThings)
		{
			_conditionWakeUp.notify_one();

			// We need to wait until the background thread is done.
			_thread.join();
		}
	}

private:
	static UINT s_ThreadWorker(void *pParam)
	{
		(reinterpret_cast<BackgroundScheduler*>(pParam))->_DoWork();
		return 0;
	}

	void _DoWork()
	{
		while (!_exit)
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_conditionWakeUp.wait(lock, [&]() { return this->_exit || !this->_queue.empty(); });
			if (!_exit)
			{
				// Now the mutex is locked again.
				assert(!this->_queue.empty());
				std::unique_ptr<_TPayload> payload = std::move(_queue.front().payload);
				std::function<std::unique_ptr<_TResponse>(ITaskStatus&, _TPayload&)> func = _queue.front().func;
				int id = _queue.front().id;
				_queue.pop_front();
				// But we'll unlock it while we do our heavy work. Unlock through the
				// unique_lock so its ownership state stays consistent.
				lock.unlock();

				if (payload)
				{
					try
					{
						std::unique_ptr<_TResponse> response = func(*this, *payload);
						// If the owner wanted a response, send it now.
						if (response)
						{
							HWND hwnd;
							UINT msg;
							{
								std::lock_guard<std::mutex> lock(_mutexResponse);
								hwnd = _hwndResponse;
								msg = _msgResponse;
								if (_hwndResponse)
								{
									_responseQueue.emplace_back(id, std::move(response));
								}
							}
							if (hwnd)
							{
								PostMessage(hwnd, msg, 0, 0);
							}
						}
					}
					catch (...)
					{
						// A task -- or handling its response -- must not take down the
						// worker thread: an exception escaping here would propagate out
						// of the thread function, an unconditional std::terminate.
					}
				}
				// No re-lock needed: the lock is already released, and the next
				// iteration's unique_lock reacquires _mutex fresh.
			}
		}
	}

	std::thread _thread;

	// REVIEW: these were auto reset...
	std::condition_variable _conditionWakeUp;

	// Atomic so the worker loop can test it without holding _mutex (the loop
	// condition below reads it outside the lock). It is still written under
	// _mutex in Exit(), so the check-then-set there stays a unit.
	std::atomic<bool> _exit;

	struct TaskInfo
	{
		TaskInfo(int id, std::unique_ptr<_TPayload> payload, std::function<std::unique_ptr<_TResponse>(ITaskStatus&, _TPayload&)> func) : id(id), payload(std::move(payload)), func(func) {}

		int id;
		std::function<std::unique_ptr<_TResponse>(ITaskStatus&, _TPayload&)> func;
		std::unique_ptr<_TPayload> payload;
	};

	struct TaskResponse
	{
		TaskResponse(int id, std::unique_ptr<_TResponse> response) : id(id), response(std::move(response)) {}

		int id;
		std::unique_ptr<_TResponse> response;
	};

	std::mutex _mutex;
	int _nextId;
	std::deque<TaskInfo> _queue;

	std::mutex _mutexResponse;
	HWND _hwndResponse;
	UINT _msgResponse;
	std::deque<TaskResponse> _responseQueue;
};

#include <future>
#include <thread>
#include <mutex>
#include <memory>
#include <cassert>

// Runs a function on a background thread and posts a message to a window when it
// finishes. On Abandon (called from the destructor when the owning dialog closes),
// it stops depending on the window and detaches the worker, so closing the dialog
// neither blocks the UI thread nor posts to a window that no longer exists. The
// worker's state is heap-owned (a shared_ptr the worker holds by value), so a
// detached worker that finishes later writes only to memory that outlives the
// sink -- there is no use-after-free. The work function must copy everything it
// needs (capture by value), because the worker may outlive the owning dialog. (#53)
template<typename _TResponse>
class CWndTaskSink
{
public:
	// pwnd guaranteed to exist as long as CWndTaskSink does.
	CWndTaskSink(CWnd *pwnd, UINT message) : _pwnd(pwnd), _message(message) {}
	CWndTaskSink(const CWndTaskSink &) = delete;
	CWndTaskSink &operator=(const CWndTaskSink &) = delete;

	~CWndTaskSink()
	{
		Abandon();
		if (_thread.joinable())
		{
			// Do not block the UI thread waiting for the work; the heap-owned state
			// keeps the detached worker safe.
			_thread.detach();
		}
	}

	template<typename _TFunc>
	void StartTask(_TFunc func)
	{
		// Abandon and detach any previous run. Callers start one at a time, but a
		// joinable std::thread must not be overwritten (that would std::terminate).
		Abandon();
		if (_thread.joinable())
		{
			_thread.detach();
		}

		auto state = std::make_shared<SharedState>();
		state->hwnd = _pwnd->GetSafeHwnd();
		state->message = _message;
		state->runId = ++_nextRunId;
		_state = state;

		_thread = std::thread([state, func]()
		{
			try
			{
				// Compute the result first (the work is the slow part), then publish
				// it under the lock.
				std::unique_ptr<_TResponse> response = std::make_unique<_TResponse>(func());
				std::lock_guard<std::mutex> lock(state->mutex);
				state->response = std::move(response);
				// Post only if the owner still wants the result. Abandon nulls the
				// hwnd, so a closed dialog is never posted to. Posting to a stale HWND
				// (a worker that finishes in the small gap before Abandon) merely
				// returns FALSE -- MFC has already detached it, so no message reaches
				// a destroyed window.
				if (state->hwnd != nullptr)
				{
					// Tag the completion with this run's id so GetResponse can ignore a
					// stale completion from a superseded run. (#112)
					::PostMessage(state->hwnd, state->message, static_cast<WPARAM>(state->runId), 0);
				}
			}
			catch (...)
			{
				// A work function that throws must not escape the thread's top-level
				// function -- that is an unconditional std::terminate (see the same
				// guard in BackgroundScheduler). Match the previous std::async
				// behavior: leave the response unset and post nothing, so the failure
				// is not delivered rather than crashing the application. (#53)
			}
		});
	}

	void Abandon()
	{
		if (_state)
		{
			std::lock_guard<std::mutex> lock(_state->mutex);
			_state->hwnd = nullptr;
		}
	}

	// Call from the message handler after the task posts its completion message,
	// passing that message's wParam (the run id StartTask tagged it with). The
	// worker stores the response before it posts, so it is ready here.
	//
	// Returns a default-constructed _TResponse (a "no result" sentinel) instead of
	// crashing when there is nothing valid to return: before any task has run
	// (_state is null, e.g. a stray message), when the completion is from a
	// superseded run (its id does not match the current run), or when the response
	// is not set. Callers that run one task at a time and handle each completion
	// before starting the next always get the real result. (#112)
	_TResponse GetResponse(WPARAM completionRunId)
	{
		if (!_state)
		{
			return _TResponse{};
		}
		std::lock_guard<std::mutex> lock(_state->mutex);
		if ((static_cast<UINT>(completionRunId) != _state->runId) || !_state->response)
		{
			return _TResponse{};
		}
		// Consume the response: reset it after the move so a duplicate completion for
		// the same run returns the sentinel rather than a moved-from value. (#112)
		_TResponse result = std::move(*_state->response);
		_state->response.reset();
		return result;
	}

private:
	struct SharedState
	{
		std::mutex mutex;
		HWND hwnd = nullptr;
		UINT message = 0;
		UINT runId = 0;
		std::unique_ptr<_TResponse> response;
	};

	std::shared_ptr<SharedState> _state;
	std::thread _thread;
	CWnd *_pwnd;
	UINT _message;
	UINT _nextRunId = 0;   // incremented per StartTask; tags each completion (#112)
};
