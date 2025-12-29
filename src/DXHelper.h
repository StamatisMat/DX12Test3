#pragma once

#include <stdexcept>
#include <exception>
#include <Windows.h>

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::runtime_error("HRESULT Error");
	}
}
