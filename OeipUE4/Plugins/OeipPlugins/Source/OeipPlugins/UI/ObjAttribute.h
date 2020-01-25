// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "CoreMinimal.h"
#include "BaseAttribute.h"
#include "UMG.h"
#include "BaseAutoWidget.h"

using namespace std;
using namespace std::placeholders;

template<class U>
class ObjAttribute
{
private:
	//�󶨵Ķ���
	U * obj = nullptr;
	//�󶨶����Ԫ�ṹ
	UStruct* structDefinition = nullptr;
	//����UI��box
	UVerticalBox* panel = nullptr;
	//�󶨶���Ԫ��������
	TArray<BaseAttribute*> attributeList;
	//����Ԫ�����������ɵ�UI
	TArray<UBaseAutoWidget*> widgetList;
	//�Ƿ��
	bool bBind = false;
	//���󶨶���ı�������Ļص�
	std::function<void(ObjAttribute<U>*, FString name)> onObjChangeHandle = nullptr;
public:
	//��һ������UVerticalBox�ϣ�����arrtList������Ԫ�����Լ���ӦUIģ������UI��box��,��ӦUI�ı䶯���Զ����µ������ڴ�������
	void Bind(U* pobj, UVerticalBox* box, TArray<BaseAttribute*> arrtList, TArray<UClass*> templateWidgetList, UWorld* world)
	{
		if (bBind)
			return;
		obj = pobj;
		structDefinition = U::StaticStruct();
		attributeList = arrtList;
		panel = box;
		for (auto attribute : attributeList)
		{
			int index = attribute->GetIndex();
			//TSubclassOf<UToggleAutoWidget> togglewidget = LoadClass<UToggleAutoWidget>(nullptr, TEXT("WidgetBlueprint'/Game/UI/togglePanel.togglePanel_C'"));
			auto templateWidget = templateWidgetList[index];
			if (!templateWidget)
				continue;
			if (index == 0)
				InitWidget<UToggleAutoWidget>(attribute, templateWidget, world);
			else if (index == 1)
				InitWidget<UInputWidget>(attribute, templateWidget, world);
			else if (index == 2)
				InitWidget<USliderInputWidget>(attribute, templateWidget, world);
			else if (index == 3)
				InitWidget<UDropdownWidget>(attribute, templateWidget, world);
		}
		for (auto widget : widgetList)
		{
			panel->AddChild(widget);
		}
		UpdateDropdownParent();
		bBind = true;
	};

	//1 probj = null��ֱ�Ӹ����ڴ����ݣ�Ȼ������UI
	//2 probj != null�󶨵�ͬ���ͱ�Ķ���,����������UI,���Һ���UI���·��������¶���
	void Update(U* pobj = nullptr)
	{
		if (!bBind)
			return;
		if (pobj)
			obj = pobj;
		for (auto widget : widgetList)
		{
			int index = widget->index;			
			if (index == 0)
				GetValue<UToggleAutoWidget>(widget);
			else if (index == 1)
				GetValue<UInputWidget>(widget);
			else if (index == 2)
				GetValue<USliderInputWidget>(widget);
			else if (index == 3)
				GetValue<UDropdownWidget>(widget);
		}
	}

	//����Ӧ�ṹ��UI�Ķ����¶���󣬷��صĻص�����һ��������ʾ��ǰ���󣬵ڶ���������ʾ��Ӧ�ֶ���
	void SetOnObjChangeAction(std::function<void(ObjAttribute<U>*, FString name)> onChange)
	{
		onObjChangeHandle = onChange;
	}

	//���ص�ǰBind�Ķ���
	U* GetObj()
	{
		return obj;
	}

	//�����ֶ����õ���ӦUToggleAutoWidget/UInputWidget/USliderInputWidget/UDropdownWidget
	template<typename A>
	A* GetWidget(FString name)
	{
		for (auto widget : widgetList)
		{
			A* awidget = dynamic_cast<A*>(widget);
			if (awidget && awidget->memberName == name)
			{
				return awidget;
			}
		}
		return nullptr;
	}

private:
	template<typename A>
	void InitWidget(BaseAttribute* attribute, UClass* widgetTemplate, UWorld* world)
	{
		auto widget = CreateWidget<A>(world, widgetTemplate);
		widget->onValueChange = std::bind(&ObjAttribute<U>::SetValue<A::Type>, this, _1, _2);
		widget->attribute = attribute;
		widget->uproperty = FindProperty(attribute->MemberName);
		widget->index = attribute->GetIndex();
		//���ö�Ӧ��ͼ��UI��ֵ
		widget->InitWidget();
		//����UI���¼������ϵ�onValueChange��
		widget->InitEvent();
		widget->memberName = attribute->MemberName;
		widget->text->SetText(FText::FromString(attribute->DisplayName));
		widgetList.Add(widget);
	}

	//�ڶ�Ӧ��Widget��ֱ�ӱ����UProperty���󣬴˺��������/UI����
	UProperty* FindProperty(FString name)
	{
		for (TFieldIterator<UProperty> It(structDefinition); It; ++It)
		{
			UProperty* Property = *It;
			if (Property->GetName() == name)
			{
				return Property;
			}
		}
		return nullptr;
	}

