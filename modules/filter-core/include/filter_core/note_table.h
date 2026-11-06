#pragma once
// SPDX-License-Identifier: MIT
// The one table of the filter core: cutoff in note units -> every value that depends only
// on the cutoff. Built at compile time in double precision from the formulas in
// docs/DESIGN.md, so there is no libm dependency and no start-up work, and the audio path
// needs neither tan() nor the square root and division of the limiter level.
#include <cstddef>

namespace seed::dsp::filter {

inline constexpr unsigned kSampleRate = 48000;
// Feedback at resonance 100 %. The loop oscillates above 4 at every cutoff, because the
// bilinear transform maps the cutoff exactly (DESIGN.md 2). 4.4 is a trade-off measured in
// DESIGN.md: more starts the oscillation sooner, less keeps it purer and flatter.
inline constexpr float kMaxFeedback = 4.4f;
inline constexpr double kOscExcess = 0.0953101798043249; // ln(kMaxFeedback / 4)
inline constexpr float kNoteMin = 0.f;                   // 8.18 Hz
inline constexpr float kNoteMax = 135.f;                 // 19.9 kHz
inline constexpr unsigned kRowsPerNote = 2;
inline constexpr std::size_t kRows = 135 * kRowsPerNote + 2; // one spare row for the interpolation

struct NoteRow {
    float G;      // g / (1 + g), g = tan(pi f / fs)
    float inv_a2; // 1 / a^2 of the limiter for an output level of 1 at the fourth stage
    float half_a; // a / 2, the limiter's ceiling for the same level
};

namespace detail {
constexpr double kPi = 3.14159265358979323846;

constexpr double Sqrt(double x) {
    double r = x > 1. ? x : 1.;
    for(int i = 0; i < 60; ++i)
        r = 0.5 * (r + x / r);
    return r;
}
// tan(x) for 0 < x < pi/2 from the Taylor series of sine and cosine.
constexpr double Tan(double x) {
    double sine = 0., cosine = 0., term = 1.;
    for(int i = 0; i < 40; ++i) {
        if(i % 2 == 0)
            cosine += (i % 4 == 0) ? term : -term;
        else
            sine += (i % 4 == 1) ? term : -term;
        term *= x / double(i + 1);
    }
    return sine / cosine;
}
// Input amplitude / a at which the limiter's describing function falls to `gain`:
// N(r) = 2 (1 - 1 / sqrt(1 + r^2)) / r^2 solved for r.
constexpr double LimiterRatio(double gain) {
    const double q = (-gain + Sqrt(gain * gain + 8. * gain)) / (2. * gain);
    return Sqrt(q * q - 1.);
}
constexpr NoteRow MakeRow(double frequency) {
    const double g = Tan(kPi * frequency / double(kSampleRate));
    const double one_g2 = 1. + g * g;
    // First-order loop solution: the state leak that exactly cancels the feedback excess.
    const double leak = kOscExcess * 2. * g / one_g2;
    // Amplitude of the limited state when the fourth stage has amplitude 1.
    const double state = 2. * Sqrt(2.) * Sqrt(one_g2);
    const double a = state / LimiterRatio(1. - leak);
    return {float(g / (1. + g)), float(1. / (a * a)), float(0.5 * a)};
}
struct Table {
    NoteRow rows[kRows];
};
constexpr Table MakeTable() {
    Table table{};
    double frequency = 8.17579891564370697; // note 0
    for(std::size_t i = 0; i < kRows; ++i) {
        table.rows[i] = MakeRow(frequency);
        frequency *= 1.02930223664349207; // 2^(1/24): half a semitone
    }
    return table;
}
} // namespace detail

inline constexpr detail::Table kNoteTable = detail::MakeTable();

// The row of `note` (NaN and out-of-range notes are clamped), linear between the rows.
inline NoteRow LookUp(float note) {
    note = note > kNoteMin ? (note < kNoteMax ? note : kNoteMax) : kNoteMin;
    const float position = note * float(kRowsPerNote);
    const unsigned index = unsigned(position);
    const float fraction = position - float(index);
    const NoteRow& lo = kNoteTable.rows[index];
    const NoteRow& hi = kNoteTable.rows[index + 1];
    return {lo.G + fraction * (hi.G - lo.G), lo.inv_a2 + fraction * (hi.inv_a2 - lo.inv_a2),
            lo.half_a + fraction * (hi.half_a - lo.half_a)};
}

} // namespace seed::dsp::filter
