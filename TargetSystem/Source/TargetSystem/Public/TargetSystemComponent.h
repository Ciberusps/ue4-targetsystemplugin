// Copyright 2018-2021 Mickael Daniel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TargetSystemInterface.h"
#include "Components/ActorComponent.h"
#include "TargetSystemComponent.generated.h"

struct FTargetActorDetails;
using TargetInterface = TScriptInterface<ITargetSystemInterface>;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFinishTargetLock);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnTargetIsDead,
    TScriptInterface<ITargetSystemInterface>, Interface
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FComponentOnTargetLockedOnOff,
    const AActor*, TargetActor
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FComponentSetRotation,
    const AActor*, TargetActor,
    FRotator&, ControlRotation
);

UENUM(BlueprintType)
enum class ECharacterRotationMode : uint8
{
    OrientToMovement,
    Strafe,
};

class UUserWidget;
class UWidgetComponent;
class APlayerController;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TARGETSYSTEM_API UTargetSystemComponent final: public UActorComponent
{
	GENERATED_BODY()

public:
	UTargetSystemComponent();

    void SetUp(
        bool bAdjustPitchBasedOnDistanceToTarget,
        bool bAdjustPitchBasedOnDistanceToTargetUsingCurve
    );

    UPROPERTY(BlueprintAssignable, Category = "Target System | Delegates")
    FOnFinishTargetLock OnFinishTargetLock;

    UPROPERTY(BlueprintAssignable, Category = "Target System | Delegates")
    FOnTargetIsDead OnTargetIsDead;

    UPROPERTY(BlueprintAssignable, Category = "Target System")
    FComponentOnTargetLockedOnOff OnTargetLockedOff;

    UPROPERTY(BlueprintAssignable, Category = "Target System")
    FComponentOnTargetLockedOnOff OnTargetLockedOn;

    UFUNCTION(BlueprintCallable, Category = "Target System")
    bool IsLocked() const;

    UFUNCTION(BlueprintCallable, Category = "Target System")
    AActor* GetLockedOnTargetActor() const;

    UFUNCTION(BlueprintCallable, Category = "Target System")
    void TryStartTargetLock();

    UFUNCTION(BlueprintCallable, Category = "Target System")
    void StopObservingTarget(const bool bIgnoreAutoSwitch = false, const bool bTargetIsDead = false);

    UFUNCTION(BlueprintCallable, Category = "Target System")
    void ControlRotation(bool ShouldControlRotation) const;

    void SwitchTarget(FVector2D AxisValue);

protected:
    virtual void BeginPlay() override;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Base params
    UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Target System")
    TSubclassOf<AActor> RequiredClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System")
    ECharacterRotationMode CharacterRotationMode = ECharacterRotationMode::OrientToMovement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System")
	bool bIgnoreViewport = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System")
    bool bAutoTargetSwitch = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System")
    float StartRotatingThreshold = 0.85f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System")
    TEnumAsByte<ECollisionChannel> TargetCollisionChannel;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System")
    float BreakLineOfSightDelay = 2.0f;

    // Optimization
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Optimization")
    float TimerTick = 0.5f;

    // Distance Settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Distance Settings")
    float DangerousDistanceToTarget = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Distance Settings")
	float MaximumDistanceCanStartTarget = 3000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Distance Settings")
    float LoseTargetDistance = 4000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Distance Settings")
    float MaximumDistanceToPotentialTargets = 2400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Distance Settings")
    float MaximumFindAngle = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Distance Settings")
    float ExtraDistanceToLimitWhenSearchingByAngle = 300.0f;

    // Widget
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Widget")
	TSubclassOf<UUserWidget> LockedOnWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Widget")
	float LockedOnWidgetDrawSize = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Widget")
	FString CurrentSocketOnNearestTarget = FString("spine_03");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Widget")
	FVector LockedOnWidgetRelativeLocation = FVector(0.0f, 0.0f, 0.0f);

    // Pitch Offset using Curve
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset using Curve")
	bool bAdjustPitchBasedOnDistanceToTargetUsingCurve = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset using Curve")
	UCurveFloat* DefaultPitchOffsetCurve = nullptr;

    // Pitch Offset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset")
	bool bAdjustPitchBasedOnDistanceToTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset")
	float PitchDistanceCoefficient = -0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset")
	float PitchDistanceOffset = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset")
	float PitchMin = -50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target System | Pitch Offset")
	float PitchMax = -20.0f;

    // TODO Delete
	UPROPERTY(BlueprintAssignable, Category = "Target System")
	FComponentSetRotation OnTargetSetRotation;

private:
	UPROPERTY()
	AActor* OwnerActor = nullptr;

	UPROPERTY()
	APawn* OwnerPawn = nullptr;

	UPROPERTY()
	APlayerController* OwnerPlayerController = nullptr;

	UPROPERTY()
	UWidgetComponent* TargetLockedOnWidgetComponent = nullptr;

    UPROPERTY()
    TArray<TScriptInterface<ITargetSystemInterface>> PotentialTargets;

    UPROPERTY()
    TScriptInterface<ITargetSystemInterface> NearestTarget;

	bool bIsSwitchingTarget = false;
	bool bTargetLocked = false;

    FTimerHandle SwitchingTargetTimerHandle;
    FTimerHandle ObservingTimer;
    FTimerHandle BehindWallTimer;

    bool CanTargetLock() const;
    bool CanSwitchTarget(const FVector2D& AxisValue) const;
    bool IsInViewport(TargetInterface TargetActor) const;
    bool ObjectIsTargetable(const TargetInterface Interface) const;

    int32 GetPointIndexByName(const FString& Name) const;
    float GetDistanceFromTarget(const TargetInterface& Interface) const;
    float GetAngleUsingCameraRotation(const FVector& Location) const;
    float GetAngleUsingCharacterRotation(const FVector& Location) const;
    FRotator GetControlRotationOnTarget(TargetInterface Interface) const;
    FTargetActorDetails GetTargetDetails(const TargetInterface& Interface) const;
    FVector GetTargetOwnerLocation(const TargetInterface& Interface) const;

    void SetControlRotationOnTarget() const;
    void SetupLocalPlayerController();

    void AddPotentialTargetsByInterface(const TSubclassOf<AActor>& ActorClass);
    bool LineTrace(const FVector& Start, const FVector& End, FHitResult& Hit) const;
	void CreateAndAttachTargetLockedOnWidgetComponent(const TargetInterface Interface);
	void ResetIsSwitchingTarget();

    void StartObservingTarget();
    void UpdateTargetInfo();
    bool TrySwitchBetweenTargetPoints(FVector2D AxisValue);
    void AutoSwitchTarget();
    void StopTargetLock();
    void MessageFinishTargetLock() const;

    void SortPotentialTargetsByDistance(TArray<TScriptInterface<ITargetSystemInterface>>& Array);
    void SortPotentialTargetsByAngle(TArray<TScriptInterface<ITargetSystemInterface>>& Array);

    TargetInterface FindNearestTarget(bool bUseAngle = false);
    TargetInterface FindByHorizontal(TArray<TargetInterface> LookTargets, float AxisValue) const;
    TargetInterface FindByVertical(TArray<TargetInterface> LookTargets, FVector2D AxisValue) const;
    static FRotator FindLookAtRotation(const FVector Start, const FVector Target);
};
