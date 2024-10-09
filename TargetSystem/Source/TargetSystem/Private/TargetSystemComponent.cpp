// Copyright 2018-2021 Mickael Daniel. All Rights Reserved.

#include "TargetSystemComponent.h"
#include "BTargetPoint.h"
#include "..\Public\TargetSystemInterface.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "TargetActorDetails.h"
#include "TargetSystemDependencies.h"
#include "TargetSystemLog.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

UTargetSystemComponent::UTargetSystemComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    LockedOnWidgetClass = StaticLoadClass(UObject::StaticClass(), nullptr, TEXT("/TargetSystem/UI/WBP_LockOn.WBP_LockOn_C"));
    RequiredClass = APawn::StaticClass();
    TargetCollisionChannel = ECC_Pawn;
}

void UTargetSystemComponent::SetUp(
    bool _bAdjustPitchBasedOnDistanceToTarget,
    bool _bAdjustPitchBasedOnDistanceToTargetUsingCurve
)
{
    bAdjustPitchBasedOnDistanceToTarget = _bAdjustPitchBasedOnDistanceToTarget;
    bAdjustPitchBasedOnDistanceToTargetUsingCurve = _bAdjustPitchBasedOnDistanceToTargetUsingCurve;
}

void UTargetSystemComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		TS_LOG(Error, TEXT("[%s] TargetSystemComponent: Cannot get Owner reference ..."), *GetName());
		return;
	}

	OwnerPawn = Cast<APawn>(OwnerActor);
	if (!ensure(OwnerPawn))
	{
		TS_LOG(Error, TEXT("[%s] TargetSystemComponent: Component is meant to be added to Pawn only ..."), *GetName());
		return;
	}

    if (LoseTargetDistance < MaximumDistanceCanStartTarget)
    {
        LoseTargetDistance = MaximumDistanceCanStartTarget;
    }

	SetupLocalPlayerController();
}

void UTargetSystemComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bTargetLocked || !NearestTarget) return;

    SetControlRotationOnTarget();
}

bool UTargetSystemComponent::CanTargetLock() const
{
    return !PotentialTargets.IsEmpty();
}

void UTargetSystemComponent::StartObservingTarget()
{
    bTargetLocked = true;
    NearestTarget->StartTargetable();
    CurrentSocketOnNearestTarget = GetTargetDetails(NearestTarget).StartTargetPointName;

    if (OnTargetLockedOn.IsBound())
    {
        OnTargetLockedOn.Broadcast(NearestTarget->GetTargetSystemDependencies()->GetOwner());
    }

    SetupLocalPlayerController();
    ControlRotation(true);

    if (IsValid(OwnerPlayerController))
    {
        OwnerPlayerController->SetIgnoreLookInput(true);
    }

    CreateAndAttachTargetLockedOnWidgetComponent(NearestTarget);

    GetWorld()->GetTimerManager().SetTimer(ObservingTimer, this, &UTargetSystemComponent::UpdateTargetInfo, TimerTick, true);
}

void UTargetSystemComponent::UpdateTargetInfo()
{
    FHitResult Hit;
    if(NearestTarget->IsTargetable() && !LineTrace(GetOwner()->GetActorLocation(), GetTargetOwnerLocation(NearestTarget), Hit))
    {
        if (BehindWallTimer.IsValid()) return;
        GetWorld()->GetTimerManager().SetTimer(BehindWallTimer, [this]() { StopObservingTarget(true); }, BreakLineOfSightDelay, false);
        return;
    }
    GetWorld()->GetTimerManager().ClearTimer(BehindWallTimer);

    if (NearestTarget
        && NearestTarget->IsTargetable()
        && GetDistanceFromTarget(NearestTarget) <= LoseTargetDistance) return;

    StopObservingTarget(false, true);
}

