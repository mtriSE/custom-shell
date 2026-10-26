#pragma once

// One visitor from several lambdas, for std::visit.
template <class... Ts>
struct overloaded : Ts...
{
  using Ts::operator()...;
};
