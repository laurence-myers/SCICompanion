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

class PostBuildThread
{
public:
	PostBuildThread(const std::string &gameFolder);

	void Abort();

	friend std::shared_ptr<PostBuildThread> CreatePostBuildThread(const std::string &gameFolder);

private:
	void _Start(std::shared_ptr<PostBuildThread> myself);
	static UINT s_PostBuildThreadWorker(void *pParam);
	void _Main();

	HWND _hwndUI;
	std::shared_ptr<PostBuildThread> _myself;
	std::string _gameFolder;
	ScopedHandle _hAbort;
};

std::shared_ptr<PostBuildThread> CreatePostBuildThread(const std::string &gameFolder);

struct PostBuildRunResult
{
	bool launched = false;
	bool aborted = false;
};

// Runs a child process and streams its stdout/stderr to a sink. applicationName
// and commandLine map to CreateProcess's lpApplicationName / lpCommandLine; an
// empty string means nullptr. The parent copy of the pipe write end is closed
// before the read loop -- without that close the final ReadFile never sees EOF
// and the worker thread hangs forever (issue #48). onStart runs once after the
// child launches; onOutput runs for each chunk of output read. hAbort may be
// null; if it is signalled, the result reports aborted. Extracted from
// PostBuildThread::_Main so the pipe/process plumbing can be driven by a test
// with a benign child, away from the MFC UI.
PostBuildRunResult RunPostBuildProcess(
	const std::string &applicationName,
	const std::string &commandLine,
	const std::string &workingDir,
	HANDLE hAbort,
	const std::function<void()> &onStart,
	const std::function<void(const std::string &)> &onOutput);
