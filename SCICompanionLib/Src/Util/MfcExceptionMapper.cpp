#include "stdafx.h"
#include "MfcExceptionMapper.h"

bool MapMfcException(sci::Error &error)
{
    try
    {
        throw;
    }
    catch (CException *e)
    {
        // Delete the MFC exception on every path, also if building the
        // text below throws.
        struct DeleteOnExit
        {
            CException *exception;
            ~DeleteOnExit() { exception->Delete(); }
        } deleteOnExit = { e };
        if (e->IsKindOf(RUNTIME_CLASS(CMemoryException)))
        {
            // In an MFC program a failed new throws this, not
            // std::bad_alloc, and it has no text.
            error.code = sci::ErrorCode::Internal;
            error.message = "out of memory";
        }
        else
        {
            TCHAR text[512] = {};
            BOOL hasText = e->GetErrorMessage(text, ARRAYSIZE(text));
            error.code = e->IsKindOf(RUNTIME_CLASS(CFileException)) ? sci::ErrorCode::Io : sci::ErrorCode::Internal;
            error.message = (hasText && text[0]) ? std::string(text) : std::string("MFC exception (no text)");
        }
        return true;
    }
    catch (...)
    {
        // Not an MFC exception: the boundary gives it its own text.
        return false;
    }
}

void InstallMfcExceptionMapper()
{
    sci::SetForeignExceptionMapper(MapMfcException);
}
