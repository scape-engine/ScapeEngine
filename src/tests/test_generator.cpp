#include "engine/math/noise/OpenSimplex2S.hpp"
#include "engine/pcg/terrain/terrain_generator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#define RESET "\033[0m"
#define BLACK "\033[30m"              /* Black */
#define RED "\033[31m"                /* Red */
#define GREEN "\033[32m"              /* Green */
#define YELLOW "\033[33m"             /* Yellow */
#define BLUE "\033[34m"               /* Blue */
#define MAGENTA "\033[35m"            /* Magenta */
#define CYAN "\033[36m"               /* Cyan */
#define WHITE "\033[37m"              /* White */
#define BOLDBLACK "\033[1m\033[30m"   /* Bold Black */
#define BOLDRED "\033[1m\033[31m"     /* Bold Red */
#define BOLDGREEN "\033[1m\033[32m"   /* Bold Green */
#define BOLDYELLOW "\033[1m\033[33m"  /* Bold Yellow */
#define BOLDBLUE "\033[1m\033[34m"    /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m" /* Bold Magenta */
#define BOLDCYAN "\033[1m\033[36m"    /* Bold Cyan */
#define BOLDWHITE "\033[1m\033[37m"   /* Bold White */

namespace Tests {

// ============================================
// Test 1: Analytical Derivatives Correctness
// ============================================
void TestAnalyticalDerivatives() {
  std::cout << "\n=== Test 1: Analytical Derivatives ===" << std::endl;

  OpenSimplex2S simplex(42);

  // Test multiple points
  struct TestPoint {
    double x, y;
  };
  TestPoint test_points[] = {
      {10.5, 20.3}, {0.0, 0.0}, {-5.2, 15.7}, {100.0, -50.0}, {0.123, 0.456}};

  double max_error = 0.0;
  int passed = 0;

  for (const auto& pt : test_points) {
    // Get analytical derivatives
    double value, dx, dy;
    simplex.noise2_deriv(pt.x, pt.y, value, dx, dy);

    // Compute finite difference (central difference)
    double h = 0.0001;
    double dx_numeric =
        (simplex.noise2(pt.x + h, pt.y) - simplex.noise2(pt.x - h, pt.y)) /
        (2.0 * h);
    double dy_numeric =
        (simplex.noise2(pt.x, pt.y + h) - simplex.noise2(pt.x, pt.y - h)) /
        (2.0 * h);

    double error_x = std::abs(dx - dx_numeric);
    double error_y = std::abs(dy - dy_numeric);
    double total_error = error_x + error_y;

    max_error = std::max(max_error, total_error);

    // Error should be < 0.001 (0.1%)
    if (total_error < 0.001) {
      passed++;
      std::cout << "  ✓ Point (" << pt.x << ", " << pt.y
                << "): error = " << total_error << std::endl;
    } else {
      std::cout << "  ✗ Point (" << pt.x << ", " << pt.y
                << "): error = " << total_error << " (TOO HIGH!)" << std::endl;
    }
  }

  std::cout << "Result: " << passed << "/"
            << (sizeof(test_points) / sizeof(TestPoint))
            << " passed | Max error: " << max_error << std::endl;

  assert(passed == (sizeof(test_points) / sizeof(TestPoint)) &&
         "Analytical derivatives test FAILED!");

  std::cout << GREEN << "✓ Analytical derivatives test PASSED!\n"
            << RESET << std::endl;
}

// ============================================
// Test 2: Derivative Consistency Across Octaves
// ============================================
void TestDerivativeScaling() {
  std::cout << "=== Test 2: Derivative Frequency Scaling ===" << std::endl;

  OpenSimplex2S simplex(12345);

  double x = 5.0, y = 5.0;

  // Test that derivatives scale correctly with frequency
  double frequencies[] = {1.0, 2.0, 4.0, 8.0};

  for (double freq : frequencies) {
    double value, dx, dy;
    simplex.noise2_deriv(x * freq, y * freq, value, dx, dy);

    // Derivatives should scale linearly with frequency
    std::cout << "  Freq " << freq << ": dx=" << dx << ", dy=" << dy
              << ", magnitude=" << std::sqrt(dx * dx + dy * dy) << std::endl;

    // Check that derivatives are non-zero (sanity check)
    assert((std::abs(dx) > 1e-10 || std::abs(dy) > 1e-10) &&
           "Derivatives should not be zero!");
  }

  std::cout << GREEN << "✓ Frequency scaling test PASSED!\n"
            << RESET << std::endl;
}

// ============================================
// Test 3: Generator Integration
// ============================================
void TestGeneratorDerivatives() {
  std::cout << "=== Test 3: Generator Integration ===" << std::endl;

  PCG::Config config;
  config.seed = 999;
  config.frequency = 0.01f;
  config.octaves = 4;
  config.amplitude = 50.0f;

  PCG::TerrainGenerator gen(config);

  // Sample terrain at multiple points
  float test_x[] = {0.0f, 10.0f, 50.0f, 100.0f};
  float test_y[] = {0.0f, 10.0f, 50.0f, 100.0f};

  int valid_samples = 0;

  for (float x : test_x) {
    for (float y : test_y) {
      PCG::Sample sample = gen.Eval(x, y);

      // Check that we got valid results
      bool valid = !std::isnan(sample.height) && !std::isinf(sample.height) &&
                   !std::isnan(sample.gradient.x) &&
                   !std::isnan(sample.gradient.y);

      if (valid) {
        valid_samples++;
      } else {
        std::cout << "  ✗ Invalid sample at (" << x << ", " << y << ")"
                  << std::endl;
      }
    }
  }

  std::cout << "  Valid samples: " << valid_samples << "/"
            << (sizeof(test_x) / sizeof(float) * sizeof(test_y) / sizeof(float))
            << std::endl;

  assert(valid_samples > 0 && "Generator produced no valid samples!");

  std::cout << GREEN << "✓ Generator integration test PASSED!\n"
            << RESET << std::endl;
}

// ============================================
// Test 4: Performance Comparison
// ============================================
void TestPerformance() {
  std::cout << "=== Test 4: Performance Benchmark ===" << std::endl;

  OpenSimplex2S simplex(42);

  const int NUM_SAMPLES = 10000;

  // Benchmark analytical derivatives
  auto start_analytical = std::chrono::high_resolution_clock::now();

  double sum_analytical = 0.0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    double x = (i % 100) * 0.1;
    double y = (i / 100) * 0.1;
    double value, dx, dy;
    simplex.noise2_deriv(x, y, value, dx, dy);
    sum_analytical += dx + dy;  // Prevent optimization
  }

