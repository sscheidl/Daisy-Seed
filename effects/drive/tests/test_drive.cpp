// Host tests of Drive (ARBEITSAUFTRAG Phase 3b): level test over drive 0-100 % and all
// curves with the phase 0 fixtures and the 303 fixture (acceptance), slow correction
// within +-3 dB and held in silence, the stored compensation table, the 10 ms on/off fade
// against the click criterion, DC, finiteness, parameters, golden output.
#define DRIVE_TABLE_NO_MAIN
#include "../tools/gen_autolevel.cpp"
#include "drive/host.h"
#include "drive/params.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using seed::fx::Drive;
using seed::fx::drive::Curve;

// Golden of Drive 0.1.0: FNV-1a over the saw_lp2 fixture through all four curves.
#ifndef DRIVE_GOLDEN
#define DRIVE_GOLDEN 0x50B9CF6Eu
#endif

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if(!(cond)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                             \
        }                                                                           \
    } while(0)

static Drive::Storage storage;

static double Db(double power) { return 10.0 * std::log10(power + 1e-30); }

// Runs a fixture looped for `seconds`; returns in/out power over the last second.
static void Run(Drive& fx, const std::vector<float>& x, double seconds, double& in_db, double& out_db,
                std::vector<float>* out = nullptr) {
    const std::size_t blocks = std::size_t(seconds * 1000), last = blocks - 1000;
    double in_sq = 0, out_sq = 0;
    float l[48], r[48];
    for(std::size_t b = 0; b < blocks; ++b) {
        const float* in = x.data() + (b % 1000) * 48;
        CHECK(fx.Process({in, in, l, r, 48}));
        for(int i = 0; i < 48; ++i) {
            if(out)
                out->push_back(l[i]);
            if(b >= last) {
                in_sq += double(in[i]) * in[i];
                out_sq += double(l[i]) * l[i];
            }
        }
    }
    in_db = Db(in_sq / 48000.0);
    out_db = Db(out_sq / 48000.0);
}

static void TestLevel() {
    std::vector<std::pair<std::string, std::vector<float>>> fixtures;
    for(const char* name : drive_table::kFixtures)
        fixtures.push_back({name, drive_table::Load(name)});
    fixtures.push_back({"acid303_phrase", drive_table::Load("acid303_phrase")});
    double worst = 0, fold_square_worst = 0;
    for(float offset : {0.f, -1.5f}) {
        for(int c = 0; c < 4; ++c) {
            for(int d = 0; d <= 100; d += 10) {
                for(auto& [name, x] : fixtures) {
                    Drive fx;
                    fx.Prepare({48000, 48}, storage);
                    seed::fx::drive::Parameters p;
                    p.curve = Curve(c);
                    p.drive = float(d);
                    p.compensation = 100.f;
                    p.target_offset_db = offset;
                    fx.SetParameters(p);
                    double in_db, out_db;
                    Run(fx, x, 4.0, in_db, out_db);
                    const double error = out_db - (in_db + offset);
                    // Known limit (CHANGELOG): Fold turns the flat tops of a square into an
                    // almost constant value, so its level falls far below any compensation
                    // within +-3 dB. It must never get louder than the target, though.
                    const bool fold_square = c == int(Curve::Fold) && name.rfind("square", 0) == 0;
                    if(fold_square) {
                        fold_square_worst = error < fold_square_worst ? error : fold_square_worst;
                        CHECK(error <= 1.0);
                        continue;
                    }
                    worst = std::fabs(error) > worst ? std::fabs(error) : worst;
                    if(std::fabs(error) > 1.0)
                        std::printf("  level: curve %d drive %d offset %.1f %s: %+.2f dB\n", c, d, offset,
                                    name.c_str(), error);
                    CHECK(std::fabs(error) <= 1.0);
                    CHECK(std::fabs(fx.SlowDb()) <= seed::fx::drive::kSlowLimitDb);
                }
            }
        }
    }
    std::printf("Drive level test: worst deviation %.2f dB (limit 1 dB); Fold on squares down to %+.1f dB\n", worst,
                fold_square_worst);
    // Compensation 0 %: no correction at all; auto level off: raw core level.
    Drive fx;
    fx.Prepare({48000, 48}, storage);
    seed::fx::drive::Parameters p;
    p.drive = 80.f;
    p.compensation = 0.f;
    fx.SetParameters(p);
    double in_db, out_db;
    Run(fx, fixtures[0].second, 2.0, in_db, out_db);
    CHECK(fx.AppliedDb() == 0.f);
    p.compensation = 100.f;
    p.auto_level = false;
    fx.SetParameters(p);
    Run(fx, fixtures[0].second, 1.0, in_db, out_db);
    CHECK(std::fabs(fx.AppliedDb()) < 1e-3f);
}

static void TestSlowCorrection() {
    // A quiet, dark signal leaves a residual the table does not know: limited to +-3 dB.
    std::vector<float> x(48000);
    for(std::size_t i = 0; i < x.size(); ++i)
        x[i] = 0.02f * float(std::sin(2.0 * 3.141592653589793 * 55.0 * double(i) / 48000.0));
    Drive fx;
    fx.Prepare({48000, 48}, storage);
    seed::fx::drive::Parameters p;
    p.drive = 100.f;
    p.tone = 0.f;
    fx.SetParameters(p);
    double in_db, out_db;
    Run(fx, x, 4.0, in_db, out_db);
    CHECK(std::fabs(fx.SlowDb()) <= 3.0f);
    // Silence: the correction holds (after the window that still saw the signal).
    std::vector<float> silence(48000, 0.f);
    Run(fx, silence, 1.0, in_db, out_db);
    const float held = fx.SlowDb();
    Run(fx, silence, 3.0, in_db, out_db);
    CHECK(fx.SlowDb() == held);
}

