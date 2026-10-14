#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Kotonoha_IOMonitor {
  SDL_AtomicInt pendingCount;
  SDL_AtomicInt missingCount;
  SDL_AtomicInt stalledCount;
  SDL_AtomicInt cancelled;
  SDL_AtomicInt *networkWaitFlag;
  bool retryMissingAssets;
} Kotonoha_IOMonitor;

typedef struct Kotonoha_IOMonitorOperation {
  Kotonoha_IOMonitor *owner;
  Uint64 operationStartedAt;
  SDL_AtomicInt cancelled;
  bool operationActive;
  bool stalled;
  bool missingAsset;
  bool retainedMissingAsset;
} Kotonoha_IOMonitorOperation;

void KotonohaIOMonitorInit(Kotonoha_IOMonitor *monitor,
                           bool retryMissingAssets);
void KotonohaIOMonitorBindNetworkWaitFlag(Kotonoha_IOMonitor *monitor,
                                          SDL_AtomicInt *networkWaitFlag);
void KotonohaIOMonitorCancel(Kotonoha_IOMonitor *monitor);
bool KotonohaIOMonitorIsLoading(const Kotonoha_IOMonitor *monitor);
bool KotonohaIOMonitorHasMissingAssets(const Kotonoha_IOMonitor *monitor);
void KotonohaIOMonitorSetRetryMissingAssets(bool retry);
bool KotonohaIOMonitorRetryMissingAssetsEnabled(void);

void KotonohaIOMonitorOperationInit(Kotonoha_IOMonitorOperation *operation,
                                    Kotonoha_IOMonitor *owner);
void KotonohaIOMonitorOperationReset(Kotonoha_IOMonitorOperation *operation);
void KotonohaIOMonitorOperationCancel(Kotonoha_IOMonitorOperation *operation);
bool KotonohaIOMonitorOperationIsCancelled(
    const Kotonoha_IOMonitorOperation *operation);
void KotonohaIOMonitorOperationBegin(Kotonoha_IOMonitorOperation *operation);
void KotonohaIOMonitorOperationEnd(Kotonoha_IOMonitorOperation *operation);
void KotonohaIOMonitorOperationSetStalled(
    Kotonoha_IOMonitorOperation *operation, bool stalled);
int KotonohaIOMonitorInterrupt(void *opaque);
void KotonohaIOMonitorSetMissing(Kotonoha_IOMonitorOperation *operation,
                                 bool missing);
bool KotonohaIOMonitorShouldRetryMissing(
    const Kotonoha_IOMonitorOperation *operation);
bool KotonohaIOMonitorIsIgnoredMissing(
    const Kotonoha_IOMonitorOperation *operation);
