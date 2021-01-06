#include "T3DMaterialParser.h"

#include "UDKImportPluginPrivatePCH.h"
#include "T3DLevelParser.h"

#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialFunction.h"

T3DMaterialParser::T3DMaterialParser(T3DLevelParser * ParentParser, const FString &Package) : T3DParser(ParentParser->UdkPath, ParentParser->TmpPath)
{
	this->LevelParser = ParentParser;
	this->Package = Package;
	this->Material = NULL;
}

UMaterial* T3DMaterialParser::ImportMaterialT3DFile(const FString &FileName)
{
	FString MaterialT3D;
	if (FFileHelper::LoadFileToString(MaterialT3D, *FileName))
	{
		ResetParser(MaterialT3D);
		MaterialT3D.Empty();
		return ImportMaterial();
	}

	return NULL;
}

static UMaterialExpression* NewMaterialExpression(UObject* Parent, UClass* Class)
{
	check(Class->IsChildOf(UMaterialExpression::StaticClass()));

	UMaterialExpression* Expression = NewObject<UMaterialExpression>(Parent, Class);
	Expression->MaterialExpressionGuid = FGuid::NewGuid();

	return Expression;
}

template<typename T>
T* NewMaterialExpression(UObject* Parent)
{
	return Cast<T>(NewMaterialExpression(Parent, T::StaticClass()));
}

// Map of classes simply renamed from UDK -> UE4.
static const TMap<FString, FString> MaterialExpressionTranslation = {
	{"MaterialExpressionFlipBookSample", "MaterialExpressionTextureSample"},
	{"MaterialExpressionConstantClamp", "MaterialExpressionClamp"},
	{"MaterialExpressionCameraVector", "MaterialExpressionCameraVectorWS"},
	{"MaterialExpressionReflectionVector", "MaterialExpressionReflectionVectorWS"},
	{"MaterialExpressionWorldNormal", "MaterialExpressionVertexNormalWS"},
	{"MaterialExpressionObjectWorldPosition", "MaterialExpressionObjectPositionWS"},
};

UMaterial*  T3DMaterialParser::ImportMaterial()
{
	FString ClassName, MaterialName, Name, Value;
	UClass * Class;

	ensure(NextLine());
	ensure(IsBeginObject(ClassName));
	ensure(ClassName == TEXT("Material"));
	ensure(GetOneValueAfter(TEXT(" Name="), MaterialName));

	FString BasePackageName = FString::Printf(TEXT("/Game/UDK/%s/Materials"), *Package);
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>(UMaterialFactoryNew::StaticClass());
	Material = (UMaterial*)AssetToolsModule.Get().CreateAsset(MaterialName, BasePackageName, UMaterial::StaticClass(), MaterialFactory);
	if (Material == NULL)
	{
		return NULL;
	}

	Material->Modify();
	while (NextLine() && !IsEndObject())
	{
		if (IsBeginObject(ClassName))
		{
			const FString* TranslatedClassName = MaterialExpressionTranslation.Find(ClassName);
			if (TranslatedClassName)
				ClassName = *TranslatedClassName;

			Class = (UClass*)StaticFindObject(UClass::StaticClass(), ANY_PACKAGE, *ClassName, true);
			if (Class)
			{
				ensure(GetOneValueAfter(TEXT(" Name="), Name));

				FRequirement TextureRequirement;
				UMaterialExpression* MaterialExpression = ImportMaterialExpression(Class, TextureRequirement);
				UMaterialExpressionComment * MaterialExpressionComment = Cast<UMaterialExpressionComment>(MaterialExpression);
				if (MaterialExpressionComment)
				{
					Material->EditorComments.Add(MaterialExpressionComment);
					MaterialExpressionComment->MaterialExpressionEditorX -= MaterialExpressionComment->SizeX;
					FixRequirement(FString::Printf(TEXT("%s'%s'"), *ClassName, *Name), MaterialExpression);
				}
				else if (MaterialExpression)
				{
					Material->Expressions.Add(MaterialExpression);
					FixRequirement(FString::Printf(TEXT("%s'%s'"), *ClassName, *Name), MaterialExpression);
				}

				if (ClassName == TEXT("MaterialExpressionFlipBookSample"))
				{
					ImportMaterialExpressionFlipBookSample((UMaterialExpressionTextureSample *)MaterialExpression, TextureRequirement);
				}
			}
			else
			{
				UE_LOG(UDKImportPluginLog, Error, TEXT("Material %s: Unknown material class: %s"), *MaterialName, *ClassName);
				JumpToEnd();
			}
		}
		else if (GetProperty(TEXT("DiffuseColor="), Value))
		{
			ImportExpression(&Material->BaseColor);
		}
		else if (GetProperty(TEXT("SpecularColor="), Value))
		{
			ImportExpression(&Material->Specular);
		}
		else if (GetProperty(TEXT("SpecularPower="), Value))
		{
			// TODO
		}
		else if (GetProperty(TEXT("Normal="), Value))
		{
			ImportExpression(&Material->Normal);
		}
		else if (GetProperty(TEXT("EmissiveColor="), Value))
		{
			ImportExpression(&Material->EmissiveColor);
		}
		else if (GetProperty(TEXT("Opacity="), Value))
		{
			ImportExpression(&Material->Opacity);
		}
		else if (GetProperty(TEXT("OpacityMask="), Value))
		{
			ImportExpression(&Material->OpacityMask);
		}
		else if (GetProperty(TEXT("PreviewMesh="), Value))
		{
			// TODO: Add requirement.
		}
		else if (IsProperty(Name, Value))
		{
			FProperty* Property = FindFProperty<FProperty>(UMaterial::StaticClass(), *Name);
			if (Property)
				Property->ImportText(*Value, Property->ContainerPtrToValuePtr<uint8>(Material), 0, Material);
			else
				UE_LOG(UDKImportPluginLog, Error, TEXT("Material %s: Unknown property %s"), *MaterialName, *Name);
		}
	}

	PrintMissingRequirements();

	return Material;
}

