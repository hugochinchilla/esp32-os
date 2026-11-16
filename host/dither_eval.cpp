// Compares the picture dithers: each picture from `tools/gen_pictures.py
// dither DIR` through each dither, then the error against the original after
// a Gaussian blur in linear light (what the eye averages at a distance),
// as PSNR in dB, higher is better. Writes each result as DIR/<picture>-<n>.idx.
//
//   dither_eval DIR
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/gfx/canvas.hpp"
#include "core/image/convert.hpp"
#include "host_adapters.hpp"

namespace {

double g_lin[256];

// Separable Gaussian blur of a w x h x 3 image in place (edges repeated).
void blur(std::vector<double> &img, int w, int h, double sigma)
{
    const int r = static_cast<int>(std::ceil(sigma * 3));
    std::vector<double> k(2 * r + 1);
    double sum = 0;
    for (int i = -r; i <= r; i++) {
        sum += k[i + r] = std::exp(-i * i / (2 * sigma * sigma));
    }
    for (double &v : k) {
        v /= sum;
    }
    std::vector<double> tmp(img.size());
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < 3; c++) {
                    double acc = 0;
                    for (int i = -r; i <= r; i++) {
                        int sx = pass == 0 ? x + i : x, sy = pass == 0 ? y : y + i;
                        sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
                        sy = sy < 0 ? 0 : (sy >= h ? h - 1 : sy);
                        acc += k[i + r] * img[(sy * w + sx) * 3 + c];
                    }
                    tmp[(y * w + x) * 3 + c] = acc;
                }
            }
        }
        img.swap(tmp);
    }
}

double psnr(const std::vector<uint8_t> &rgb, const std::vector<uint8_t> &out, int w, int h, double sigma)
{
    std::vector<double> a(w * h * 3), b(w * h * 3);
    for (int i = 0; i < w * h; i++) {
        const gfx::Rgb s = gfx::shown(out[i]);
        const uint8_t sv[3] = {s.r, s.g, s.b};
        for (int c = 0; c < 3; c++) {
            a[i * 3 + c] = g_lin[rgb[i * 3 + c]];
            b[i * 3 + c] = g_lin[sv[c]];
        }
    }
    blur(a, w, h, sigma);
    blur(b, w, h, sigma);
    double se = 0;
    for (size_t i = 0; i < a.size(); i++) {
        const double d = 255 * (std::pow(a[i], 1 / 2.2) - std::pow(b[i], 1 / 2.2));
        se += d * d;
    }
    const double mse = se / a.size();
    return 10 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: dither_eval DIR\n");
        return 2;
    }
    const std::string dir = argv[1];
    for (int i = 0; i < 256; i++) {
        g_lin[i] = std::pow(i / 255.0, 2.2);
    }
    static host::HostMemory memory;
    static image::Converter conv;
    conv.ready(memory);
    std::ifstream list(dir + "/list.txt");
    std::string name;
    int w, h;
    printf("%-26s", "dither \\ picture");
    std::vector<std::string> names;
    std::vector<std::pair<int, int>> sizes;
    while (list >> name >> w >> h) {
        names.push_back(name);
        sizes.push_back({w, h});
        printf(" %13s", name.c_str());
    }
    printf(" %13s %9s\n", "mean s1/s2", "us/Mpx");
    for (int d = 0; d < image::kDitherCount; d++) {
        printf("%-26s", image::dither_info(static_cast<image::Dither>(d)).name);
        double total1 = 0, total2 = 0, us = 0, px = 0;
        for (size_t p = 0; p < names.size(); p++) {
            w = sizes[p].first;
            h = sizes[p].second;
            std::ifstream f(dir + "/" + names[p] + ".rgb", std::ios::binary);
            std::vector<uint8_t> rgb((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::vector<uint8_t> out(w * h);
            const auto t0 = std::chrono::steady_clock::now();
            conv.begin(w, h, w, h, out.data(), static_cast<image::Dither>(d));
            for (int y = 0; y < h; y++) {
                conv.row(y, &rgb[y * w * 3]);
            }
            us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            px += w * h;
            const double q1 = psnr(rgb, out, w, h, 1.0), q2 = psnr(rgb, out, w, h, 2.0);
            total1 += q1;
            total2 += q2;
            printf(" %6.2f/%6.2f", q1, q2);
            std::ofstream o(dir + "/" + names[p] + "-" + std::to_string(d) + ".idx", std::ios::binary);
            o.write(reinterpret_cast<const char *>(out.data()), out.size());
        }
        printf(" %6.2f/%6.2f %9.0f\n", total1 / names.size(), total2 / names.size(), us / px * 1e6);
    }
    return 0;
}
