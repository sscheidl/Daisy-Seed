# Audrey II DSP helpers

`BiquadFilters.h`, `BiquadFilters.cpp` and `DSPUtils.h` from
[Synthux-Academy/Audrey-II](https://github.com/Synthux-Academy/Audrey-II) at `e5ab799`, MIT, see `LICENSE`.

They live here because two components use them: the Audrey 3 feedback engine and the Echo Delay effect. An effect must not
depend on an instrument. `BiquadFilters.*` are unchanged. `DSPUtils.h` has no `arm_math.h`, which only commented-out code
needed, and uses `std::tan` instead of `std::tanf`, for portability.