UMaterialExpression* T3DMaterialParser::ImportMaterialExpression(UClass * Class, FRequirement &TextureRequirement)
{
	if (!Class->IsChildOf(UMaterialExpression::StaticClass()))
		return NULL;

	UMaterialExpression* MaterialExpression = NewMaterialExpression(Material, Class);
	MaterialExpression->Material = Material;

	FString Value, Name, PropertyName, Type, PackageName;
	while (NextLine() && IgnoreSubs() && !IsEndObject())
	{
		if (GetProperty(TEXT("Texture="), Value))
		{
			auto MaterialExpressionTexture = Cast<UMaterialExpressionTextureBase>(MaterialExpression);

			if (MaterialExpressionTexture)
			{
				if (ParseResourceUrl(Value, TextureRequirement))
				{
					LevelParser->AddRequirement(TextureRequirement, UObjectDelegate::CreateRaw(LevelParser, &T3DLevelParser::SetTexture, (UMaterialExpressionTextureBase*)MaterialExpression));
				}
				else
				{
					UE_LOG(UDKImportPluginLog, Warning, TEXT("Unable to parse resource url : %s"), *Value);
				}
			}
		}
		else if (GetProperty(TEXT("Name="), Value))
		{
			// Silently ignore. Expressions are not named nowadays (it seems).
			continue;
		}
		else if (IsProperty(PropertyName, Value) 
			&& PropertyName != TEXT("Material")
			&& PropertyName != TEXT("ExpressionGUID")
			&& PropertyName != TEXT("ObjectArchetype"))
		{
			if (Class->GetName() == TEXT("MaterialExpressionDesaturation") && PropertyName == TEXT("Percent"))
			{
				PropertyName = TEXT("Fraction");
			}
			else if (Class == UMaterialExpressionConstant4Vector::StaticClass())
			{
				if (PropertyName == TEXT("A"))
					((UMaterialExpressionConstant4Vector*)MaterialExpression)->Constant.A = FCString::Atof(*Value);
				else if (PropertyName == TEXT("B"))
					((UMaterialExpressionConstant4Vector*)MaterialExpression)->Constant.B = FCString::Atof(*Value);
				else if (PropertyName == TEXT("G"))
					((UMaterialExpressionConstant4Vector*)MaterialExpression)->Constant.G = FCString::Atof(*Value);
				else if (PropertyName == TEXT("R"))
					((UMaterialExpressionConstant4Vector*)MaterialExpression)->Constant.R = FCString::Atof(*Value);
			}
			else if (Class == UMaterialExpressionConstant3Vector::StaticClass())
			{
				if (PropertyName == TEXT("B"))
					((UMaterialExpressionConstant3Vector*)MaterialExpression)->Constant.B = FCString::Atof(*Value);
				else if (PropertyName == TEXT("G"))
					((UMaterialExpressionConstant3Vector*)MaterialExpression)->Constant.G = FCString::Atof(*Value);
				else if (PropertyName == TEXT("R"))
					((UMaterialExpressionConstant3Vector*)MaterialExpression)->Constant.R = FCString::Atof(*Value);
			}

			FProperty* Property = FindFProperty<FProperty>(Class, *PropertyName);
			FStructProperty * StructProperty = CastField<FStructProperty>(Property);
			if (StructProperty && StructProperty->Struct->GetName() == TEXT("ExpressionInput"))
			{
				FExpressionInput * ExpressionInput = Property->ContainerPtrToValuePtr<FExpressionInput>(MaterialExpression);
				ImportExpression(ExpressionInput);
			}
			else if (Property)
			{
				Property->ImportText(*Value, Property->ContainerPtrToValuePtr<uint8>(MaterialExpression), 0, MaterialExpression);
			}
			else
			{
				UE_LOG(UDKImportPluginLog, Warning, TEXT("Material %s: %s does not have property %s (= %s)"), *Material->GetName(), *Class->GetName(), *PropertyName, *Value);
			}

			/*
			FPropertyChangedEvent PropertyChanged(Property);
			MaterialExpression->Modify();
			MaterialExpression->PostEditChangeProperty(PropertyChanged);
			*/
		}
	}

	MaterialExpression->MaterialExpressionEditorX = -MaterialExpression->MaterialExpressionEditorX;
	return MaterialExpression;
}

