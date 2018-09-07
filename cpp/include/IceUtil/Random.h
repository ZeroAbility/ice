// **********************************************************************
//
// Copyright (c) 2003-2018 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef ICE_UTIL_RANDOM_H
#define ICE_UTIL_RANDOM_H

#include <IceUtil/Config.h>
#include <IceUtil/Exception.h>

#ifdef ICE_CPP11_COMPILER
#   include <algorithm>
#   include <random>
#endif

namespace IceUtilInternal
{

ICE_API void generateRandom(char*, size_t);
ICE_API unsigned int random(int = 0);

#ifdef ICE_CPP11_COMPILER

template<class T>
void shuffle(T first, T last)
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(first, last, rng);
}

#else

struct RandomNumberGenerator : public std::unary_function<std::ptrdiff_t, std::ptrdiff_t>
{
    std::ptrdiff_t operator()(std::ptrdiff_t d)
    {
        return IceUtilInternal::random(static_cast<int>(d));
    }
};

template<class T>
void shuffle(T first, T last)
{
    RandomNumberGenerator rng;
    random_shuffle(first, last, rng);
}

#endif

}

#endif