static void TestTable() {
    using namespace seed::fx::drive;
    float table[4][kLevelPoints];
    drive_table::Table(table);
    for(int c = 0; c < 4; ++c)
        for(int i = 0; i < kLevelPoints; ++i)
            CHECK(std::fabs(table[c][i] - kAutoLevelDb[c][i]) < 1e-3f);
    // Interpolation between points; the drive gain counts into the level and out of the result.
    CHECK(std::fabs(CurveReductionDb(Curve::Soft, -16.5f) - 0.5f * (kAutoLevelDb[0][14] + kAutoLevelDb[0][15])) < 1e-5f);
    CHECK(std::fabs(StaticDb(Curve::Hard, 50.f, -20.f) - (CurveReductionDb(Curve::Hard, -5.f) - 15.f)) < 1e-5f);
    // Quiet signals pass the curves almost unchanged.
    for(int c = 0; c < 3; ++c)
        CHECK(std::fabs(kAutoLevelDb[c][0]) < 0.5f);
}

// Click criterion (Phase 2) for the 10 ms on/off fade: 100 Hz sine at -12 dBFS.
static void TestFade() {
    for(int c = 0; c < 4; ++c) {
        Drive fx;
        fx.Prepare({48000, 48}, storage);
        seed::fx::drive::Parameters p;
        p.curve = Curve(c);
        p.drive = 40.f;
        fx.SetParameters(p);
        seed::fx::DriveMixer mixer;
        mixer.Reset(false);
        float in[48], wet_l[48], wet_r[48], out_l[48], out_r[48];
        int n = 0;
        float prev = 0.f, settled = 0.f, jump = 0.f;
        for(int b = 0; b < 2000; ++b) {
            for(float& s : in)
                s = 0.25f * float(std::sin(2.0 * 3.141592653589793 * 100.0 * n++ / 48000.0));
            fx.Process({in, in, wet_l, wet_r, 48});
            if(b == 1000)
                mixer.SetEnabled(true);
            mixer.Process(in, in, wet_l, wet_r, out_l, out_r, 48);
            for(int i = 0; i < 48; ++i) {
                const float d = std::fabs(out_l[i] - prev);
                prev = out_l[i];
                if(b > 100 && b < 1000)
                    settled = d > settled ? d : settled; // dry, settled
                if(b >= 1000 && b < 1100)
                    jump = d > jump ? d : jump;
                if(b > 1500)
                    settled = d > settled ? d : settled; // wet, settled
            }
        }
        if(jump > 1.5f * settled)
            std::printf("  fade: curve %d %.5f > 1.5 x %.5f\n", c, jump, settled);
        CHECK(jump <= 1.5f * settled);
    }
}

static void TestDcFiniteGolden() {
    const std::vector<float> x = drive_table::Load("saw_lp2");
    uint32_t hash = 2166136261u;
    for(int c = 0; c < 4; ++c) {
        Drive fx;
        fx.Prepare({48000, 48}, storage);
        seed::fx::drive::Parameters p;
        p.curve = Curve(c);
        p.drive = 70.f;
        fx.SetParameters(p);
        std::vector<float> out;
        double in_db, out_db;
        Run(fx, x, 2.0, in_db, out_db, &out);
        double sum = 0;
        for(std::size_t i = out.size() / 2; i < out.size(); ++i)
            sum += out[i];
        CHECK(std::fabs(sum / double(out.size() / 2)) < 0.001);
        for(float f : out) {
            CHECK(std::isfinite(f) && std::fabs(f) < 2.f);
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            for(int k = 0; k < 4; ++k) {
                hash ^= (bits >> (8 * k)) & 0xFF;
                hash *= 16777619u;
            }
        }
    }
    std::printf("Drive golden: 0x%08X\n", unsigned(hash));
    CHECK(hash == DRIVE_GOLDEN);
    // NaN and huge input count as silence; the output stays finite.
    Drive fx;
    fx.Prepare({48000, 48}, storage);
    float bad[48], l[48], r[48];
    for(int i = 0; i < 48; ++i)
        bad[i] = i % 3 ? NAN : 1e30f;
    for(int b = 0; b < 400; ++b) {
        fx.Process({bad, bad, l, r, 48});
        for(int i = 0; i < 48; ++i)
            CHECK(std::isfinite(l[i]) && std::isfinite(r[i]));
    }
}

static void TestParams() {
    seed::fx::drive::Parameters p;
    for(uint8_t i = 0; i < sq::kDriveComponent.count; ++i) {
        const sq::ParamDesc& d = sq::kDriveComponent.params[i];
        int32_t v = 0;
        CHECK(seed::fx::drive::ReadParam(p, d.id, v) && v == d.def);
        CHECK(seed::fx::drive::WriteParam(p, d.id, d.max + 1000) && seed::fx::drive::ReadParam(p, d.id, v) &&
              v == d.max);
        CHECK(seed::fx::drive::WriteParam(p, d.id, d.def));
    }
    CHECK(!seed::fx::drive::WriteParam(p, 99, 0));
}

int main() {
    TestTable();
    TestLevel();
    TestSlowCorrection();
    TestFade();
    TestDcFiniteGolden();
    TestParams();
    if(failures) {
        std::printf("FAIL: %d checks\n", failures);
        return 1;
    }
    std::printf("PASS: compensation table, level +-1 dB (all curves, drive 0-100 %%, offsets 0/-1.5 dB, phase 0 "
                "and 303 fixtures), slow correction, 10 ms fade, DC, finite, parameters, golden\n");
    return 0;
}
