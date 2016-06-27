// --------------------------------------------
// Copyright KAPSARC. Open source MIT License.
// --------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2015 King Abdullah Petroleum Studies and Research Center
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// --------------------------------------------
// start of a quadratic map
//---------------------------------------------
#ifndef QUADMAP_MAIN_H
#define QUADMAP_MAIN_H

#include "qmutils.h"
//---------------------------------------------

using std::cout;
using std::endl;
using std::flush;

using KBase::PRNG;
using KGraph::Canvas;

//---------------------------------------------
namespace QuadMap { 

    
class QMApp {
public:
    explicit QMApp(uint64_t s);
    virtual ~QMApp();
    void run();
    void quit();
    PRNG* rng = nullptr;    
    static QMApp* theApp;
    
protected:
  
private:
    
};

}; // end of namespace

//---------------------------------------------
#endif
// --------------------------------------------
// Copyright KAPSARC. Open source MIT License.
// --------------------------------------------