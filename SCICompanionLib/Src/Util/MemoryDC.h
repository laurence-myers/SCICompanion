#pragma once

// A memory device context (CreateCompatibleDC), deleted at the end of its
// scope. Plain Win32, for the core library.
class MemoryDC
{
public:
    MemoryDC() : _hdc(CreateCompatibleDC(nullptr)) {}
    ~MemoryDC()
    {
        if (_hdc)
        {
            DeleteDC(_hdc);
        }
    }
    MemoryDC(const MemoryDC &) = delete;
    MemoryDC &operator=(const MemoryDC &) = delete;
    operator HDC() const { return _hdc; }

private:
    HDC _hdc;
};