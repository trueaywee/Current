struct big_math : F {
  INCLUDE_IN_PERF_TEST;
  enum { DIM = 100000 };
  template <typename X>
  static X2V<X> f(const X& x) {
    X2V<X> r = 0.0;
    for (size_t i = 0; i < DIM; ++i) {
      switch (i % 7) {
        case 0:
          r += sqrt(x[i] * x[i] + 1.0);
          break;
        case 1:
          r += exp(x[i] * 0.01);
          break;
        case 2:
          r += log(x[i] * x[i] + 1.0);
          break;
        case 3:
          r += sin(x[i]);
          break;
        case 4:
          r += cos(x[i]);
          break;
        case 5:
          r += tan(x[i] * 0.01);
          break;
        case 6:
          r += atan(x[i] * 0.01);
          break;
      }
    }
    return r;
  }
  std::normal_distribution<double> distribution_;
  big_math() {
    for (size_t i = 0; i < DIM; ++i) {
      add_var(distribution_);
    }
  }
};
