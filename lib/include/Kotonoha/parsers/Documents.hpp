#pragma once
#include <Kotonoha/core/Resource.hpp>
#include <Kotonoha/parsers/Ktrf.h>
#include <Kotonoha/parsers/Ors.h>
#include <Kotonoha/routing/KtrfAdapter.h>
namespace Kotonoha {
using KtrfDocument = Resource<Kotonoha_KtrfDocument, Kotonoha_KtrfClean>;
using OrsDocument = Resource<Kotonoha_orsData, Kotonoha_OrsClean>;
using KtrfRuntimeResource =
    Resource<Kotonoha_KtrfRuntime, Kotonoha_KtrfRuntimeClean>;
using KtrfRouterResource =
    Resource<Kotonoha_KtrfRouter, Kotonoha_KtrfRouterClean>;
using KtrfAdapterResource =
    Resource<Kotonoha_KtrfAdapter, Kotonoha_KtrfAdapterClean>;
} // namespace Kotonoha
