#pragma once

#include "CoreMinimal.h"

#define LOCTEXT_NAMESPACE "UDKImportPlugin"

DECLARE_LOG_CATEGORY_EXTERN(UDKImportPluginLog, Log, All);
DECLARE_DELEGATE_OneParam(UObjectDelegate, UObject*);

class T3DParser
{
public:
	struct FRequirement
	{
		FString Type, Package, Name, OriginalUrl, Url;

		FORCEINLINE friend uint32 GetTypeHash(const FRequirement& req)
		{
			return GetTypeHash(req.Url);
		}
	};

	struct FRequirementFixups
	{
		TArray<UObjectDelegate> Actions; // Fixup actions
		UObject* ResolvedObject = nullptr; // Object resolved from this requirement
	};

protected:
	static float UnrRotToDeg;
	static float IntensityMultiplier;

	T3DParser(const FString &UdkPath, const FString &TmpPath);

	int32 StatusNumerator, StatusDenominator;

	/// UDK
	FString UdkPath, TmpPath;
	int32 RunUDK(const FString &CommandLine);
	int32 RunUDK(const FString &CommandLine, FString &output);

	/// Resources requirements
	TArray<TPair<FRequirement, FRequirementFixups>> Requirements;

	bool FindRequirement(const FRequirement &Requirement, UObject * &Object);
	TPair<FRequirement, FRequirementFixups>* FindRequirement(const FRequirement& Requirement);

	bool ConvertOBJToFBX(const FString &ObjFileName, const FString &FBXFilename);
	void AddRequirement(const FString &UDKRequiredObjectName, UObjectDelegate Action);
	void FixRequirement(const FString& UDKRequiredObjectName, UObject* Object);
	bool FindRequirement(const FString &UDKRequiredObjectName, UObject * &Object);
	void AddRequirement(const FRequirement &Requirement, UObjectDelegate Action);
	void FixRequirement(TPair<FRequirement, FRequirementFixups>& Pair, UObject* Object);
	void PrintMissingRequirements();

	/// Line parsing
	int32 LineIndex, ParserLevel;
	TArray<FString> Lines;
	FString Line, Package;
	void ResetParser(const FString &Content);
	bool NextLine();
	bool IgnoreSubs();
	bool IgnoreSubObjects();
	void JumpToEnd();

	/// Line content parsing
	bool IsBeginObject(FString &Class);
	bool IsEndObject();
	bool IsProperty(FString &PropertyName, FString &Value);
	bool IsParameter(const FString& Key, int32& index, FString& Value);
	bool IsActorLocation(AActor * Actor);
	bool IsActorRotation(AActor * Actor);
	bool IsActorScale(AActor * Actor);
	bool IsActorProperty(AActor * Actor);

	/// Value parsing
	bool GetOneValueAfter(const FString &Key, FString &Value, int32 maxindex = MAX_int32);
	bool GetProperty(const FString &Key, FString &Value);
	bool ParseUDKRotation(const FString &InSourceString, FRotator &Rotator);
	bool ParseFVector(const TCHAR* Stream, FVector& Value);
	void ParseResourceUrl(const FString &Url, FString &Package, FString &Name);
	bool ParseResourceUrl(const FString &Url, FString &Type, FString &Package, FString &Name);
	bool ParseResourceUrl(const FString &Url, FRequirement &Requirement);
};

FORCEINLINE bool operator==(const T3DParser::FRequirement& A, const T3DParser::FRequirement& B)
{
	return A.Url == B.Url;
}

FORCEINLINE bool T3DParser::ParseResourceUrl(const FString &Url, FRequirement &Requirement)
{
	Requirement.OriginalUrl = Url;
	if (ParseResourceUrl(Url, Requirement.Type, Requirement.Package, Requirement.Name))
	{
		Requirement.Url = FString::Printf(TEXT("%s'%s.%s'"), *Requirement.Type, *Requirement.Package, *Requirement.Name);
		return true;
	}
	return false;
}

FORCEINLINE bool T3DParser::GetProperty(const FString &Key, FString &Value)
{
	return GetOneValueAfter(Key, Value, 0);
}
