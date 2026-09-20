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
#include "stdafx.h"
#include "PostBuildThread.h"
#include "format.h"
#include "AppState.h"
#include "MainFrm.h"

using namespace std;

std::vector<std::string> split(const std::string& value, char separator);

shared_ptr<PostBuildThread> CreatePostBuildThread(const std::string &gameFolder)
{
	shared_ptr<PostBuildThread> postBuild = make_shared<PostBuildThread>(gameFolder);
	postBuild->_Start(postBuild);
	return postBuild;
}

UINT PostBuildThread::s_PostBuildThreadWorker(void *pParam)
{
	static_cast<PostBuildThread*>(pParam)->_Main();
	return 0;
}

string GetPostBuildFilename(const string &gameFolder)
{
	return gameFolder + "\\PostRepackage.cmd";
}

void PostBuildThread::Abort()
{
	SetEvent(_hAbort.hFile);
	// Hmm... not sure about this. Who is responsible for cleanup?
}

PostBuildThread::PostBuildThread(const string &gameFolder) : _gameFolder(gameFolder), _hwndUI(nullptr) {}

void PostBuildThread::_Start(std::shared_ptr<PostBuildThread> myself)
{
	if (PathFileExists(GetPostBuildFilename(_gameFolder).c_str()))
	{
		_hwndUI = AfxGetMainWnd()->GetSafeHwnd();
		_hAbort.hFile = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		_myself = myself;

		try
		{
			std::thread ourThread = std::thread(s_PostBuildThreadWorker, this);
			ourThread.detach();   // Thread deletes itself when done.
		}
		catch (std::system_error)
		{
			_hwndUI = nullptr;
			// Close() invalidates the handle, so the ScopedHandle destructor does
			// not close it a second time. A bare CloseHandle(_hAbort.hFile) here
			// would leave the stale value for the destructor to re-close.
			_hAbort.Close();
			_myself = nullptr;
		}
	}
}


static void _DrainPipeChunk(HANDLE handle, const std::function<void(const std::string &)> &onOutput)
{
	DWORD cbRead;
	char buffer[1024];

	// A blocking read on the pipe. It returns the next chunk, or zero bytes at
	// end-of-file once every write handle is closed -- including the parent copy
	// of the write end, which RunPostBuildProcess closes before this loop runs.
	if (ReadFile(handle, buffer, sizeof(buffer), &cbRead, nullptr) && (cbRead != 0))
	{
		if (onOutput)
		{
			onOutput(std::string(buffer, cbRead));
		}
	}
}

