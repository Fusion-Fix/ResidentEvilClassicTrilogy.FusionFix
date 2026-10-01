module;
#include <common.hxx>

export module ClassicMemory;

export namespace ClassicMemory
{
    template<typename T> T Read(const void* address, size_t offset = 0)
    {
        T value;
        memcpy(&value, static_cast<const uint8_t*>(address) + offset, sizeof(value));
        return value;
    }

    template<typename T> void Write(void* address, size_t offset, T value)
    {
        memcpy(static_cast<uint8_t*>(address) + offset, &value, sizeof(value));
    }
}