void UTargetSystemComponent::StopObservingTarget(const bool bIgnoreAutoSwitch, const bool bTargetIsDead)
{
    if (NearestTarget)
    {
        if (OnTargetLockedOff.IsBound())
        {
            OnTargetLockedOff.Broadcast(NearestTarget->GetTargetSystemDependencies()->GetOwner());
        }

        NearestTarget->StopTargetable();
        if (bTargetIsDead)
        {
            PotentialTargets.Remove(NearestTarget);
            if (OnTargetIsDead.IsBound())
            {
                OnTargetIsDead.Broadcast(NearestTarget);
            }
        }
    }
    GetWorld()->GetTimerManager().ClearTimer(ObservingTimer);

    if (TargetLockedOnWidgetComponent)
    {
        TargetLockedOnWidgetComponent->DestroyComponent();
    }

    if (bIsSwitchingTarget) return;

    if (bAutoTargetSwitch && !bIgnoreAutoSwitch)
    {
        AutoSwitchTarget();
        return;
    }

    StopTargetLock();
}

void UTargetSystemComponent::MessageFinishTargetLock() const
{
    if (OnFinishTargetLock.IsBound())
    {
        OnFinishTargetLock.Broadcast();
    }
}

void UTargetSystemComponent::TryStartTargetLock()
{
    AddPotentialTargetsByInterface(RequiredClass);
    if (!CanTargetLock())
    {
       MessageFinishTargetLock();
        return;
    }

    NearestTarget = FindNearestTarget(true);
    if (!NearestTarget)
    {
        MessageFinishTargetLock();
        return;
    }

    StartObservingTarget();
}

void UTargetSystemComponent::StopTargetLock()
{
    SetupLocalPlayerController();

    bTargetLocked = false;

    if (NearestTarget)
    {
        ControlRotation(false);

        if (IsValid(OwnerPlayerController))
        {
            OwnerPlayerController->ResetIgnoreLookInput();
        }
    }
    PotentialTargets.Empty();

    NearestTarget = nullptr;

    MessageFinishTargetLock();
}

void UTargetSystemComponent::SwitchTarget(FVector2D AxisValue)
{
    if (!CanSwitchTarget(AxisValue)) return;
    if (TrySwitchBetweenTargetPoints(AxisValue)) return;
    if (PotentialTargets.Num() <= 1) return;
    if (bIsSwitchingTarget) return;

    TArray<TargetInterface> ActorsToLook;
    FHitResult Hit;

    for (TargetInterface Interface : PotentialTargets)
    {
        if(!LineTrace(GetOwner()->GetActorLocation(),  GetTargetOwnerLocation(Interface), Hit)) continue;
        if (!IsInViewport(Interface)) continue;

        ActorsToLook.Add(Interface);
    }

    const TargetInterface NewTarget = FMath::Abs(AxisValue.X) > FMath::Abs(AxisValue.Y)  ?
        FindByHorizontal(ActorsToLook, AxisValue.X) :
        FindByVertical(ActorsToLook, AxisValue);

    if (!NewTarget) return;

    bIsSwitchingTarget = true;

    StopObservingTarget();
    NearestTarget = NewTarget;
    StartObservingTarget();

    ResetIsSwitchingTarget();
}

void UTargetSystemComponent::AutoSwitchTarget()
{
    const TScriptInterface<ITargetSystemInterface> NewTarget = FindNearestTarget();
    if (!NewTarget)
    {
        StopTargetLock();
        return;
    }
    NearestTarget = NewTarget;
    StartObservingTarget();

    bIsSwitchingTarget = true;
    ResetIsSwitchingTarget();
}