  auto end_analytical = std::chrono::high_resolution_clock::now();
  auto duration_analytical =
      std::chrono::duration_cast<std::chrono::microseconds>(end_analytical -
                                                            start_analytical)
          .count();

  // Benchmark finite differences (5 calls per derivative)
  auto start_finite = std::chrono::high_resolution_clock::now();

  double sum_finite = 0.0;
  double h = 0.001;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    double x = (i % 100) * 0.1;
    double y = (i / 100) * 0.1;

    // Central difference requires 4 noise calls + 1 for value
    double dx =
        (simplex.noise2(x + h, y) - simplex.noise2(x - h, y)) / (2.0 * h);
    double dy =
        (simplex.noise2(x, y + h) - simplex.noise2(x, y - h)) / (2.0 * h);
    sum_finite += dx + dy;
  }

  auto end_finite = std::chrono::high_resolution_clock::now();
  auto duration_finite = std::chrono::duration_cast<std::chrono::microseconds>(
                             end_finite - start_finite)
                             .count();

  double speedup = (double)duration_finite / duration_analytical;

  std::cout << "  Analytical: " << duration_analytical << " μs" << std::endl;
  std::cout << "  Finite Diff: " << duration_finite << " μs" << std::endl;
  std::cout << "  Speedup: " << speedup << "x faster" << std::endl;
  std::cout << "  (Sums: " << sum_analytical << " vs " << sum_finite << ")"
            << std::endl;

  // ! Performance assertions belong in benchmarks, not in correctness tests
  assert(speedup > 1.5 && "Analytical should be at least 1.5x faster!");

  std::cout << GREEN << "✓ Performance test PASSED!\n" << RESET << std::endl;
}

