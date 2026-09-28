#include "stdafx.h"
#include "CrystalScriptStream.h"
#include "CCrystalTextBuffer.h"

// The parser stream over the editor's text buffer. It is in the GUI library;
// the core library has the rest of the stream (CrystalScriptStream.cpp).

CPoint GetNaturalLimit(CCrystalTextBuffer *pBuffer)
{
	CPoint limit;
	limit.y = pBuffer->GetLineCount() - 1;
	if (limit.y >= 0)
	{
		limit.x = pBuffer->GetLineLength(limit.y);
	}
	return limit;
}

ReadOnlyTextBuffer::ReadOnlyTextBuffer(CCrystalTextBuffer *pBuffer) : ReadOnlyTextBuffer(pBuffer, GetNaturalLimit(pBuffer), 0) {}

ReadOnlyTextBuffer::ReadOnlyTextBuffer(CCrystalTextBuffer *pBuffer, CPoint limit, int extraSpace)
{
	TextPos textLimit;
	textLimit.line = limit.y;
	textLimit.column = limit.x;
	_Init([pBuffer](int nLine) { return pBuffer->GetLineLength(nLine); }, [pBuffer](int nLine) { return pBuffer->GetLineChars(nLine); }, textLimit, extraSpace);
}

CScriptStreamLimiter::CScriptStreamLimiter(CCrystalTextBuffer *pBuffer)
{
	_pBuffer = std::make_unique<ReadOnlyTextBuffer>(pBuffer);
	_pCallback = nullptr;
	_fCancel = false;
}

CScriptStreamLimiter::CScriptStreamLimiter(CCrystalTextBuffer *pBuffer, CPoint ptLimit, int extraSpace)
{
	_pBuffer = std::make_unique<ReadOnlyTextBuffer>(pBuffer, ptLimit, extraSpace);
	_pCallback = nullptr;
	_fCancel = false;
}