bool UTargetSystemComponent::TrySwitchBetweenTargetPoints(FVector2D AxisValue)
{
    if (!NearestTarget) return false;
    if (GetTargetDetails(NearestTarget).TargetPoints.Num() <= 1) return false;
    if (bIsSwitchingTarget) return false;

    FString NewCurrentTargetPointName = "None";
    const int32 MaxIndex = GetTargetDetails(NearestTarget).TargetPoints.Num() - 1;
    const float MajorAxis = FMath::Abs(AxisValue.X) > FMath::Abs(AxisValue.Y) ? AxisValue.X : AxisValue.Y;

     const float RangeMin = NearestTarget->GetTargetSystemDependencies()->GetOwner()->GetActorRotation().Yaw - 90.f;
     const float RangeMax = NearestTarget->GetTargetSystemDependencies()->GetOwner()->GetActorRotation().Yaw + 90.f;

    const int32 SwitchDirection = OwnerActor->GetActorRotation().Yaw > RangeMin && OwnerActor->GetActorRotation().Yaw < RangeMax ?
          MajorAxis > 0.f ? 1 : -1:
          MajorAxis > 0.f ? -1 : 1;


    int32 CurrentIndex = 0;
    for (int32 i = 0; i < GetTargetDetails(NearestTarget).TargetPoints.Num(); ++i)
    {
        if (GetTargetDetails(NearestTarget).TargetPoints[i]->GetName() == CurrentSocketOnNearestTarget)
        {
            CurrentIndex = i; break;
        }
    }

    const int32 NewIndex = CurrentIndex + SwitchDirection;
    if (NewIndex > MaxIndex || NewIndex < 0) return false;

   CurrentSocketOnNearestTarget = GetTargetDetails(NearestTarget).TargetPoints[NewIndex]->GetName();
    if (TargetLockedOnWidgetComponent)
    {
        TargetLockedOnWidgetComponent->DestroyComponent();
    }
    CreateAndAttachTargetLockedOnWidgetComponent(NearestTarget);
    bIsSwitchingTarget = true;
    ResetIsSwitchingTarget();
    return true;
}

TScriptInterface<ITargetSystemInterface> UTargetSystemComponent::FindByHorizontal(TArray<TargetInterface> LookTargets, float AxisValue) const
{
    TScriptInterface<ITargetSystemInterface> NewNearestTarget = nullptr;

    float MinDistance = MaximumDistanceCanStartTarget;
    const float RangeMin = AxisValue < 0 ? 0 : 180;
    const float RangeMax = AxisValue < 0 ? 180 : 360;

    for (TScriptInterface<ITargetSystemInterface> Interface : LookTargets)
    {
        if (NearestTarget == Interface) continue;
        const float Angle = GetAngleUsingCameraRotation(GetTargetOwnerLocation(Interface));
        if (Angle < RangeMin || Angle > RangeMax) continue;

        const float Distance = GetDistanceFromTarget(Interface);
        if (Distance > MaximumDistanceCanStartTarget) continue;

        const float RelativeActorsDistance = NearestTarget->GetTargetSystemDependencies()->GetOwner()->GetDistanceTo(Interface->GetTargetSystemDependencies()->GetOwner());
        if (RelativeActorsDistance > MinDistance) continue;

        MinDistance = RelativeActorsDistance;
        NewNearestTarget = Interface;
    }
    return NewNearestTarget;
}

TScriptInterface<ITargetSystemInterface> UTargetSystemComponent::FindByVertical(TArray<TargetInterface> LookTargets, FVector2D AxisValue) const
{
    TScriptInterface<ITargetSystemInterface> NewNearestTarget = nullptr;

    float MinDistance = MaximumDistanceCanStartTarget;

    const float RangeMin = AxisValue.X < 0.f ? 0 : 180;
    const float RangeMax = AxisValue.X < 0.f ? 180 : 360;

    for (TScriptInterface<ITargetSystemInterface> Interface : LookTargets)
    {
        if (NearestTarget == Interface) continue;

        const float Angle = GetAngleUsingCameraRotation(Interface->GetTargetSystemDependencies()->GetOwner()->GetActorLocation());
        if (Angle < RangeMin || Angle > RangeMax) continue;

        const float Distance = GetDistanceFromTarget(Interface);
        if (Distance > MaximumDistanceCanStartTarget) continue;

        const float RelativeActorsDistance = NearestTarget->GetTargetSystemDependencies()->GetOwner()->GetDistanceTo(Interface->GetTargetSystemDependencies()->GetOwner());
        if (RelativeActorsDistance > MinDistance) continue;

        if (AxisValue.Y < 0.f)
        {
            if (GetDistanceFromTarget(Interface) < GetDistanceFromTarget(NearestTarget)) continue;
        }
        else
        {
            if (GetDistanceFromTarget(Interface) > GetDistanceFromTarget(NearestTarget)) continue;
        }

        MinDistance = RelativeActorsDistance;
        NewNearestTarget = Interface;
    }
    return NewNearestTarget;
}

