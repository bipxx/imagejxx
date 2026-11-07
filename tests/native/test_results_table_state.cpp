#include <imagejxx/results_table_state.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool v){if(!v)throw std::runtime_error("Results table allocation contract failed");}
template<class E,class F>void refuses(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught);}
void same(std::optional<double> actual,double expected){check(actual.has_value());check(std::isnan(expected)?std::isnan(*actual):*actual==expected);}
}
int main()try {
  const double nan=std::numeric_limits<double>::quiet_NaN();
  struct Cell{std::size_t row;double early,late;};
  struct Case{bool initial;int toggle,late;double next_early,next_late;std::array<Cell,10> cells;};
  const Case cases[]={
    {false,-1,0,0,0,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,-1,1,0,0,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,-1,49,0,0,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,-1,99,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,-1,100,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,-1,199,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {false,0,0,nan,nan,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,0,1,nan,nan,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,0,49,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,0,99,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,0,100,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,0,199,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,1,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,1,1,nan,nan,{{{0,1.25,nan},{1,0,7.25},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,1,49,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,7.25},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,1,99,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,1,100,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,1,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,49,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,49,1,nan,nan,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,49,49,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,7.25},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,49,99,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,49,100,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,49,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,98,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,98,1,nan,nan,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,98,49,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,98,99,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,98,100,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,98,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,99,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,99,1,nan,nan,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,99,49,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,99,99,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,99,100,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,99,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,100,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,100,1,nan,nan,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,100,49,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,100,99,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,7.25},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,100,100,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,0,7.25},{198,0,nan},{199,0,nan},{200,nan,nan},{204,nan,nan}}}},
    {false,100,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,0,nan},{198,0,nan},{199,0,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,198,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,198,1,nan,nan,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,198,49,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,198,99,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,7.25},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,198,100,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,7.25},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,198,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,0,nan},{198,0,nan},{199,0,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,199,0,nan,nan,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,199,1,nan,nan,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,199,49,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,199,99,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,7.25},{100,0,0},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,199,100,nan,nan,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,7.25},{198,0,0},{199,0,0},{200,nan,nan},{204,nan,nan}}}},
    {false,199,199,nan,nan,{{{0,1.25,nan},{1,0,nan},{49,0,nan},{98,0,nan},{99,0,nan},{100,0,nan},{198,0,nan},{199,0,7.25},{200,nan,nan},{204,nan,nan}}}},
    {false,200,0,0,0,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,200,1,0,0,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,200,49,0,0,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,200,99,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,200,100,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {false,200,199,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {true,-1,0,nan,nan,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,-1,1,nan,nan,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,-1,49,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,-1,99,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,-1,100,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,-1,199,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
    {true,0,0,0,0,{{{0,1.25,7.25},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,0,1,0,0,{{{0,1.25,0},{1,0,7.25},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,0,49,0,0,{{{0,1.25,0},{1,0,0},{49,0,7.25},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,0,99,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,0,100,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,0,199,0,0,{{{0,1.25,0},{1,0,0},{49,0,0},{98,0,0},{99,0,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {true,1,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,1,1,0,0,{{{0,1.25,0},{1,nan,7.25},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,1,49,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,7.25},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,1,99,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,1,100,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,1,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {true,49,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,49,1,0,0,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,49,49,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,7.25},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,49,99,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,49,100,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,49,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {true,98,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,98,1,0,0,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,98,49,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,98,99,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,98,100,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,98,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {true,99,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,99,1,0,0,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,99,49,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,99,99,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,7.25},{100,0,0},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,99,100,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,7.25},{198,0,0},{199,0,0},{200,0,0},{204,0,0}}}},
    {true,99,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,0,0},{198,0,0},{199,0,7.25},{200,0,0},{204,0,0}}}},
    {true,100,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,100,1,0,0,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,100,49,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,100,99,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,100,100,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,nan,7.25},{198,nan,0},{199,nan,0},{200,0,0},{204,0,0}}}},
    {true,100,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,nan,0},{198,nan,0},{199,nan,7.25},{200,0,0},{204,0,0}}}},
    {true,198,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,198,1,0,0,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,198,49,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,198,99,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,198,100,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,198,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,nan,0},{198,nan,0},{199,nan,7.25},{200,0,0},{204,0,0}}}},
    {true,199,0,0,0,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,199,1,0,0,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,199,49,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,199,99,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,199,100,0,0,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,0,0},{204,0,0}}}},
    {true,199,199,0,0,{{{0,1.25,0},{1,nan,0},{49,nan,0},{98,nan,0},{99,nan,0},{100,nan,0},{198,nan,0},{199,nan,7.25},{200,0,0},{204,0,0}}}},
    {true,200,0,nan,nan,{{{0,1.25,7.25},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,200,1,nan,nan,{{{0,1.25,nan},{1,nan,7.25},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,200,49,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,7.25},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,200,99,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,7.25},{100,nan,nan},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,200,100,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,7.25},{198,nan,nan},{199,nan,nan},{200,nan,nan},{204,nan,nan}}}},
    {true,200,199,nan,nan,{{{0,1.25,nan},{1,nan,nan},{49,nan,nan},{98,nan,nan},{99,nan,nan},{100,nan,nan},{198,nan,nan},{199,nan,7.25},{200,nan,nan},{204,nan,nan}}}},
  };
  for(const auto& test:cases) {
    ResultsTableState table;bool flag=test.initial;
    for(int row=0;row<205;++row) {
      if(row==test.toggle)flag=!test.initial;
      ResultsTableRows group;group.numeric_columns={"Counter"};
      if(row==0)group.numeric_columns.push_back("Early");
      if(row==test.late)group.numeric_columns.push_back("Late");
      table.append_rows({group},flag);
      check(table.capacity()==std::size_t(row<99?100:row<199?200:400));
    }
    check(table.row_count()==205 && table.capacity()==400);
    for(const auto& cell:test.cells) {
      if(cell.row!=0)same(table.default_value(cell.row,"Early"),cell.early);
      if(cell.row!=std::size_t(test.late))same(table.default_value(cell.row,"Late"),cell.late);
    }
    const auto record=table.record();auto restored=ResultsTableState::from_record(record);
    check(metadata_equal(restored.record(),record));
    restored.append_rows({{1,{"Counter"},true}},flag);
    same(restored.default_value(205,"Early"),test.next_early);same(restored.default_value(205,"Late"),test.next_late);
    check(table.row_count()==205); // Detached snapshot does not advance its owner.
  }
  {
    ResultsTableState empty;auto record=empty.record();
    record["managed"]=std::vector<std::string>{};record["columns"]=std::vector<std::string>{};
    check(metadata_equal(ResultsTableState::from_record(record).record(),empty.record()));
    record["columns"]=std::vector<std::string>{"invalid"};
    refuses<std::invalid_argument>([&]{ResultsTableState::from_record(record);});
  }
  for(bool flag:{false,true}) {
    ResultsTableState table;table.append_rows({{105,{"Old"},true}},flag);table.clear();
    check(table.row_count()==0 && table.capacity()==100 && table.column_names().empty() && table.nan_empty_cells()==flag);
    table.append_rows({{3,{},true},{1,{"New"},true}},flag);same(table.default_value(0,"New"),flag?nan:0.);
  }
  {
    ResultsTableState table;table.append_rows({{2,{},false},{1,{"A"},true}},true);
    check(!table.default_value(0,"A") && !table.default_value(1,"A"));same(table.default_value(2,"A"),nan);
    check(!table.default_value(2,"unknown"));refuses<std::out_of_range>([&]{table.default_value(3,"A");});
    auto bad=table.record();bad["version"]=2;refuses<std::invalid_argument>([&]{ResultsTableState::from_record(bad);});
    bad=table.record();bad["rows"]=true;refuses<std::invalid_argument>([&]{ResultsTableState::from_record(bad);});
    bad=table.record();bad["capacity"]=101;refuses<std::invalid_argument>([&]{ResultsTableState::from_record(bad);});
    const auto before=table.record();refuses<std::invalid_argument>([&]{table.append_rows({{1,{"A","A"},true}},false);});
    check(metadata_equal(before,table.record()));
  }
  {
    ResultsTableStateLimits limits;limits.max_working_bytes=65536;
    ResultsTableState table(limits);std::vector<ResultsTableRows> repeated(500,{1,{"A","B"},true});
    check(table.append_working_bytes(repeated)<limits.max_working_bytes);table.append_rows(repeated,true);
    check(table.row_count()==500 && table.column_names().size()==2);
    limits.max_working_bytes=1024;ResultsTableState tight(limits);const auto before=tight.record();
    refuses<std::invalid_argument>([&]{tight.append_rows({{1,{"A"},true}},true);});check(metadata_equal(before,tight.record()));
    limits=ResultsTableStateLimits{};limits.max_columns=1;ResultsTableState one(limits);
    refuses<std::invalid_argument>([&]{one.append_rows({{1,{"A","B"},true}},true);});check(one.row_count()==0 && one.column_names().empty() && !one.nan_empty_cells());
    limits.max_rows=1;ResultsTableState single(limits);refuses<std::invalid_argument>([&]{single.append_rows({{2,{},true}},false);});
    limits=ResultsTableStateLimits{};limits.max_name_bytes=1;ResultsTableState names(limits);refuses<std::invalid_argument>([&]{names.append_rows({{1,{"AB"},true}},false);});
  }
  std::cout<<"PASS results-table allocation:120 original numeric cases,2reset,normal continuation/record/defaults/opaque/limits/atomicity\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
