#include <Kotonoha/utils/IOMonitor.h>

#define KotonohaFFmpeg_IO_TIMEOUT_MS 500

static SDL_AtomicInt retryMissingAssets = {1};

static bool operationIsPending(const Kotonoha_IOMonitorOperation *operation) {
  return operation->stalled ||
         (operation->missingAsset && operation->owner != NULL &&
          operation->owner->retryMissingAssets);
}

static void updatePendingCount(Kotonoha_IOMonitorOperation *operation,
                               bool wasPending) {
  if (operation->owner == NULL) {
    return;
  }

  const bool isPending = operationIsPending(operation);
  if (isPending != wasPending) {
    SDL_AddAtomicInt(&operation->owner->pendingCount, isPending ? 1 : -1);
  }
}

void KotonohaIOMonitorInit(Kotonoha_IOMonitor *monitor,
                           bool shouldRetryMissingAssets) {
  if (monitor == NULL) {
    return;
  }
  SDL_SetAtomicInt(&monitor->pendingCount, 0);
  SDL_SetAtomicInt(&monitor->missingCount, 0);
  SDL_SetAtomicInt(&monitor->stalledCount, 0);
  SDL_SetAtomicInt(&monitor->cancelled, 0);
  monitor->networkWaitFlag = NULL;
  monitor->retryMissingAssets = shouldRetryMissingAssets;
}

void KotonohaIOMonitorBindNetworkWaitFlag(Kotonoha_IOMonitor *monitor,
                                          SDL_AtomicInt *networkWaitFlag) {
  if (monitor == NULL) {
    return;
  }
  monitor->networkWaitFlag = networkWaitFlag;
  if (networkWaitFlag != NULL) {
    SDL_SetAtomicInt(networkWaitFlag,
                     SDL_GetAtomicInt(&monitor->stalledCount) > 0 ? 1 : 0);
  }
}

void KotonohaIOMonitorCancel(Kotonoha_IOMonitor *monitor) {
  if (monitor != NULL) {
    SDL_SetAtomicInt(&monitor->cancelled, 1);
  }
}

bool KotonohaIOMonitorIsLoading(const Kotonoha_IOMonitor *monitor) {
  return monitor != NULL &&
         SDL_GetAtomicInt((SDL_AtomicInt *)&monitor->pendingCount) > 0;
}

bool KotonohaIOMonitorHasMissingAssets(const Kotonoha_IOMonitor *monitor) {
  return monitor != NULL &&
         SDL_GetAtomicInt((SDL_AtomicInt *)&monitor->missingCount) > 0;
}

void KotonohaIOMonitorSetRetryMissingAssets(bool retry) {
  SDL_SetAtomicInt(&retryMissingAssets, retry ? 1 : 0);
}

bool KotonohaIOMonitorRetryMissingAssetsEnabled(void) {
  return SDL_GetAtomicInt(&retryMissingAssets) != 0;
}

void KotonohaIOMonitorOperationInit(Kotonoha_IOMonitorOperation *operation,
                                    Kotonoha_IOMonitor *owner) {
  if (operation == NULL) {
    return;
  }
  operation->owner = owner;
  operation->operationStartedAt = 0;
  SDL_SetAtomicInt(&operation->cancelled, 0);
  operation->operationActive = false;
  operation->stalled = false;
  operation->missingAsset = false;
  operation->retainedMissingAsset = false;
}

void KotonohaIOMonitorOperationReset(Kotonoha_IOMonitorOperation *operation) {
  if (operation == NULL) {
    return;
  }
  const bool wasPending = operationIsPending(operation);
  if (operation->owner != NULL && operation->missingAsset &&
      !operation->retainedMissingAsset) {
    SDL_AddAtomicInt(&operation->owner->missingCount, -1);
  }
  if (operation->owner != NULL && operation->stalled) {
    SDL_AddAtomicInt(&operation->owner->stalledCount, -1);
    if (operation->owner->networkWaitFlag != NULL) {
      SDL_SetAtomicInt(
          operation->owner->networkWaitFlag,
          SDL_GetAtomicInt(&operation->owner->stalledCount) > 0 ? 1 : 0);
    }
  }
  operation->operationActive = false;
  operation->operationStartedAt = 0;
  operation->stalled = false;
  operation->missingAsset = false;
  operation->retainedMissingAsset = false;
  updatePendingCount(operation, wasPending);
  SDL_SetAtomicInt(&operation->cancelled, 0);
}

