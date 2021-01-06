#include "T3DParser.h"

#include "Layers/LayersSubsystem.h"

#include "UDKImportPluginPrivatePCH.h"

DEFINE_LOG_CATEGORY(UDKImportPluginLog);

float T3DParser::UnrRotToDeg = 0.00549316540360483;
float T3DParser::IntensityMultiplier = 5000;

T3DParser::T3DParser(const FString &UdkPath, const FString &TmpPath)
{
	this->UdkPath = UdkPath;
	this->TmpPath = TmpPath;
}

inline bool IsWhitespace(TCHAR c) 
{ 
	return c == LITERAL(TCHAR, ' ') || c == LITERAL(TCHAR, '\t') || c == LITERAL(TCHAR, '\r');
}

void T3DParser::ResetParser(const FString &Content)
{
	LineIndex = 0;
	ParserLevel = 0;
	Content.ParseIntoArray(Lines, TEXT("\n"), true);
}

bool T3DParser::NextLine()
{
	if (LineIndex < Lines.Num())
	{
		int32 Start, End;
		const FString &String = Lines[LineIndex];
		Start = 0;
		End = String.Len();

		// Trimming
		while (Start < End && IsWhitespace(String[Start]))
		{
			++Start;
		}

		while (End > Start && IsWhitespace(String[End-1]))
		{
			--End;
		}

		Line = String.Mid(Start, End - Start);
		++LineIndex;
		return true;
	}
	return false;
}

bool T3DParser::IgnoreSubObjects()
{
	while (Line.StartsWith(TEXT("Begin Object "), ESearchCase::CaseSensitive))
	{
		JumpToEnd();
		if (!NextLine())
			return false;
	}

	return true;
}

bool T3DParser::IgnoreSubs()
{
	while (Line.StartsWith(TEXT("Begin "), ESearchCase::CaseSensitive))
	{
		JumpToEnd();
		if (!NextLine())
			return false;
	}

	return true;
}

void T3DParser::JumpToEnd()
{
	int32 Level = 1;
	while (NextLine())
	{
		if (Line.StartsWith(TEXT("Begin "), ESearchCase::CaseSensitive))
		{
			++Level;
		}
		else if (Line.StartsWith(TEXT("End "), ESearchCase::CaseSensitive))
		{
			--Level;
			if (Level == 0)
				break;
		}
	}
}

bool T3DParser::IsBeginObject(FString &Class)
{
	if (Line.StartsWith(TEXT("Begin Object "), ESearchCase::CaseSensitive))
	{
		GetOneValueAfter(TEXT(" Class="), Class);
		return true;
	}
	return false;
}

bool T3DParser::IsEndObject()
{
	return Line.Equals(TEXT("End Object"));
}

