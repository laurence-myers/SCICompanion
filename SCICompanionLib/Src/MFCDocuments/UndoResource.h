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

//
// A generic undo for sci resources.
// _TBase is the baseclass of the MFC document object to which you want to add this functionality
// _TItem is the resource type
// ptrdiff_t is any extra info (like cursor position) you wish to include with the undo snapshot
//

#define MAX_UNDO 100
#define AGGRESSIVITY_UNDO 30

template <class _TBase, class _TItem>
class CUndoResource : public _TBase
{
private:
	struct UndoData
	{
		UndoData(std::unique_ptr<_TItem>&& theItem, ptrdiff_t theExtra, size_t theId)
		{
			item = std::move(theItem);
			extra = theExtra;
			id = theId;
		}

		std::unique_ptr<_TItem> item;
		ptrdiff_t extra;
		size_t id; // durable frame identity, independent of the heap address
	};

protected:
	// Implementors can override to attach extra data to a resource in the undo stack
	virtual ptrdiff_t v_GetExtra() { return 0; }

public:

	typedef std::list<UndoData> _MyListType;

	CUndoResource()
	{
		_pos = _undo.end();
		_nextId = 0;
		_lastSavedId = _cInvalidId;
	}

	void AddFirstResource(std::unique_ptr<_TItem> pResource, ptrdiff_t extra = 0)
	{
		ASSERT(_pos == _undo.end());
		size_t id = _nextId++;
		_lastSavedId = id;
		_undo.emplace_back(std::move(pResource), extra, id);
		_pos = _GetLastUndoFrame();
	}

	void AddNewResourceToUndo(std::unique_ptr<_TItem> pResourceNew, ptrdiff_t extra = 0)
	{
		// Delete all resources after the current one.
		_MyListType::iterator pos = _pos;
		++pos;
		// Now pos points to the position after the current one.
		while (pos != _undo.end())
		{
			// Delete all those resources.
			_MyListType::iterator posToDel = pos;
			++pos;
			_undo.erase(posToDel);
		}

		// Insert after the current pos (which is now the end), and make this our new pos.
		_undo.emplace_back(std::move(pResourceNew), extra, _nextId++);
		_pos = _GetLastUndoFrame();

		// Make sure we don't grow infinitely.
		_TrimUndoStack();
	}

	// Backs out the frame most recently added by AddNewResourceToUndo -- e.g. a
	// preview clone that turned out to make no change. OnUndo restores the previous
	// frame and refreshes the views; then the clone is erased, so it is not left as
	// a phantom redo frame (which would otherwise make Redo a no-op that still
	// fires a full view refresh).
	void RemoveLastResourceFromUndo()
	{
		if (_undo.empty())
		{
			return;
		}
		typename _MyListType::iterator last = _GetLastUndoFrame();
		OnUndo();
		// OnUndo moved _pos off the clone (unless the clone was the only frame, in
		// which case it stays current and we leave it -- no phantom redo either way).
		if (_pos != last)
		{
			_undo.erase(last);
		}
	}

	void SetExtra(ptrdiff_t extra)
	{
		if (_pos != _undo.end())
		{
			_pos->extra = extra;
		}
	}

	void SetLastSaved(const _TItem *pResource)
	{
		// Record the saved frame by its durable id, looked up while the pointer
		// is still live (called right after a save). A raw pointer would dangle
		// once the frame is trimmed or its redo tail is erased, and a reused
		// heap address could then falsely match an unsaved frame.
		_lastSavedId = _cInvalidId;
		for (const UndoData &frame : _undo)
		{
			if (frame.item.get() == pResource)
			{
				_lastSavedId = frame.id;
				break;
			}
		}
	}

	// Gets the resource at the current location
	const _TItem *GetResource() const
	{
		return (_pos != _undo.end()) ? (*_pos).item.get() : nullptr;
	}

	// Gets the extra data associated with the resource at the current location
	ptrdiff_t GetExtra() const
	{
		return (_pos != _undo.end()) ? (*_pos).extra : 0;
	}

	bool HasUndos()
	{
		return !_undo.empty();
	}

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg void OnUndo()
	{
		if (_pos != _undo.begin())
		{
			--_pos;
			_OnUndoRedo();
			SetModifiedFlag((*_pos).id != _lastSavedId);
		}
	}

	afx_msg void OnRedo()
	{
		if (_pos != _GetLastUndoFrame())
		{
			++_pos;
			_OnUndoRedo();
			SetModifiedFlag((*_pos).id != _lastSavedId);
		}
	}

	afx_msg void OnUpdateRedo(CCmdUI *pCmdUI)
	{
		BOOL fRet = FALSE;
		if (!_undo.empty() && !_PreventUndos())
		{
			fRet = (_pos != _GetLastUndoFrame());
		}
		pCmdUI->Enable(fRet);
	}

	afx_msg void OnUpdateUndo(CCmdUI *pCmdUI)
	{
		BOOL fRet = FALSE;
		if (!_undo.empty() && !_PreventUndos())
		{
			fRet = (_pos != _undo.begin());
		}
		pCmdUI->Enable(fRet);
	}

	// Overrides
	virtual bool v_PreventUndos() const
	{
		return false;
	}

	virtual void v_OnUndoRedo() = 0;

private:
	bool _PreventUndos()
	{
		return v_PreventUndos();
	}

	void _OnUndoRedo()
	{
		v_OnUndoRedo();
	}

	void _TrimUndoStack()
	{
		if (_undo.size() > MAX_UNDO)
		{
			// Remove every other stored frame, up to a certain point.
			_MyListType::iterator pos = _undo.begin();
			BOOL fRemove = FALSE;
			for (int i = 0; i < AGGRESSIVITY_UNDO; i++)
			{
				if (pos != _pos) // Skip it if it's where we currently are!
				{
					_MyListType::iterator posToMaybeDelete = pos;
					++pos;
					if (fRemove)
					{
						_undo.erase(posToMaybeDelete);
					}
					fRemove = !fRemove;
				}
			}
		}
	}

	typename _MyListType::iterator _GetLastUndoFrame()
	{
		_MyListType::iterator pos = _undo.end();
		return --pos;
	}

	// Undo buffer.
	_MyListType _undo;
	typename _MyListType::iterator _pos;

	static const size_t _cInvalidId = (size_t)-1;
	size_t _lastSavedId; // id of the saved frame, or _cInvalidId if none is reachable
	size_t _nextId;      // monotonically increasing per-frame id source
};

BEGIN_TEMPLATE_MESSAGE_MAP_2(CUndoResource, _TBase, _TItem, _TBase)
	ON_COMMAND(ID_EDIT_UNDO, OnUndo)
	ON_COMMAND(ID_EDIT_REDO, OnRedo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, OnUpdateUndo)
	ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, OnUpdateRedo)
END_MESSAGE_MAP()