void KotonohaIOMonitorOperationCancel(Kotonoha_IOMonitorOperation *operation) {
  if (operation != NULL) {
    SDL_SetAtomicInt(&operation->cancelled, 1);
  }
}

bool KotonohaIOMonitorOperationIsCancelled(
    const Kotonoha_IOMonitorOperation *operation) {
  return operation != NULL &&
         (SDL_GetAtomicInt((SDL_AtomicInt *)&operation->cancelled) != 0 ||
          (operation->owner != NULL &&
           SDL_GetAtomicInt(&operation->owner->cancelled) != 0));
}

void KotonohaIOMonitorOperationBegin(Kotonoha_IOMonitorOperation *operation) {
  if (operation != NULL) {
    operation->operationStartedAt = SDL_GetTicks();
    operation->operationActive = true;
  }
}

void KotonohaIOMonitorOperationEnd(Kotonoha_IOMonitorOperation *operation) {
  if (operation != NULL) {
    operation->operationActive = false;
    operation->operationStartedAt = 0;
  }
}

void KotonohaIOMonitorOperationSetStalled(
    Kotonoha_IOMonitorOperation *operation, bool stalled) {
  if (operation == NULL || operation->stalled == stalled) {
    return;
  }
  const bool wasPending = operationIsPending(operation);
  operation->stalled = stalled;
  if (operation->owner != NULL) {
    SDL_AddAtomicInt(&operation->owner->stalledCount, stalled ? 1 : -1);
    if (operation->owner->networkWaitFlag != NULL) {
      SDL_SetAtomicInt(
          operation->owner->networkWaitFlag,
          SDL_GetAtomicInt(&operation->owner->stalledCount) > 0 ? 1 : 0);
    }
  }
  updatePendingCount(operation, wasPending);
}

int KotonohaIOMonitorInterrupt(void *opaque) {
  Kotonoha_IOMonitorOperation *operation =
      (Kotonoha_IOMonitorOperation *)opaque;
  if (operation == NULL) {
    return 0;
  }
  if (KotonohaIOMonitorOperationIsCancelled(operation)) {
    return 1;
  }
  if (!operation->operationActive ||
      SDL_GetTicks() - operation->operationStartedAt <
          KotonohaFFmpeg_IO_TIMEOUT_MS) {
    return 0;
  }

  if (!operation->stalled) {
    KotonohaIOMonitorOperationSetStalled(operation, true);
    SDL_LogWarn(
        SDL_LOG_CATEGORY_APPLICATION,
        "FFmpeg AVIO is waiting for media data; displaying loading screen");
  }
  return 0;
}

void KotonohaIOMonitorSetMissing(Kotonoha_IOMonitorOperation *operation,
                                 bool missing) {
  if (operation == NULL || operation->missingAsset == missing) {
    return;
  }

  const bool wasPending = operationIsPending(operation);
  operation->missingAsset = missing;
  if (operation->owner != NULL) {
    SDL_AddAtomicInt(&operation->owner->missingCount, missing ? 1 : -1);
  }
  if (!missing) {
    operation->retainedMissingAsset = false;
  } else if (operation->owner != NULL &&
             !operation->owner->retryMissingAssets) {
    operation->retainedMissingAsset = true;
  }
  updatePendingCount(operation, wasPending);
}

bool KotonohaIOMonitorShouldRetryMissing(
    const Kotonoha_IOMonitorOperation *operation) {
  return operation != NULL && operation->owner != NULL &&
         operation->owner->retryMissingAssets;
}

bool KotonohaIOMonitorIsIgnoredMissing(
    const Kotonoha_IOMonitorOperation *operation) {
  return operation != NULL && operation->missingAsset &&
         operation->owner != NULL && !operation->owner->retryMissingAssets;
}