bool T3DParser::GetOneValueAfter(const FString &Key, FString &Value, int32 maxindex)
{
	int32 start = Line.Find(Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
	if (start != -1 && start <= maxindex)
	{
		start += Key.Len();

		const TCHAR * Buffer = *Line + start;
		if (*Buffer == TCHAR('"'))
		{
			++start;
			++Buffer;
			bool Escaping = false;
			while (*Buffer && (*Buffer != TCHAR('"') || Escaping))
			{
				if (Escaping)
					Escaping = false;
				else if (*Buffer == TCHAR('\\'))
					Escaping = true;
				++Buffer;
			}
		}
		else if(*Buffer == TCHAR('('))
		{
			++Buffer;
			int Level = 1;
			while (*Buffer && Level != 0)
			{
				if (*Buffer == TCHAR('('))
					++Level;
				else if (*Buffer == TCHAR(')'))
					--Level;
				++Buffer;
			}
		}
		else
		{
			while (*Buffer && *Buffer != TCHAR(' ') && *Buffer != TCHAR(',') && *Buffer != TCHAR(')'))
			{
				++Buffer;
			}
		}
		Value = Line.Mid(start, Buffer - *Line - start);

		return true;
	}
	return false;
}

void T3DParser::AddRequirement(const FString &UDKRequiredObjectName, UObjectDelegate Action)
{
	FRequirement Requirement;
	if (!ParseResourceUrl(UDKRequiredObjectName, Requirement))
	{
		UE_LOG(UDKImportPluginLog, Warning, TEXT("Unable to parse ressource url : %s"), *UDKRequiredObjectName);
		return;
	}
	AddRequirement(Requirement, Action);
}

void T3DParser::AddRequirement(const FRequirement &Requirement, UObjectDelegate Action)
{
	auto* ExistingPair = FindRequirement(Requirement);
	if (ExistingPair && ExistingPair->Value.ResolvedObject)
	{
		Action.Execute(ExistingPair->Value.ResolvedObject);
	}
	else
	{
		if (ExistingPair)
		{
			// Append action to existing requirement actions.
			ExistingPair->Value.Actions.Add(Action);
		}
		else
		{
			FRequirementFixups Fixups;
			Fixups.Actions.Add(Action);
			
			Requirements.Emplace(Requirement, Fixups);
		}
	}
}

void T3DParser::FixRequirement(const FString& UDKRequiredObjectName, UObject* Object)
{
	FRequirement Requirement;
	if (!ParseResourceUrl(UDKRequiredObjectName, Requirement))
	{
		UE_LOG(UDKImportPluginLog, Warning, TEXT("Unable to parse ressource url : %s"), *UDKRequiredObjectName);
		return;
	}

	auto* Pair = FindRequirement(Requirement);
	if (Pair)
	{
		FixRequirement(*Pair, Object);
	}
	else
	{
		// Add the requirement to our list as an already-fixed requirement.
		FRequirementFixups Fixups;
		Fixups.ResolvedObject = Object;

		Requirements.Emplace(Requirement, Fixups);
	}
}

void T3DParser::FixRequirement(TPair<FRequirement, FRequirementFixups>& Pair, UObject* Object)
{
	if (Object == NULL)
		return;

	if (Pair.Value.ResolvedObject)
	{
		UE_LOG(UDKImportPluginLog, Warning, TEXT("Fixing up already resolved requirement? : %s"), *Pair.Key.Name);
		return;
	}

	for (auto IterActions = Pair.Value.Actions.CreateConstIterator(); IterActions; ++IterActions)
		IterActions->ExecuteIfBound(Object);

	Pair.Value.ResolvedObject = Object;
}

TPair<T3DParser::FRequirement, T3DParser::FRequirementFixups>* T3DParser::FindRequirement(const FRequirement& Requirement)
{
	for (auto& Pair : Requirements)
	{
		if (Pair.Key == Requirement)
			return &Pair;
	}

	return nullptr;
}

bool T3DParser::FindRequirement(const FString &UDKRequiredObjectName, UObject * &Object)
{
	FRequirement Requirement;
	if (!ParseResourceUrl(UDKRequiredObjectName, Requirement))
	{
		UE_LOG(UDKImportPluginLog, Warning, TEXT("Unable to parse ressource url : %s"), *UDKRequiredObjectName);
		return false;
	}
	return FindRequirement(Requirement, Object);
}

bool T3DParser::FindRequirement(const FRequirement &Requirement, UObject * &Object)
{
	auto* Pair = FindRequirement(Requirement);
	if (!Pair)
		return false;

	Object = Pair->Value.ResolvedObject;
	return Pair->Value.ResolvedObject != nullptr;
}

void T3DParser::PrintMissingRequirements()
{
	for (auto Iter = Requirements.CreateConstIterator(); Iter; ++Iter)
	{
		const FRequirement &Requirement = (*Iter).Key;
		const auto& Fixups = (*Iter).Value;

		if (!Fixups.ResolvedObject)
			UE_LOG(UDKImportPluginLog, Error, TEXT("Missing requirements : %s"), *Requirement.Url);
	}
}

bool T3DParser::ParseUDKRotation(const FString &InSourceString, FRotator &Rotator)
{
	int32 Pitch = 0;
	int32 Yaw = 0;
	int32 Roll = 0;

	const bool bSuccessful = FParse::Value(*InSourceString, TEXT("Pitch="), Pitch) && FParse::Value(*InSourceString, TEXT("Yaw="), Yaw) && FParse::Value(*InSourceString, TEXT("Roll="), Roll);

	Rotator.Pitch = Pitch * UnrRotToDeg;
	Rotator.Yaw = Yaw * UnrRotToDeg;
	Rotator.Roll = Roll * UnrRotToDeg;

	return bSuccessful;

}

bool T3DParser::ParseFVector(const TCHAR* Stream, FVector& Value)
{
	Value = FVector::ZeroVector;

	Value.X = FCString::Atof(Stream);
	Stream = FCString::Strchr(Stream, ',');
	if (!Stream)
	{
		return false;
	}

	Stream++;
	Value.Y = FCString::Atof(Stream);
	Stream = FCString::Strchr(Stream, ',');
	if (!Stream)
	{
		return false;
	}

	Stream++;
	Value.Z = FCString::Atof(Stream);

	return true;
}

bool T3DParser::IsProperty(FString &PropertyName, FString &Value)
{
	int32 Index;
	if (Line.FindChar('=', Index) && Index > 0)
	{
		PropertyName = Line.Mid(0, Index);
		return GetOneValueAfter(PropertyName + TEXT("="), Value);
	}

	return false;
}

bool T3DParser::IsParameter(const FString& Key, int32& index, FString& Value)
{
	const TCHAR* Stream = *Line;

	if (FParse::Command(&Stream, *Key) && *Stream == TCHAR('('))
	{
		++Stream;
		index = FCString::Atoi(Stream);
		while (FChar::IsAlnum(*Stream))
		{
			++Stream;
		}
		if (*Stream == TCHAR(')'))
		{
			++Stream;
			if (*Stream == TCHAR('='))
			{
				++Stream;
				Value = Stream;
				return true;
			}
		}
	}

	return false;
}

bool T3DParser::IsActorLocation(AActor * Actor)
{
	FString Value;
	if (GetProperty(TEXT("Location="), Value))
	{
		FVector Location;
		ensure(Location.InitFromString(Value));
		Actor->SetActorLocation(Location);
		return true;
	}

	return false;
}

bool T3DParser::IsActorRotation(AActor * Actor)
{
	FString Value;
	if (GetProperty(TEXT("Rotation="), Value))
	{
		FRotator Rotator;
		ensure(ParseUDKRotation(Value, Rotator));
		Actor->SetActorRotation(Rotator);
		return true;
	}

	return false;
}

bool T3DParser::IsActorScale(AActor * Actor)
{
	FString Value;
	if (GetProperty(TEXT("DrawScale="), Value))
	{
		float DrawScale = FCString::Atof(*Value);
		Actor->SetActorScale3D(Actor->GetActorScale() * DrawScale);
		return true;
	}
	else if (GetProperty(TEXT("DrawScale3D="), Value))
	{
		FVector DrawScale3D;
		ensure(DrawScale3D.InitFromString(Value));
		Actor->SetActorScale3D(Actor->GetActorScale() * DrawScale3D);
		return true;
	}

	return false;
}

bool T3DParser::IsActorProperty(AActor * Actor)
{
	FString Value;
	if (GetProperty(TEXT("Layer="), Value))
	{
		GEditor->GetEditorSubsystem<ULayersSubsystem>()->AddActorToLayer(Actor, FName(*Value));
		return true;
	}

	return false;
}

int32 T3DParser::RunUDK(const FString &CommandLine)
{
	FString Output;
	return RunUDK(CommandLine, Output);
}

int32 T3DParser::RunUDK(const FString &CommandLine, FString &Output)
{
	FString StdErr;
	FScopedSlowTask Task(0.f, LOCTEXT("StatusRunUDK", "Running UDK Commandlet..."));

	void *ReadPipe, *WritePipe;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
		return -1;

	auto Process = FPlatformProcess::CreateProc(*(UdkPath / TEXT("Binaries/Win64/UDK.com")), *CommandLine, true, true, true, nullptr, 0, nullptr, WritePipe);
	while (FPlatformProcess::IsProcRunning(Process))
	{
		Task.EnterProgressFrame(0.f);
		FPlatformProcess::Sleep(0.05f);

		Output += FPlatformProcess::ReadPipe(ReadPipe);
	}

	int32 Code;
	if (FPlatformProcess::GetProcReturnCode(Process, &Code))
	{
		if (Code != 0)
		{
			UE_LOG(UDKImportPluginLog, Error, TEXT("UDK failed with code 0x%08X, output follows:\n%s"), Code, *Output);
		}
		else
		{
			UE_LOG(UDKImportPluginLog, Verbose, TEXT("UDK output follows:\n%s"), *Output);
		}

		return Code;
	}

	UE_LOG(UDKImportPluginLog, Error, TEXT("UDK output follows:\n%s"), *Output);
	return -1;
}

bool T3DParser::ConvertOBJToFBX(const FString &ObjFileName, const FString &FBXFilename)
{
	const FString CommandLine = FString::Printf(TEXT("\"%s\" \"%s\""), *ObjFileName, *FBXFilename);
	const FString Program = TEXT("C:\\Program Files (x86)\\Autodesk\\FBX\\FBX Converter\\2013.3\\bin\\FbxConverter.exe");
	FString StdOut, StdErr;
	int32 exitCode;

	if (FPlatformProcess::ExecProcess(*Program, *CommandLine, &exitCode, &StdOut, &StdErr))
	{
		return exitCode == 0;
	}

	return false;
}

void T3DParser::ParseResourceUrl(const FString &Url, FString &Package, FString &Name)
{
	int32 PackageIndex, NameIndex;

	PackageIndex = Url.Find(".", ESearchCase::CaseSensitive, ESearchDir::FromStart);

	if (PackageIndex == -1)
	{
		Package = Url;
		Name = FString();
	}
	else
	{
		Package = Url.Mid(0, PackageIndex);
		NameIndex = Url.Find(".", ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Name = Url.Mid(NameIndex + 1);
	}
}

bool T3DParser::ParseResourceUrl(const FString &Url, FString &Type, FString &Package, FString &Name)
{
	int32 Index, PackageIndex, NameIndex;

	if (!Url.FindChar('\'', Index) || !Url.EndsWith(TEXT("'")))
		return false;

	Type = Url.Mid(0, Index);
	++Index;
	PackageIndex = Url.Find(".", ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);

	if (PackageIndex == -1)
	{
		// Package Name is the current Package
		Package = this->Package;
		Name = Url.Mid(Index, Url.Len() - Index - 1);
	}
	else
	{
		Package = Url.Mid(Index, PackageIndex - Index);
		NameIndex = Url.Find(".", ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Name = Url.Mid(NameIndex + 1, Url.Len() - NameIndex - 2);
	}

	return true;
}