	//����Ӧ��UI�Ķ���UIӰ���Ӧobj��ֵ������t��ʾ��ӦUI���ص�����
	//ComponentTemplate��Ӧ�ķ���t�ǹ̶��ģ��������ݽṹ����ֶ����Ϳɶ��֣�ת���߼�������д�þ���
	template<typename T>
	void SetValue(ComponentTemplate<T>* widget, T t)
	{
		if (widget->uproperty != nullptr)
		{
			ValueToUProperty(widget->uproperty, t);
			if (onObjChangeHandle)
			{
				onObjChangeHandle(this, widget->uproperty->GetName());
			}
		}
	};

	void ValueToUProperty(UProperty* Property, bool t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UBoolProperty *BoolProperty = Cast<UBoolProperty>(Property))
		{
			BoolProperty->SetPropertyValue(Value, t);
		}
	};

	void ValueToUProperty(UProperty* Property, float t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UNumericProperty *NumericProperty = Cast<UNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				NumericProperty->SetFloatingPointPropertyValue(Value, (float)t);
			}
			else if (NumericProperty->IsInteger())
			{
				NumericProperty->SetIntPropertyValue(Value, (int64)t);
			}
		}
	};

	void ValueToUProperty(UProperty* Property, FString t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UStrProperty *StringProperty = Cast<UStrProperty>(Property))
		{
			StringProperty->SetPropertyValue(Value, t);
		}
	}

	void ValueToUProperty(UProperty* Property, int t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UNumericProperty *NumericProperty = Cast<UNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				NumericProperty->SetFloatingPointPropertyValue(Value, (int64)t);
			}
			else if (NumericProperty->IsInteger())
			{
				NumericProperty->SetIntPropertyValue(Value, (int64)t);
			}
		}
		else if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
		{
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Value, (int64)t);
		}
	}

	//�Ӷ�Ӧ��obj��ȥȡֵ����UI����ת��ComponentTemplate::Update
	//ͬSetValue��ComponentTemplate���͹̶������ݽṹ���Ϳɶ��֣�������Ҫд��Ӧ��ת���߼�
	template<typename A>//template<typename T, typename A>
	void GetValue(UBaseAutoWidget* baseWidget)//ComponentTemplate<T>* widget, T* t)
	{
		A* widget = (A*)baseWidget;
		if (widget->uproperty != nullptr)
		{
			A::Type t;
			if (UPropertyToValue(widget->uproperty, t))
				widget->Update(t);
		}
		//A* widget = (A*)baseWidget;
		//for (TFieldIterator<UProperty> It(structDefinition); It; ++It)
		//{
		//	UProperty* Property = *It;
		//	FString PropertyName = Property->GetName();
		//	if (PropertyName == widget->attribute->MemberName)
		//	{
		//		A::Type t;
		//		if (UPropertyToValue(Property, t))
		//			widget->Update(t);
		//	}
		//}
	};

	bool UPropertyToValue(UProperty* Property, bool& t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UBoolProperty *BoolProperty = Cast<UBoolProperty>(Property))
		{
			bool value = BoolProperty->GetPropertyValue(Value);
			t = value;
			return true;
		}
		return false;
	};

	bool UPropertyToValue(UProperty* Property, float& t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UNumericProperty *NumericProperty = Cast<UNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				float value = NumericProperty->GetFloatingPointPropertyValue(Value);
				t = value;
				return true;
			}
			else if (NumericProperty->IsInteger())
			{
				int value = NumericProperty->GetSignedIntPropertyValue(Value);
				t = (float)value;
				return true;
			}
		}
		return false;
	};

	bool UPropertyToValue(UProperty* Property, FString& t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UStrProperty *StringProperty = Cast<UStrProperty>(Property))
		{
			FString value = StringProperty->GetPropertyValue(Value);
			t = value;
			return true;
		}
		return false;
	}

	bool UPropertyToValue(UProperty* Property, int& t)
	{
		void* Value = Property->ContainerPtrToValuePtr<uint8>(obj);
		if (UNumericProperty *NumericProperty = Cast<UNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				float value = NumericProperty->GetFloatingPointPropertyValue(Value);
				t = (int)value;
				return true;
			}
			else if (NumericProperty->IsInteger())
			{
				int value = NumericProperty->GetSignedIntPropertyValue(Value);
				t = value;
				return true;
			}
		}
		else if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
		{
			UEnum* EnumDef = EnumProperty->GetEnum();
			t = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
			return true;
		}
		return false;
	};

	void UpdateDropdownParent()
	{
		for (auto widget : widgetList)
		{
			panel->AddChild(widget);
			if (widget->index == 3)
			{
				UDropdownWidget* dropWidget = (UDropdownWidget*)widget;
				DropdownAttribute* dropAttribut = (DropdownAttribute*)(dropWidget->attribute);
				if (dropAttribut->Parent.IsEmpty())
					continue;
				for (auto widget : widgetList)
				{
					if (widget->index == 3 && widget->memberName == dropAttribut->Parent)
					{
						dropWidget->parent = (UDropdownWidget*)widget;
						dropWidget->InitParentEvent();
					}
				}
			}
		}
	}
};