AActor* UTargetSystemComponent::GetLockedOnTargetActor() const
{
    if (NearestTarget == nullptr) return nullptr;
	return NearestTarget->GetTargetSystemDependencies()->GetOwner();
}

bool UTargetSystemComponent::IsLocked() const
{
	return bTargetLocked && NearestTarget;
}

float UTargetSystemComponent::GetAngleUsingCameraRotation(const FVector& Location) const
{
    const UCameraComponent* CameraComponent = OwnerActor->FindComponentByClass<UCameraComponent>();
    if (!IsValid(CameraComponent))
    {
        return GetAngleUsingCharacterRotation(Location);
    }

    const FRotator CameraWorldRotation = CameraComponent->GetComponentRotation();
    const FRotator LookAtRotation = FindLookAtRotation(CameraComponent->GetComponentLocation(), Location);

    float YawAngle = CameraWorldRotation.Yaw - LookAtRotation.Yaw;
    if (YawAngle < 0)
    {
        YawAngle = YawAngle + 360;
    }

    return YawAngle;
}

float UTargetSystemComponent::GetAngleUsingCharacterRotation(const FVector& Location) const
{
    const FRotator CharacterRotation = OwnerActor->GetActorRotation();
    const FRotator LookAtRotation = FindLookAtRotation(OwnerActor->GetActorLocation(), Location);

    float YawAngle = CharacterRotation.Yaw - LookAtRotation.Yaw;
    if (YawAngle < 0)
    {
        YawAngle = YawAngle + 360;
    }

    return YawAngle;
}

FRotator UTargetSystemComponent::FindLookAtRotation(const FVector Start, const FVector Target)
{
	return FRotationMatrix::MakeFromX(Target - Start).Rotator();
}

void UTargetSystemComponent::ResetIsSwitchingTarget()
{
    if (!SwitchingTargetTimerHandle.IsValid())
    {
        GetWorld()->GetTimerManager().SetTimer(
             SwitchingTargetTimerHandle,
             this,
             &UTargetSystemComponent::ResetIsSwitchingTarget,
             bIsSwitchingTarget ? 0.25f : 0.5f,
             false
         );
        return;
    }

    GetWorld()->GetTimerManager().ClearTimer(SwitchingTargetTimerHandle);
	bIsSwitchingTarget = false;
}

bool UTargetSystemComponent::CanSwitchTarget(const FVector2D& AxisValue) const
{
	return FMath::Abs(AxisValue.X) >= StartRotatingThreshold || FMath::Abs(AxisValue.Y) >= StartRotatingThreshold;
}