PostBuildRunResult RunPostBuildProcess(
	const std::string &applicationName,
	const std::string &commandLine,
	const std::string &workingDir,
	HANDLE hAbort,
	const std::function<void()> &onStart,
	const std::function<void(const std::string &)> &onOutput)
{
	PostBuildRunResult result;

	SECURITY_ATTRIBUTES saAttr = {};
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;   // the child inherits the write end
	saAttr.lpSecurityDescriptor = nullptr;

	ScopedHandle childOutRead;
	ScopedHandle childOutWrite;
	if (!CreatePipe(&childOutRead.hFile, &childOutWrite.hFile, &saAttr, 0))
	{
		return result;
	}

	// The parent read end must not be inherited by the child.
	SetHandleInformation(childOutRead.hFile, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION procInfo = {};
	STARTUPINFO startInfo = {};
	startInfo.cb = sizeof(startInfo);
	startInfo.hStdError = childOutWrite.hFile;
	startInfo.hStdOutput = childOutWrite.hFile;
	startInfo.hStdInput = nullptr;
	startInfo.dwFlags |= STARTF_USESTDHANDLES;
	startInfo.wShowWindow = SW_HIDE;
	startInfo.dwFlags |= STARTF_USESHOWWINDOW;

	// CreateProcess may write to the lpCommandLine buffer, so give it a mutable one.
	std::string mutableCmd = commandLine;
	if (CreateProcess(
		applicationName.empty() ? nullptr : applicationName.c_str(),
		mutableCmd.empty() ? nullptr : &mutableCmd[0],
		nullptr,	// process security attributes
		nullptr,	// primary thread security attributes
		TRUE,	   // inherit handles
		CREATE_SUSPENDED,	// created suspended so it joins the Job before it can spawn children (#127)
		nullptr,	// use the parent's environment
		workingDir.empty() ? nullptr : workingDir.c_str(),
		&startInfo,
		&procInfo))
	{
		result.launched = true;

		// Own the process and thread handles here so any throw from a sink
		// callback below cannot leak them or orphan the child.
		ScopedHandle hProcess;
		ScopedHandle hThread;
		hProcess.hFile = procInfo.hProcess;
		hThread.hFile = procInfo.hThread;

		// Put the child in a Job object so an abort can stop the whole process
		// tree, not just the top process. A post-build step is usually a .cmd
		// that launches other tools; terminating only the top process would
		// leave those running and still writing files (#127). The child was
		// created suspended, so it is assigned to the Job before it can spawn
		// anything. JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE also stops the tree if
		// this function leaves early for any reason.
		ScopedHandle job;
		bool jobReady = false;
		job.hFile = CreateJobObject(nullptr, nullptr);
		if (job.hFile != nullptr)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (SetInformationJobObject(job.hFile, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
				AssignProcessToJobObject(job.hFile, hProcess.hFile))
			{
				jobReady = true;
			}
		}
		// Resume the child regardless: if the Job could not be set up, it still
		// runs (without the tree-kill guarantee) rather than hanging suspended.
		ResumeThread(hThread.hFile);

		// Close the parent copy of the write end BEFORE the read loop. A blocking
		// ReadFile reports EOF only when every write handle is closed. The child
		// closes its inherited copy when it exits, but while the parent keeps this
		// copy open the final ReadFile never sees EOF and the worker hangs. This
		// close is the fix for issue #48.
		childOutWrite.Close();

		if (onStart)
		{
			onStart();
		}

		DWORD waitResult;
		HANDLE waitHandles[2] = { hProcess.hFile, hAbort };
		DWORD handleCount = (hAbort != nullptr) ? 2 : 1;
		// Poll interval for re-checking the pipe for output while waiting on the
		// process and the abort event. It bounds how long an abort can be deferred.
		const DWORD kPollMs = 50;
		// Loop: drain whatever output is available WITHOUT blocking, then wait for
		// the process to exit or the abort to signal (or a short timeout, to
		// re-check for more output). A blocking ReadFile here would ignore hAbort
		// while a child runs but writes nothing, so a silent step could not be
		// aborted (issue #83). PeekNamedPipe bounds each read to the bytes already
		// present, so a read never blocks waiting for a silent child.
		do
		{
			DWORD bytesAvailable = 0;
			while (PeekNamedPipe(childOutRead.hFile, nullptr, 0, nullptr, &bytesAvailable, nullptr) &&
				(bytesAvailable != 0))
			{
				_DrainPipeChunk(childOutRead.hFile, onOutput);
			}
			waitResult = WaitForMultipleObjects(handleCount, waitHandles, FALSE, kPollMs);
		} while (waitResult == WAIT_TIMEOUT);

		result.aborted = (hAbort != nullptr) && (waitResult == (WAIT_OBJECT_0 + 1));

		if (result.aborted)
		{
			// Terminate the child and everything it spawned before returning, so a
			// .cmd that launched other tools does not keep running and writing
			// files after the abort (#127). The killed processes then close their
			// write ends, so the drain below sees the last output and then EOF.
			if (jobReady)
			{
				// Preferred: the Job kills the whole tree atomically.
				TerminateJobObject(job.hFile, 1);
			}
			else
			{
				// The Job could not be set up -- this happens when the host process
				// is already in a Job that does not allow a nested one (for example
				// under some test runners). Fall back to walking the process tree by
				// PID and terminating each process.
				TerminateProcessTree(hProcess.hFile, 1);
			}
		}

		// Drain any output still buffered in the pipe, without blocking. On a
		// normal exit the child's write end is closed and PeekNamedPipe reports
		// the remaining buffered output; on an abort the tree was just terminated,
		// so the same drain captures the final bytes. (The parent write end was
		// already closed above -- the #48 fix.)
		DWORD bytesAvailable = 0;
		while (PeekNamedPipe(childOutRead.hFile, nullptr, 0, nullptr, &bytesAvailable, nullptr) &&
			(bytesAvailable != 0))
		{
			_DrainPipeChunk(childOutRead.hFile, onOutput);
		}
	}

	return result;
}

void PostBuildThread::_Main()
{
	// Own a reference to ourselves during the lifetime of this method.
	shared_ptr<PostBuildThread> myself = _myself;
	_myself.reset();

	const HWND hwndUI = _hwndUI;
	const std::string cmdPath = GetPostBuildFilename(_gameFolder);

	// Report the start once the child launches.
	auto onStart = [hwndUI, cmdPath]()
	{
		if (hwndUI)
		{
			unique_ptr<vector<CompileResult>> results = make_unique<vector<CompileResult>>();
			results->push_back(CompileResult("Running " + cmdPath));
			SendMessage(hwndUI, UWM_RESULTS, (WPARAM)OutputPaneType::Compile, reinterpret_cast<LPARAM>(results.release()));
		}
	};

	// Forward each chunk of child output to the compile pane, one line per entry.
	auto onOutput = [hwndUI](const std::string &chunk)
	{
		if (hwndUI)
		{
			std::vector<std::string> lines = split(chunk, '\n');
			unique_ptr<vector<CompileResult>> results = make_unique<vector<CompileResult>>();
			for (const std::string &line : lines)
			{
				results->push_back(CompileResult(line));
			}
			SendMessage(hwndUI, UWM_RESULTS, (WPARAM)OutputPaneType::Compile, reinterpret_cast<LPARAM>(results.release()));
		}
	};

	PostBuildRunResult result = RunPostBuildProcess(cmdPath, "", _gameFolder, _hAbort.hFile, onStart, onOutput);

	if (hwndUI && result.launched)
	{
		unique_ptr<vector<CompileResult>> results = make_unique<vector<CompileResult>>();
		results->push_back(CompileResult(result.aborted ? "Aborted" : "Completed " + cmdPath));
		SendMessage(hwndUI, UWM_RESULTS, (WPARAM)OutputPaneType::Compile, reinterpret_cast<LPARAM>(results.release()));
	}
}