void T3DMaterialParser::ImportMaterialExpressionFlipBookSample(UMaterialExpressionTextureSample * Expression, FRequirement &TextureRequirement)
{
	UMaterialExpressionMaterialFunctionCall * MEFunction = NewMaterialExpression<UMaterialExpressionMaterialFunctionCall>(Material);
	UMaterialExpressionConstant * MECRows = NewMaterialExpression<UMaterialExpressionConstant>(Material);
	UMaterialExpressionConstant * MECCols = NewMaterialExpression<UMaterialExpressionConstant>(Material);
	MEFunction->MaterialExpressionEditorY = Expression->MaterialExpressionEditorY;
	MECRows->MaterialExpressionEditorY = MEFunction->MaterialExpressionEditorY;
	MECCols->MaterialExpressionEditorY = MECRows->MaterialExpressionEditorY + 64;

	MEFunction->MaterialExpressionEditorX = Expression->MaterialExpressionEditorX - 304;
	MECRows->MaterialExpressionEditorX = MEFunction->MaterialExpressionEditorX - 80;
	MECCols->MaterialExpressionEditorX = MECRows->MaterialExpressionEditorX;

	MECRows->bCollapsed = true;
	MECCols->bCollapsed = true;

	MEFunction->SetMaterialFunction(LoadObject<UMaterialFunction>(NULL, TEXT("/Engine/Functions/Engine_MaterialFunctions02/Texturing/FlipBook.FlipBook")));

	MEFunction->FunctionInputs[1].Input.Expression = MECRows;
	MEFunction->FunctionInputs[2].Input.Expression = MECCols;

	if (Expression->Coordinates.Expression)
	{
		MEFunction->FunctionInputs[4].Input.Expression = Expression->Coordinates.Expression;
	}

	FString ExportFolder;
	FString FileName = TextureRequirement.Name + TEXT(".T3D");
	LevelParser->ExportPackage(TextureRequirement.Package, T3DLevelParser::EExportType::Texture2DInfo, ExportFolder);
	if (FFileHelper::LoadFileToString(Line, *(ExportFolder / FileName)))
	{
		FString Value;
		if (GetOneValueAfter(TEXT("HorizontalImages="), Value))
		{
			MECCols->R = FCString::Atof(*Value);
		}
		if (GetOneValueAfter(TEXT("VerticalImages="), Value))
		{
			MECRows->R = FCString::Atof(*Value);
		}
	}

	Expression->Coordinates.OutputIndex = 2;
	Expression->Coordinates.Expression = MEFunction;

	Material->Expressions.Add(MECRows);
	Material->Expressions.Add(MECCols);
	Material->Expressions.Add(MEFunction);
}

void T3DMaterialParser::ImportExpression(FExpressionInput * ExpressionInput)
{
	FString Value;
	if (GetOneValueAfter(TEXT("Expression="), Value))
		AddRequirement(Value, UObjectDelegate::CreateRaw(this, &T3DMaterialParser::SetExpression, ExpressionInput));
	if (GetOneValueAfter(TEXT("Mask="), Value))
		ExpressionInput->Mask = FCString::Atoi(*Value);
	if (GetOneValueAfter(TEXT("MaskR="), Value))
		ExpressionInput->MaskR = FCString::Atoi(*Value);
	if (GetOneValueAfter(TEXT("MaskG="), Value))
		ExpressionInput->MaskG = FCString::Atoi(*Value);
	if (GetOneValueAfter(TEXT("MaskB="), Value))
		ExpressionInput->MaskB = FCString::Atoi(*Value);
	if (GetOneValueAfter(TEXT("MaskA="), Value))
		ExpressionInput->MaskA = FCString::Atoi(*Value);
}

void T3DMaterialParser::SetExpression(UObject * Object, FExpressionInput * ExpressionInput)
{
	UMaterialExpression * Expression = Cast<UMaterialExpression>(Object);
	ExpressionInput->Expression = Expression;
}