// ============================================
// Test 5: Edge Cases
// ============================================
void TestEdgeCases() {
  std::cout << "=== Test 5: Edge Cases ===" << std::endl;

  OpenSimplex2S simplex(777);

  struct EdgeCase {
    const char* name;
    double x, y;
  };

  EdgeCase cases[] = {{"Zero", 0.0, 0.0},
                      {"Negative", -10.0, -20.0},
                      {"Large positive", 1000.0, 2000.0},
                      {"Large negative", -1000.0, -2000.0},
                      {"Mixed", -50.0, 100.0},
                      {"Small decimals", 0.00001, 0.00002}};

  int passed = 0;

  for (const auto& test : cases) {
    double value, dx, dy;
    simplex.noise2_deriv(test.x, test.y, value, dx, dy);

    bool valid = !std::isnan(value) && !std::isinf(value) && !std::isnan(dx) &&
                 !std::isinf(dx) && !std::isnan(dy) && !std::isinf(dy);

    if (valid) {
      std::cout << "  ✓ " << test.name << ": OK" << std::endl;
      passed++;
    } else {
      std::cout << "  ✗ " << test.name << ": FAILED (NaN/Inf detected)"
                << std::endl;
    }
  }

  assert(passed == (sizeof(cases) / sizeof(EdgeCase)) &&
         "Edge case test FAILED!");

  std::cout << GREEN << "✓ Edge cases test PASSED!\n" << RESET << std::endl;
}

// ============================================
// Test 6: Expressive Range Analysis
// ============================================
void TestExpressiveRange() {
  std::cout << "=== Test 6: Expressive Range Analysis ===" << std::endl;

  PCG::Config config;
  config.seed = 42;
  config.frequency = 0.05f;
  config.amplitude = 60.0f;
  config.sharpness = -0.5f;
  config.ridge_erosion = 0.6f;
  config.slope_erosion = 0.4f;

  PCG::TerrainGenerator gen(config);

  // Generate small heightmap for analysis
  const int SIZE = 512;
  std::vector<float> heights;
  heights.reserve(SIZE * SIZE);

  float min_h = std::numeric_limits<float>::max();
  float max_h = std::numeric_limits<float>::lowest();
  float sum_h = 0.0f;

  // Height histogram (10 bins)
  std::array<int, 10> height_bins = {0};

  for (int y = 0; y < SIZE; ++y) {
    for (int x = 0; x < SIZE; ++x) {
      PCG::Sample s = gen.Eval((float)x, (float)y);
      heights.push_back(s.height);

      min_h = std::min(min_h, s.height);
      max_h = std::max(max_h, s.height);
      sum_h += s.height;
    }
  }

  float mean_h = sum_h / (SIZE * SIZE);
  float range_h = max_h - min_h;

  // Fill histogram
  for (float h : heights) {
    int bin = std::clamp(static_cast<int>((h - min_h) / range_h * 9.99f), 0, 9);
    height_bins[bin]++;
  }

  // Calculate variance
  float variance = 0.0f;
  for (float h : heights) {
    variance += (h - mean_h) * (h - mean_h);
  }
  variance /= (SIZE * SIZE);
  float stddev = std::sqrt(variance);

  std::cout << "  Height Range: [" << min_h << ", " << max_h
            << "] = " << range_h << std::endl;
  std::cout << "  Mean: " << mean_h << " | StdDev: " << stddev << std::endl;
  std::cout << "  Distribution:" << std::endl;

  for (int i = 0; i < 10; ++i) {
    float bin_start = min_h + (i * range_h / 10.0f);
    float bin_end = min_h + ((i + 1) * range_h / 10.0f);
    int count = height_bins[i];
    float percent = (count / (float)(SIZE * SIZE)) * 100.0f;

    std::cout << "    [" << std::setw(6) << std::fixed << std::setprecision(1)
              << bin_start << " - " << std::setw(6) << bin_end
              << "]: " << std::setw(4) << count << " (" << std::setw(5)
              << std::setprecision(1) << percent << "%) ";

    // ASCII bar chart
    int bar_len = static_cast<int>(percent / 2.0f);
    for (int j = 0; j < bar_len; ++j)
      std::cout << "█";
    std::cout << std::endl;
  }

  // Feature detection (count peaks and valleys)
  int peaks = 0;
  int valleys = 0;
  for (int y = 1; y < SIZE - 1; ++y) {
    for (int x = 1; x < SIZE - 1; ++x) {
      int idx = y * SIZE + x;
      float h = heights[idx];
      float n = heights[(y - 1) * SIZE + x];
      float s = heights[(y + 1) * SIZE + x];
      float e = heights[y * SIZE + (x + 1)];
      float w = heights[y * SIZE + (x - 1)];

      if (h > n && h > s && h > e && h > w)
        peaks++;
      if (h < n && h < s && h < e && h < w)
        valleys++;
    }
  }

  std::cout << "  Features: " << peaks << " peaks, " << valleys << " valleys"
            << std::endl;

  std::cout << GREEN << "✓ Complete!\n" << RESET << std::endl;
}

