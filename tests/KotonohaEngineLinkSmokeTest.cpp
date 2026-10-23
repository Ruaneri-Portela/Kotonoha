#include <Kotonoha/utils/Time.h>

#include <cmath>

int main() {
  Kotonoha_Time *clock = KotonohaTimeNew(true);
  if (clock == nullptr)
    return 1;

  KotonohaTimeSet(clock, 1250);
  const bool positionOk = KotonohaTimeGet(clock) == 1250;
  const bool rateOk = KotonohaTimeSetRate(clock, 2.0) &&
                      std::abs(KotonohaTimeGetRate(clock) - 2.0) < 0.000001;

  KotonohaTimeDestroy(clock);
  return positionOk && rateOk ? 0 : 2;
}
