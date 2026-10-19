// CHEB1: J2000 ecliptic Chebyshev coefficients, little-endian km; time TDB.
#pragma once

#include "ephemeris.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "Orbiter CHEB1 tables require little-endian doubles");

class ChebTable {
    std::string filename_;
    double start_ = 0, length_ = 0;
    int segments_ = 0, terms_ = 0;
    std::vector<double> coeff_;
    bool tried_ = false;

public:
    explicit ChebTable(const char *file) : filename_(file) {}

    bool load()
    {
        if (tried_) return !coeff_.empty();
        tried_ = true;
        FILE *f = std::fopen(oapiResolvePath(filename_.c_str()).c_str(), "rb");
        if (!f) {
            oapiWriteLogError("%s: table missing (using analytic fallback)", filename_.c_str());
            return false;
        }
        char header[128] = {};
        const bool parsed = std::fgets(header, sizeof(header), f) &&
            std::sscanf(header, "CHEB1 %lf %lf %d %d",
                        &start_, &length_, &segments_, &terms_) == 4;
        bool valid = parsed && std::isfinite(start_) && std::isfinite(length_) &&
                     length_ >= 1e-3 && segments_ > 0 && segments_ <= 100000 &&
                     terms_ >= 2 && terms_ <= 64 &&
                     std::isfinite(start_ + length_ * segments_);
        size_t count = 0;
        if (valid) {
            count = size_t(segments_) * 3 * size_t(terms_);
            const long dataStart = std::ftell(f);
            valid = dataStart > 0 && std::fseek(f, 0, SEEK_END) == 0 &&
                    std::ftell(f) - dataStart == long(count * sizeof(double)) &&
                    std::fseek(f, dataStart, SEEK_SET) == 0;
        }
        if (valid) {
            coeff_.resize(count);
            valid = std::fread(coeff_.data(), sizeof(double), count, f) == count;
            for (size_t i = 0; i < count && valid; ++i)
                valid = std::isfinite(coeff_[i]);
        }
        std::fclose(f);
        if (!valid) {
            coeff_.clear();
            segments_ = 0;
            oapiWriteLogError("%s: invalid CHEB1 table (using analytic fallback)",
                              filename_.c_str());
            return false;
        }
        oapiWriteLogV("%s: JPL-derived CHEB1 table loaded: %d segments, %d coefficients, MJD %.3f..%.3f",
                      filename_.c_str(), segments_, terms_, start_, endMjd());
        return true;
    }

    double startMjd() const { return start_; }
    double endMjd() const { return start_ + segments_ * length_; }

    bool covers(double mjd) const
    {
        return segments_ > 0 && std::isfinite(mjd) &&
               mjd >= start_ && mjd < endMjd();
    }

    // Populate true and bary output for the shared ChebModule API.
    bool sample(double mjd, double *output)
    {
        if (!tried_ && !load()) return false;
        if (!covers(mjd)) return false;
        const double u = (mjd - start_) / length_;
        const int seg = int(u);
        const double x = 2.0 * (u - seg) - 1.0;
        for (int a = 0; a < 3; ++a) {
            const double *k = coeff_.data() +
                              (size_t(seg) * 3 + a) * size_t(terms_);
            double t0 = 1, t1 = x, d0 = 0, d1 = 1;
            double pos = k[0] + k[1]*x, deriv = k[1];
            for (int j = 2; j < terms_; ++j) {
                const double t2 = 2*x*t1-t0;
                const double d2 = 2*t1+2*x*d1-d0;
                pos += k[j]*t2;
                deriv += k[j]*d2;
                t0 = t1; t1 = t2;
                d0 = d1; d1 = d2;
            }
            // Table axes are x,y,z ecliptic J2000; Orbiter uses x,z,y.
            const int axis = a == 1 ? 2 : (a == 2 ? 1 : 0);
            output[axis] = pos * 1000.0;
            output[3+axis] = deriv * (2.0 / length_) * (1000.0 / eph::DAY);
        }
        std::memcpy(output + 6, output, 6*sizeof(double));
        return true;
    }
};