void UTargetSystemComponent::CreateAndAttachTargetLockedOnWidgetComponent(const TargetInterface Interface)
{
    AActor* TargetActor = Interface.GetInterface()->GetTargetSystemDependencies()->GetOwner();
    if (!IsValid(TargetActor)) return;

    const TArray<UBTargetPoint*> TargetPoints = GetTargetDetails(Interface).TargetPoints;
    if (TargetPoints.IsEmpty()) return;

    int32 Index = 0;
    for (int32 i = 0; i < GetTargetDetails(Interface).TargetPoints.Num(); ++i)
    {
        if (GetTargetDetails(Interface).TargetPoints[i]->GetName() == CurrentSocketOnNearestTarget)
        {
            Index = i;
            break;
        }
    }

	if (!LockedOnWidgetClass)
	{
		TS_LOG(Error, TEXT("TargetSystemComponent: Cannot get LockedOnWidgetClass, please ensure it is a valid reference in the Component Properties."));
		return;
	}

	TargetLockedOnWidgetComponent = NewObject<UWidgetComponent>(TargetActor, MakeUniqueObjectName(TargetActor, UWidgetComponent::StaticClass(), FName("TargetLockOn")));
	TargetLockedOnWidgetComponent->SetWidgetClass(LockedOnWidgetClass);

	UMeshComponent* MeshComponent = TargetActor->FindComponentByClass<UMeshComponent>();
	USceneComponent* ParentComponent = MeshComponent ? MeshComponent : TargetActor->GetRootComponent();

	if (IsValid(OwnerPlayerController))
	{
		TargetLockedOnWidgetComponent->SetOwnerPlayer(OwnerPlayerController->GetLocalPlayer());
	}

	TargetLockedOnWidgetComponent->ComponentTags.Add(FName("TargetSystem.LockOnWidget"));
	TargetLockedOnWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	TargetLockedOnWidgetComponent->SetupAttachment(TargetPoints[Index]);
	TargetLockedOnWidgetComponent->SetRelativeLocation(LockedOnWidgetRelativeLocation);
	TargetLockedOnWidgetComponent->SetDrawSize(FVector2D(LockedOnWidgetDrawSize, LockedOnWidgetDrawSize));
	TargetLockedOnWidgetComponent->SetVisibility(true);
	TargetLockedOnWidgetComponent->RegisterComponent();
}

void UTargetSystemComponent::AddPotentialTargetsByInterface(const TSubclassOf<AActor>& ActorClass)
{
	for (TActorIterator ActorIterator(GetWorld(), ActorClass); ActorIterator; ++ActorIterator)
	{
	    TScriptInterface<ITargetSystemInterface> Interface = TScriptInterface<ITargetSystemInterface>(*ActorIterator);
	    if(!ObjectIsTargetable(Interface)) continue;

	    if (GetDistanceFromTarget(Interface) > MaximumDistanceToPotentialTargets) continue;

        PotentialTargets.Add(Interface);
	}
}

bool UTargetSystemComponent::ObjectIsTargetable(const TScriptInterface<ITargetSystemInterface> Actor) const
{
    if(!Actor) return false;
    return GetTargetDetails(Actor).bCouldBeTarget;
}

int32 UTargetSystemComponent::GetPointIndexByName(const FString& Name) const
{
    constexpr int32 InvalidIndex = -1;
    if (!NearestTarget) return InvalidIndex;
    for (int32 i = 0; i < GetTargetDetails(NearestTarget).TargetPoints.Num(); ++i)
    {
        if (GetTargetDetails(NearestTarget).TargetPoints[i]->GetName() != Name) continue;

        return i;
    }
    return InvalidIndex;
}

void UTargetSystemComponent::SetupLocalPlayerController()
{
	if (!IsValid(OwnerPawn))
	{
		TS_LOG(Error, TEXT("[%s] TargetSystemComponent: Component is meant to be added to Pawn only ..."), *GetName());
		return;
	}

	OwnerPlayerController = Cast<APlayerController>(OwnerPawn->GetController());
}

void UTargetSystemComponent::SortPotentialTargetsByDistance(TArray<TScriptInterface<ITargetSystemInterface>>& Array)
{
    if (Array.IsEmpty()) return;

    Array.Sort([this](const TScriptInterface<ITargetSystemInterface> A, const TScriptInterface<ITargetSystemInterface> B)
        {
            return GetDistanceFromTarget(A) < GetDistanceFromTarget(B);
        }
    );
}

void UTargetSystemComponent::SortPotentialTargetsByAngle(TArray<TScriptInterface<ITargetSystemInterface>>& Array)
{
    if (Array.IsEmpty()) return;

    Array.Sort([this](const TScriptInterface<ITargetSystemInterface> A, const TScriptInterface<ITargetSystemInterface> B)
        {
            return GetAngleUsingCameraRotation(GetTargetOwnerLocation(A)) < GetAngleUsingCameraRotation(GetTargetOwnerLocation(B));
        }
    );
}

