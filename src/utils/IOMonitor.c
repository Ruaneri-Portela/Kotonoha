#include <Kotonoha/utils/IOMonitor.h>

#define KOTONOHA_FFMPEG_IO_TIMEOUT_MS 500

static SDL_AtomicInt retryMissingAssets = { 1 };

static bool operationIsPending(
	const Kotonoha_IOMonitorOperation* operation) {
	return operation->stalled ||
		(operation->missingAsset && operation->owner != NULL &&
			operation->owner->retryMissingAssets);
}

static void updatePendingCount(Kotonoha_IOMonitorOperation* operation,
	bool wasPending) {
	if (operation->owner == NULL) {
		return;
	}

	const bool isPending = operationIsPending(operation);
	if (isPending != wasPending) {
		SDL_AddAtomicInt(&operation->owner->pendingCount, isPending ? 1 : -1);
	}
}

void Kotonoha_IOMonitorInit(Kotonoha_IOMonitor* monitor,
	bool shouldRetryMissingAssets) {
	if (monitor == NULL) {
		return;
	}
	SDL_SetAtomicInt(&monitor->pendingCount, 0);
	SDL_SetAtomicInt(&monitor->missingCount, 0);
	SDL_SetAtomicInt(&monitor->stalledCount, 0);
	monitor->networkWaitFlag = NULL;
	monitor->retryMissingAssets = shouldRetryMissingAssets;
}

void Kotonoha_IOMonitorBindNetworkWaitFlag(Kotonoha_IOMonitor* monitor,
	SDL_AtomicInt* networkWaitFlag) {
	if (monitor == NULL) {
		return;
	}
	monitor->networkWaitFlag = networkWaitFlag;
	if (networkWaitFlag != NULL) {
		SDL_SetAtomicInt(networkWaitFlag,
			SDL_GetAtomicInt(&monitor->stalledCount) > 0 ? 1 : 0);
	}
}

bool Kotonoha_IOMonitorIsLoading(const Kotonoha_IOMonitor* monitor) {
	return monitor != NULL && SDL_GetAtomicInt(
		(SDL_AtomicInt*)&monitor->pendingCount) > 0;
}

bool Kotonoha_IOMonitorHasMissingAssets(const Kotonoha_IOMonitor* monitor) {
	return monitor != NULL && SDL_GetAtomicInt(
		(SDL_AtomicInt*)&monitor->missingCount) > 0;
}

void Kotonoha_IOMonitorSetRetryMissingAssets(bool retry) {
	SDL_SetAtomicInt(&retryMissingAssets, retry ? 1 : 0);
}

bool Kotonoha_IOMonitorRetryMissingAssetsEnabled(void) {
	return SDL_GetAtomicInt(&retryMissingAssets) != 0;
}

void Kotonoha_IOMonitorOperationInit(Kotonoha_IOMonitorOperation* operation,
	Kotonoha_IOMonitor* owner) {
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

void Kotonoha_IOMonitorOperationReset(Kotonoha_IOMonitorOperation* operation) {
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
			SDL_SetAtomicInt(operation->owner->networkWaitFlag,
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

void Kotonoha_IOMonitorOperationCancel(Kotonoha_IOMonitorOperation* operation) {
	if (operation != NULL) {
		SDL_SetAtomicInt(&operation->cancelled, 1);
	}
}

void Kotonoha_IOMonitorOperationBegin(Kotonoha_IOMonitorOperation* operation) {
	if (operation != NULL) {
		operation->operationStartedAt = SDL_GetTicks();
		operation->operationActive = true;
	}
}

void Kotonoha_IOMonitorOperationEnd(Kotonoha_IOMonitorOperation* operation) {
	if (operation != NULL) {
		operation->operationActive = false;
		operation->operationStartedAt = 0;
	}
}

void Kotonoha_IOMonitorOperationSetStalled(
	Kotonoha_IOMonitorOperation* operation, bool stalled) {
	if (operation == NULL || operation->stalled == stalled) {
		return;
	}
	const bool wasPending = operationIsPending(operation);
	operation->stalled = stalled;
	if (operation->owner != NULL) {
		SDL_AddAtomicInt(&operation->owner->stalledCount, stalled ? 1 : -1);
		if (operation->owner->networkWaitFlag != NULL) {
			SDL_SetAtomicInt(operation->owner->networkWaitFlag,
				SDL_GetAtomicInt(&operation->owner->stalledCount) > 0 ? 1 : 0);
		}
	}
	updatePendingCount(operation, wasPending);
}

int Kotonoha_IOMonitorInterrupt(void* opaque) {
	Kotonoha_IOMonitorOperation* operation =
		(Kotonoha_IOMonitorOperation*)opaque;
	if (operation == NULL) {
		return 0;
	}
	if (SDL_GetAtomicInt(&operation->cancelled) != 0) {
		return 1;
	}
	if (!operation->operationActive ||
		SDL_GetTicks() - operation->operationStartedAt <
			KOTONOHA_FFMPEG_IO_TIMEOUT_MS) {
		return 0;
	}

	if (!operation->stalled) {
		Kotonoha_IOMonitorOperationSetStalled(operation, true);
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"FFmpeg AVIO is waiting for media data; displaying loading screen");
	}
	return 0;
}

void Kotonoha_IOMonitorSetMissing(Kotonoha_IOMonitorOperation* operation,
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
	}
	else if (operation->owner != NULL &&
		!operation->owner->retryMissingAssets) {
		operation->retainedMissingAsset = true;
	}
	updatePendingCount(operation, wasPending);
}

bool Kotonoha_IOMonitorShouldRetryMissing(
	const Kotonoha_IOMonitorOperation* operation) {
	return operation != NULL && operation->owner != NULL &&
		operation->owner->retryMissingAssets;
}

bool Kotonoha_IOMonitorIsIgnoredMissing(
	const Kotonoha_IOMonitorOperation* operation) {
	return operation != NULL && operation->missingAsset &&
		operation->owner != NULL &&
		!operation->owner->retryMissingAssets;
}
