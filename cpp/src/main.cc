/*
 *  Copyright Francois Simond 2017
 */

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using std::cout;
using std::vector;

#define SAMPLE_RATE 48000.0
#define BUFFER_LEN_TESTS 13
#define SAMPLE_COUNT 524288
#define FILTER_COUNT 100
// #define WRITE_BUFFERS

struct SquareWave {
  size_t switch_samples;
  bool status;
  size_t progress;

  explicit SquareWave(double frequency) {
    switch_samples = round(SAMPLE_RATE / frequency / 2.0);
    status = false;
    progress = 0;
  }

  void reset() {
    status = false;
    progress = 0;
  }
};

void fill_buffer(vector<double> &buf, SquareWave *sqw) {
  for (unsigned int i = 0; i < buf.size(); i++) {
    if (sqw->progress == sqw->switch_samples) {
      sqw->progress = 0;
      sqw->status = !sqw->status;
    }

    buf[i] = sqw->status ? 0.5 : -0.5;
    sqw->progress++;
  }
}

struct Biquad {
  double b0;
  double b1;
  double b2;
  double a1;
  double a2;

  double x1 = 0.0;
  double x2 = 0.0;
  double y1 = 0.0;
  double y2 = 0.0;

  static Biquad peak_eq(double fs, double f0, double q, double db_gain) {
    double A = pow(10.0, db_gain / 40.0);
    double omega = 2 * M_PIl * f0 / fs;

    double alpha = sin(omega) / (2 * q);

    Biquad bq;

    double a0 = 1 + alpha / A;

    bq.b0 = (1 + alpha * A) / a0;
    bq.b1 = (-2 * cos(omega)) / a0;
    bq.b2 = (1 - alpha * A) / a0;
    bq.a1 = bq.b1;
    bq.a2 = (1 - alpha / A) / a0;

    return bq;
  }

  void reset() {
    x1 = 0.0;
    x2 = 0.0;
    y1 = 0.0;
    y2 = 0.0;
  }
};

void reset_biquads(vector<Biquad> &biquads) {
  for (auto &biquad : biquads) {
    biquad.reset();
  }
}

#ifdef WRITE_BUFFERS
struct OutputPcmFile {
  std::ofstream writer;

  explicit OutputPcmFile(uint64_t buffer_len) {
    std::string path = "/tmp/vec_overhead_cpp_" + std::to_string(buffer_len);
    remove(path.c_str());
    writer.open(path, std::ofstream::binary);
  }

  void write_buffer(const vector<double> &buf) {
    writer.write((const char *)buf.data(), buf.size() * sizeof(double_t));
  }

  void close() { writer.close(); }
};
#endif

void print_elapsed(std::string msg, std::chrono::steady_clock::time_point start,
                   uint64_t filter_count) {
  auto end = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  double duration =
      elapsed.count() / static_cast<double>(filter_count) / SAMPLE_COUNT;
  double realtime = 1.0 / duration / SAMPLE_RATE * 1e+9;
  cout << "\t" << msg << "\t" << duration << " ns"
       << "\t" << realtime << "x for generator + IIR filter\n";
}

void iir(vector<double> &buf, Biquad *bq) {
  for (unsigned int i = 0; i < buf.size(); i++) {
    double x = buf[i];
    buf[i] = (bq->b0 * x) + (bq->b1 * bq->x1) + (bq->b2 * bq->x2) -
             (bq->a1 * bq->y1) - (bq->a2 * bq->y2);

    bq->x2 = bq->x1;
    bq->x1 = x;

    bq->y2 = bq->y1;
    bq->y1 = buf[i];
  }
}

int main(int argc, char **argv) {
  cout << "DSP Bench C++\n";

  // initialize the square wave generator struct
  SquareWave *sqw = new SquareWave(50.0);

  vector<Biquad> biquads;
  bool biquad_gain_positive = true;
  for (unsigned i = 0; i < FILTER_COUNT; i++) {
    double db_gain = biquad_gain_positive ? 2.0 : -2.0;
    biquad_gain_positive = !biquad_gain_positive;
    biquads.push_back(Biquad::peak_eq(SAMPLE_RATE, 50.0, 0.3, db_gain));
  }

  for (unsigned int l = 3; l < BUFFER_LEN_TESTS; l++) {
    size_t buffer_len = pow(2, l);

    unsigned int filter_count = FILTER_COUNT;
    unsigned int buffer_count = SAMPLE_COUNT / buffer_len;

    cout << "Buffer size: " << buffer_len << " samples\n";

    // create the buffer
    vector<double> *buf = new vector<double>;
    buf->resize(buffer_len);

    sqw->reset();
    reset_biquads(biquads);

#ifdef WRITE_BUFFERS
    OutputPcmFile *output = new OutputPcmFile(buffer_len);
#endif

    std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();

    for (unsigned int i = 0; i < buffer_count; i++) {
      fill_buffer(*buf, sqw);

      for (unsigned int f = 0; f < filter_count; f++) {
        iir(*buf, &biquads[f]);
      }

#ifdef WRITE_BUFFERS
      output->write_buffer(buf);
#endif
    }

    print_elapsed("vector", start, filter_count);

#ifdef WRITE_BUFFERS
    output->close();
    delete output;
#endif
  }

  delete sqw;

  return 0;
}