TScriptInterface<ITargetSystemInterface> UTargetSystemComponent::FindNearestTarget(bool bUseAngle)
{
    if (PotentialTargets.IsEmpty()) return nullptr;
    SortPotentialTargetsByDistance(PotentialTargets);

    TArray<TScriptInterface<ITargetSystemInterface>> CopyPotentialTargets = {};
    bool bFindNearestTarget = false;
    int32 BestTargetByDistance_Index = -1;

    for (int32 i = 0; i < PotentialTargets.Num(); ++i)
    {
        FHitResult Hit;
        if(!LineTrace(GetOwner()->GetActorLocation(), GetTargetOwnerLocation(PotentialTargets[i]), Hit)) continue;

        const float Distance = GetDistanceFromTarget(PotentialTargets[i]);

        if (Distance > MaximumDistanceCanStartTarget) continue;

        if (!bIgnoreViewport && !IsInViewport(PotentialTargets[i]) && Distance > DangerousDistanceToTarget) continue;

        if (!bFindNearestTarget)
        {
            bFindNearestTarget = true;
            BestTargetByDistance_Index = i;
        }

        if (!bUseAngle) break;

        CopyPotentialTargets.Add(PotentialTargets[i]);
    }
    if (BestTargetByDistance_Index < 0) return nullptr;
    if (!bUseAngle || bIgnoreViewport) return PotentialTargets[BestTargetByDistance_Index];

    SortPotentialTargetsByAngle(CopyPotentialTargets);

    for (int32 i = 0; i < CopyPotentialTargets.Num(); ++i)
    {
        if (GetAngleUsingCameraRotation(GetTargetOwnerLocation(CopyPotentialTargets[i])) > MaximumFindAngle) continue;

        const float Distance = GetDistanceFromTarget(CopyPotentialTargets[i]);
        if (GetDistanceFromTarget(PotentialTargets[BestTargetByDistance_Index]) + ExtraDistanceToLimitWhenSearchingByAngle < Distance) continue;

        return CopyPotentialTargets[i];
    }
    return PotentialTargets[BestTargetByDistance_Index];
}

bool UTargetSystemComponent::LineTrace(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());

	TArray<AActor*> IgnoredActors {};
    IgnoredActors.Init(OwnerActor, 1);
    for (AActor* ChildActor : OwnerActor->Children)
    {
        IgnoredActors.Add(ChildActor);
    }
    Params.AddIgnoredActors(IgnoredActors);
    GetWorld()->LineTraceSingleByChannel(
        Hit,
        Start,
        End,
        TargetCollisionChannel,
        Params
    );

    return Hit.HitObjectHandle.GetLocation() == End;
}

