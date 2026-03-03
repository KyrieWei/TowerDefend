// Compatibility shim for deprecated UE templates (UE 5.7+)
// Provides replacements for TChooseClass and TIsTriviallyDestructible
// using standard C++ equivalents.

#pragma once

#include "CoreTypes.h"
#include <type_traits>

// TChooseClass replacement using std::conditional
// Usage: typename UnLuaChooseClass<Pred, A, B>::Result
template<bool Predicate, typename TrueClass, typename FalseClass>
struct UnLuaChooseClass
{
	typedef typename std::conditional<Predicate, TrueClass, FalseClass>::type Result;
};
