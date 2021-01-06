#include "T3DMaterialInstanceConstantParser.h"

#include "UDKImportPluginPrivatePCH.h"
#include "T3DLevelParser.h"

T3DMaterialInstanceConstantParser::T3DMaterialInstanceConstantParser(T3DLevelParser * ParentParser, const FString &Package) : T3DParser(ParentParser->UdkPath, ParentParser->TmpPath)
{
	this->LevelParser = ParentParser;
	this->Package = Package;
	this->MaterialInstanceConstant = NULL;
}

UMaterialInstanceConstant* T3DMaterialInstanceConstantParser::ImportT3DFile(const FString &FileName)
{
	FString MaterialT3D;
	if (FFileHelper::LoadFileToString(MaterialT3D, *FileName))
	{
		ResetParser(MaterialT3D);
		MaterialT3D.Empty();
		return ImportMaterialInstanceConstant();
	}

	return NULL;
}

UMaterialInstanceConstant*  T3DMaterialInstanceConstantParser::ImportMaterialInstanceConstant()
{
	FString ClassName, Name, Value;
	int32 ParameterIndex;

	ensure(NextLine());
	ensure(IsBeginObject(ClassName));
	ensure(ClassName == TEXT("MaterialInstanceConstant"));
	ensure(GetOneValueAfter(TEXT(" Name="), Name));

	FString BasePackageName = FString::Printf(TEXT("/Game/UDK/%s/MaterialInstances"), *Package);
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialInstanceConstantFactoryNew* MaterialFactory = NewObject<UMaterialInstanceConstantFactoryNew>(UMaterialInstanceConstantFactoryNew::StaticClass());
	MaterialInstanceConstant = (UMaterialInstanceConstant*)AssetToolsModule.Get().CreateAsset(Name, BasePackageName, UMaterialInstanceConstant::StaticClass(), MaterialFactory);
	if (MaterialInstanceConstant == NULL)
	{
		return NULL;
	}

	MaterialInstanceConstant->Modify();

	while (NextLine() && IgnoreSubObjects() && !IsEndObject())
	{
		if (IsBeginObject(ClassName))
		{
			JumpToEnd();
		}
		else if (IsParameter(TEXT("TextureParameterValues"), ParameterIndex, Value))
		{
			if (ParameterIndex >= MaterialInstanceConstant->TextureParameterValues.Num())
				MaterialInstanceConstant->TextureParameterValues.SetNum(ParameterIndex + 1);

			FTextureParameterValue &Parameter = MaterialInstanceConstant->TextureParameterValues[ParameterIndex];
			Parameter.ExpressionGUID = FGuid::NewGuid();

			if (GetOneValueAfter(TEXT("ParameterValue="), Value))
			{
				FRequirement Requirement;
				if (ParseResourceUrl(Value, Requirement))
				{
					LevelParser->AddRequirement(Requirement, UObjectDelegate::CreateRaw(LevelParser, &T3DLevelParser::SetTextureParameterValue, MaterialInstanceConstant, ParameterIndex));
				}
				else
				{
					UE_LOG(UDKImportPluginLog, Warning, TEXT("Unable to parse ressource url : %s"), *Value);
				}
			}
			if (GetOneValueAfter(TEXT("ParameterName="), Value))
				Parameter.ParameterInfo.Name = *Value;
				
		}
		else if (IsParameter(TEXT("ScalarParameterValues"), ParameterIndex, Value))
		{
			if (ParameterIndex >= MaterialInstanceConstant->ScalarParameterValues.Num())
				MaterialInstanceConstant->ScalarParameterValues.SetNum(ParameterIndex + 1);

			FScalarParameterValue &Parameter = MaterialInstanceConstant->ScalarParameterValues[ParameterIndex];
			Parameter.ExpressionGUID = FGuid::NewGuid();

			if (GetOneValueAfter(TEXT("ParameterValue="), Value))
				Parameter.ParameterValue = FCString::Atof(*Value);
			if (GetOneValueAfter(TEXT("ParameterName="), Value))
				Parameter.ParameterInfo.Name = *Value;
		}
		else if (IsParameter(TEXT("VectorParameterValues"), ParameterIndex, Value))
		{
			if (ParameterIndex >= MaterialInstanceConstant->VectorParameterValues.Num())
				MaterialInstanceConstant->VectorParameterValues.SetNum(ParameterIndex + 1);

			FVectorParameterValue &Parameter = MaterialInstanceConstant->VectorParameterValues[ParameterIndex];
			Parameter.ExpressionGUID = FGuid::NewGuid();

			if (GetOneValueAfter(TEXT("ParameterValue="), Value))
				Parameter.ParameterValue.InitFromString(Value);
			if (GetOneValueAfter(TEXT("ParameterName="), Value))
				Parameter.ParameterInfo.Name = *Value;
		}
		else if (GetProperty(TEXT("Parent="), Value))
		{
			FRequirement Requirement;
			if (ParseResourceUrl(Value, Requirement))
			{
				LevelParser->AddRequirement(Requirement, UObjectDelegate::CreateRaw(LevelParser, &T3DLevelParser::SetParent, MaterialInstanceConstant));
			}
			else
			{
				UE_LOG(UDKImportPluginLog, Warning, TEXT("Unable to parse ressource url : %s"), *Value);
			}
		}
	}

	return MaterialInstanceConstant;
}