FRotator UTargetSystemComponent::GetControlRotationOnTarget(TargetInterface Interface) const
{
    if (!Interface) return FRotator::ZeroRotator;

	if (!IsValid(OwnerPlayerController))
	{
		TS_LOG(Warning, TEXT("UTargetSystemComponent::GetControlRotationOnTarget - OwnerPlayerController is not valid ..."))
		return FRotator::ZeroRotator;
	}

	const FRotator ControlRotation = OwnerPlayerController->GetControlRotation();

	const FVector CharacterLocation = OwnerActor->GetActorLocation();
    FVector TargetPointLocation = GetTargetOwnerLocation(Interface);

    // for (int32 i = 0; i < GetTargetDetails(Interface).TargetPoints.Num(); ++i)
    // {
    //     if (CurrentSocketOnNearestTarget == GetTargetDetails(Interface).TargetPoints[i]->GetName())
    //     {
    //         TargetPointLocation = GetTargetDetails(NearestTarget).TargetPoints[i]->GetComponentLocation();
    //     }
    // }

	// Find look at rotation
	const FRotator LookRotation = FRotationMatrix::MakeFromX(TargetPointLocation - CharacterLocation).Rotator();
	float Pitch = LookRotation.Pitch;
	FRotator TargetRotation;
	if (bAdjustPitchBasedOnDistanceToTargetUsingCurve)
	{
		const float Distance = GetDistanceFromTarget(Interface);
        const int32 Index = GetPointIndexByName(CurrentSocketOnNearestTarget);
	    const UCurveFloat* CurvePitch = Index >= 0 && IsValid(GetTargetDetails(Interface).TargetPoints[Index]->GetPitchOffsetCurve()) ?
	            GetTargetDetails(Interface).TargetPoints[Index]->GetPitchOffsetCurve():
	            DefaultPitchOffsetCurve;

		const float CurveValue = IsValid(CurvePitch) ? CurvePitch->GetFloatValue(Distance) : 0.f;
		TargetRotation = FRotator(CurveValue, LookRotation.Yaw, ControlRotation.Roll);
	}
	else if (bAdjustPitchBasedOnDistanceToTarget)
	{
		const float DistanceToTarget = GetDistanceFromTarget(Interface);
		const float PitchInRange = (DistanceToTarget * PitchDistanceCoefficient + PitchDistanceOffset) * -1.0f;
		const float PitchOffset = FMath::Clamp(PitchInRange, PitchMin, PitchMax);

		Pitch = Pitch + PitchOffset;
		TargetRotation = FRotator(Pitch, LookRotation.Yaw, ControlRotation.Roll);
	}
	else
	{
	    TargetRotation = FRotator(Pitch, LookRotation.Yaw, ControlRotation.Roll);
	}

	return FMath::RInterpTo(ControlRotation, TargetRotation, GetWorld()->GetDeltaSeconds(), 9.0f);
}

FTargetActorDetails UTargetSystemComponent::GetTargetDetails(const TargetInterface& Interface) const
{
    if (!Interface) return {};
    return Interface->GetTargetSystemDependencies()->GetTargetActorDetails();
}

FVector UTargetSystemComponent::GetTargetOwnerLocation(const TargetInterface& Interface) const
{
    if (!Interface) return FVector::Zero();
    return Interface->GetTargetSystemDependencies()->GetOwner()->GetActorLocation();
}

void UTargetSystemComponent::SetControlRotationOnTarget() const
{
	if (!IsValid(OwnerPlayerController)) return;
    if (!NearestTarget) return;

	const FRotator ControlRotation = GetControlRotationOnTarget(NearestTarget);
    OwnerPlayerController->SetControlRotation(ControlRotation);
}

float UTargetSystemComponent::GetDistanceFromTarget(const TargetInterface& Interface) const
{
	return OwnerActor->GetDistanceTo(Interface->GetTargetSystemDependencies()->GetOwner());
}

void UTargetSystemComponent::ControlRotation(const bool ShouldControlRotation) const
{
	if (!IsValid(OwnerPawn))
	{
		return;
	}

    if (CharacterRotationMode == ECharacterRotationMode::OrientToMovement)
    {
        OwnerPawn->bUseControllerRotationYaw = ShouldControlRotation;

        UCharacterMovementComponent* CharacterMovementComponent = OwnerPawn->FindComponentByClass<UCharacterMovementComponent>();
        if (IsValid(CharacterMovementComponent))
        {
            CharacterMovementComponent->bOrientRotationToMovement = !ShouldControlRotation;
        }
    }
}

bool UTargetSystemComponent::IsInViewport(TargetInterface Interface) const
{
	if (!IsValid(OwnerPlayerController)) return true;

	FVector2D ScreenLocation;
	OwnerPlayerController->ProjectWorldLocationToScreen(GetTargetOwnerLocation(Interface), ScreenLocation);

	FVector2D ViewportSize;
	GetWorld()->GetGameViewport()->GetViewportSize(ViewportSize);

	return ScreenLocation.X > 10.f && ScreenLocation.Y > 10.f && ScreenLocation.X < ViewportSize.X && ScreenLocation.Y < ViewportSize.Y;
}
