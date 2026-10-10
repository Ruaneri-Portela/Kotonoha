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

void Kotonoha_IOMonitorInit(Kotonoha_IOMonitor *monitor,
                            bool retryMissingAssets);
void Kotonoha_IOMonitorBindNetworkWaitFlag(Kotonoha_IOMonitor *monitor,
                                            SDL_AtomicInt *networkWaitFlag);
void Kotonoha_IOMonitorCancel(Kotonoha_IOMonitor *monitor);
bool Kotonoha_IOMonitorIsLoading(const Kotonoha_IOMonitor *monitor);
bool Kotonoha_IOMonitorHasMissingAssets(const Kotonoha_IOMonitor *monitor);
void Kotonoha_IOMonitorSetRetryMissingAssets(bool retry);
bool Kotonoha_IOMonitorRetryMissingAssetsEnabled(void);

void Kotonoha_IOMonitorOperationInit(Kotonoha_IOMonitorOperation *operation,
                                     Kotonoha_IOMonitor *owner);
void Kotonoha_IOMonitorOperationReset(Kotonoha_IOMonitorOperation *operation);
void Kotonoha_IOMonitorOperationCancel(Kotonoha_IOMonitorOperation *operation);
bool Kotonoha_IOMonitorOperationIsCancelled(
    const Kotonoha_IOMonitorOperation *operation);
void Kotonoha_IOMonitorOperationBegin(Kotonoha_IOMonitorOperation *operation);
void Kotonoha_IOMonitorOperationEnd(Kotonoha_IOMonitorOperation *operation);
void Kotonoha_IOMonitorOperationSetStalled(
    Kotonoha_IOMonitorOperation *operation, bool stalled);
int Kotonoha_IOMonitorInterrupt(void *opaque);
void Kotonoha_IOMonitorSetMissing(Kotonoha_IOMonitorOperation *operation,
                                  bool missing);
bool Kotonoha_IOMonitorShouldRetryMissing(
    const Kotonoha_IOMonitorOperation *operation);
bool Kotonoha_IOMonitorIsIgnoredMissing(
    const Kotonoha_IOMonitorOperation *operation);
