// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Interop/Connection/SpatialEventTracerUserInterface.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Interop/Connection/SpatialEventTracer.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/SpatialClassInfoManager.h"
#include "SpatialView/EntityComponentId.h"

DEFINE_LOG_CATEGORY(LogSpatialEventTracerUserInterface);

FString USpatialEventTracerUserInterface::CreateSpanId(UObject* WorldContextObject)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return {};
	}

	return SpatialGDK::SpatialEventTracer::SpanIdToString(EventTracer->CreateSpan().GetValue());
}

FString USpatialEventTracerUserInterface::CreateSpanIdWithCauses(UObject* WorldContextObject, const TArray<FString>& Causes)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return {};
	}

	TArray<Trace_SpanId> SpanIdS;
	for (const FString& String : Causes)
	{
		SpanIdS.Add(SpatialGDK::SpatialEventTracer::StringToSpanId(String));
	}

	return SpatialGDK::SpatialEventTracer::SpanIdToString(EventTracer->CreateSpan(SpanIdS.GetData(), SpanIdS.Num()).GetValue());
}

void USpatialEventTracerUserInterface::TraceEvent(UObject* WorldContextObject, const FString& SpanId, FSpatialTraceEvent SpatialTraceEvent)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr)
	{
		return;
	}

	EventTracer->TraceEvent(SpatialTraceEvent, SpatialGDK::SpatialEventTracer::StringToSpanId(SpanId));
}

void USpatialEventTracerUserInterface::SetActiveSpanId(UObject* WorldContextObject, FEventTracerDynamicDelegate Delegate,
													   const FString& SpanId)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return;
	}

	EventTracer->SpanIdStack.AddNewLayer(SpatialGDK::SpatialEventTracer::StringToSpanId(SpanId));
	Delegate.Execute();
	EventTracer->SpanIdStack.PopLayer();
}

void USpatialEventTracerUserInterface::AddSpanIdToStack(UObject* WorldContextObject, const FString& SpanId)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return;
	}

	EventTracer->SpanIdStack.AddNewLayer(SpatialGDK::SpatialEventTracer::StringToSpanId(SpanId));
}

void USpatialEventTracerUserInterface::PopSpanIdFromStack(UObject* WorldContextObject)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return;
	}

	EventTracer->SpanIdStack.PopLayer();
}

void USpatialEventTracerUserInterface::AddLatentActorSpanId(UObject* WorldContextObject, const AActor& Actor, const FString& SpanId)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return;
	}

	USpatialNetDriver* NetDriver = GetSpatialNetDriver(WorldContextObject);
	if (NetDriver == nullptr)
	{
		return;
	}

	const Worker_EntityId EntityId = NetDriver->PackageMap->GetEntityIdFromObject(&Actor);
	const Worker_ComponentId ComponentId = NetDriver->ClassInfoManager->GetComponentIdForClass(*Actor.GetClass());
	EventTracer->AddLatentPropertyUpdateSpanIds({ EntityId, ComponentId }, SpatialGDK::SpatialEventTracer::StringToSpanId(SpanId));
}

void USpatialEventTracerUserInterface::AddLatentComponentSpanId(UObject* WorldContextObject, const UActorComponent& Component,
																const FString& SpanId)
{
	SpatialGDK::SpatialEventTracer* EventTracer = GetEventTracer(WorldContextObject);
	if (EventTracer == nullptr || !EventTracer->IsEnabled())
	{
		return;
	}

	USpatialNetDriver* NetDriver = GetSpatialNetDriver(WorldContextObject);
	if (NetDriver == nullptr)
	{
		return;
	}

	AActor* Owner = Component.GetOwner();
	if (Owner == nullptr)
	{
		UE_LOG(LogSpatialEventTracerUserInterface, Error,
			   TEXT("USpatialEventTracerUserInterface::AddLatentComponentSpanId - Component has now owner"));
		return;
	}

	const Worker_EntityId EntityId = NetDriver->PackageMap->GetEntityIdFromObject(Owner);
	const Worker_ComponentId ComponentId =
		NetDriver->ClassInfoManager->GetComponentIdForSpecificSubObject(*Owner->GetClass(), *Component.GetClass());
	EventTracer->AddLatentPropertyUpdateSpanIds({ EntityId, ComponentId }, SpatialGDK::SpatialEventTracer::StringToSpanId(SpanId));
}

void USpatialEventTracerUserInterface::AddLatentSpanId(UObject* WorldContextObject, UObject* Object, const FString& SpanId)
{
	if (AActor* Actor = Cast<AActor>(Object))
	{
		AddLatentActorSpanId(WorldContextObject, *Actor, SpanId);
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		AddLatentComponentSpanId(WorldContextObject, *Component, SpanId);
	}
	UE_LOG(LogSpatialEventTracerUserInterface, Warning,
		   TEXT("USpatialEventTracerUserInterface::AddLatentSpanId - Could not add latent SpanId for %s"), Object->GetName());
}

SpatialGDK::SpatialEventTracer* USpatialEventTracerUserInterface::GetEventTracer(UObject* WorldContextObject)
{
	USpatialNetDriver* NetDriver = GetSpatialNetDriver(WorldContextObject);
	if (NetDriver == nullptr || NetDriver->Connection == nullptr)
	{
		UE_LOG(LogSpatialEventTracerUserInterface, Error,
			   TEXT("USpatialEventTracerUserInterface::GetEventTracer - NetDriver or Connection is null"));
		return nullptr;
	}

	return NetDriver->Connection->GetEventTracer();
}

USpatialNetDriver* USpatialEventTracerUserInterface::GetSpatialNetDriver(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		UE_LOG(LogSpatialEventTracerUserInterface, Error, TEXT("USpatialEventTracerUserInterface::GetSpatialNetDriver - World is null"));
		World = GWorld;
	}

	USpatialNetDriver* NetDriver = Cast<USpatialNetDriver>(World->GetNetDriver());
	return NetDriver;
}