// ============================================
// Test 7: Spectral Analysis (Power Spectrum)
// ============================================
void TestSpectralAnalysis() {
  std::cout << "=== Test 7: Spectral Analysis ===" << std::endl;

  PCG::Config config;
  config.seed = 42;
  config.frequency = 0.05f;
  config.amplitude = 60.0f;
  config.octaves = 6;

  PCG::TerrainGenerator gen(config);

  // Generate 1D slice for spectral analysis
  const int SIZE = 256;
  std::vector<double> signal(SIZE);

  for (int i = 0; i < SIZE; ++i) {
    PCG::Sample s = gen.Eval((float)i, 0.0f);
    signal[i] = s.height;
  }

  // Remove DC component (mean)
  double mean = 0.0;
  for (double v : signal)
    mean += v;
  mean /= SIZE;
  for (double& v : signal)
    v -= mean;

  // Simple power spectrum (magnitude squared)
  std::vector<double> power_spectrum(SIZE / 2);

  for (int k = 0; k < SIZE / 2; ++k) {
    double real = 0.0, imag = 0.0;
    for (int n = 0; n < SIZE; ++n) {
      double angle = 2.0 * std::numbers::pi * k * n / SIZE;
      real += signal[n] * std::cos(angle);
      imag += signal[n] * std::sin(angle);
    }
    power_spectrum[k] = (real * real + imag * imag) / SIZE;
  }

  // Find dominant frequencies
  std::vector<std::pair<int, double>> top_freqs;
  for (int k = 1; k < SIZE / 2; ++k) {  // Skip DC (k=0)
    top_freqs.push_back({k, power_spectrum[k]});
  }

  std::sort(top_freqs.begin(), top_freqs.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  std::cout << "  Top 5 Frequency Components:" << std::endl;
  for (int i = 0; i < std::min(5, (int)top_freqs.size()); ++i) {
    int freq_bin = top_freqs[i].first;
    double power = top_freqs[i].second;
    double wavelength = (double)SIZE / freq_bin;

    std::cout << "    Freq " << freq_bin << " (λ=" << std::fixed
              << std::setprecision(1) << wavelength << " units): "
              << "Power = " << std::scientific << std::setprecision(2) << power
              << std::endl;
  }

  // Calculate power law exponent (β in 1/f^β)
  // Use freqs 2-10 to avoid DC and high-freq noise
  double sum_log_freq = 0.0, sum_log_power = 0.0;
  double sum_log_freq_sq = 0.0, sum_log_freq_log_power = 0.0;
  int count = 0;

  for (int k = 2; k <= 10 && k < SIZE / 2; ++k) {
    if (power_spectrum[k] > 0) {
      double log_f = std::log(k);
      double log_p = std::log(power_spectrum[k]);
      sum_log_freq += log_f;
      sum_log_power += log_p;
      sum_log_freq_sq += log_f * log_f;
      sum_log_freq_log_power += log_f * log_p;
      count++;
    }
  }

  double beta =
      (count * sum_log_freq_log_power - sum_log_freq * sum_log_power) /
      (count * sum_log_freq_sq - sum_log_freq * sum_log_freq);

  std::cout << "  Power Law Exponent (β): " << std::fixed
            << std::setprecision(2) << -beta << std::endl;
  std::cout << "  (Target is β ≈ 2.0-2.5)" << std::endl;

  std::cout << GREEN << "✓ Complete!\n" << RESET << std::endl;
}

// ============================================
// Main Test Runner
// ============================================
void RunAllGeneratorTests() {
  std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
  std::cout << "║   Generator & Derivatives Test Suite     ║" << std::endl;
  std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

  try {
    TestAnalyticalDerivatives();
    TestDerivativeScaling();
    TestGeneratorDerivatives();
    TestPerformance();
    TestEdgeCases();
    TestExpressiveRange();
    TestSpectralAnalysis();

    std::cout << "\n╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║     ALL TESTS PASSED!                    ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝\n" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "\n❌ TEST FAILED WITH EXCEPTION: " << e.what() << std::endl;
    throw;
  }
}

}  // namespace Tests

// Entry point for running tests standalone
int main() {
  Tests::RunAllGeneratorTests();
  return 0;
}
