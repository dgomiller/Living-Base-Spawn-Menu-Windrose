#include <InGamePanel.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Unreal/FField.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

// Phase 0 widget-construction spike -- CONFIRMED WORKING LIVE (2026-08-22): toggles cleanly and
// repeatedly, on foot and on ship, survives a world reload. Same primitives HUDControl's Lua
// BuildPanel() uses (Other\HUDControl-...\Scripts\main.lua) -- StaticFindObject to resolve each
// UMG class by its /Script/UMG.<ClassName> path, NewObject to construct instances,
// GetPropertyByNameInChain for the two structural object-reference properties (WidgetTree,
// RootWidget) that only exist as properties, not functions, and ProcessEvent for everything else
// (AddChildToCanvas, SetContent, SetText, AddToViewport, RemoveFromParent). Every ProcessEvent
// params struct below mirrors a REAL, well-known stock UMG function signature (never an
// invented/guessed one) -- see this project's own HANDOFF_INGAME_PANEL.md for why that
// distinction matters here (a wrong params struct layout is a real crash risk, not just a wrong
// result).
//
// Deliberately minimal for this first spike: one Border with one TextBlock marker, default
// (unstyled, unpositioned) canvas slot placement -- looks "wonky" (overlaps the native HUD,
// top-left corner) on purpose. Slot anchoring/positioning and real styling are Phase 4 concerns
// once click-handling (Phase 1) and the real content (Phase 2/3) are proven out on top of this
// shell -- see HANDOFF_INGAME_PANEL.md.

namespace RC::LivingBaseSpawnMenu::InGamePanel
{
    using namespace RC::Unreal;

    namespace
    {
        UObject* g_root_widget{};
        bool g_visible{};

        auto FindUMGClass(const File::CharType* short_name) -> UClass*
        {
            File::StringType path{STR("/Script/UMG.")};
            path += short_name;
            return UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
        }

        // WidgetTree/RootWidget only ever exist as UPROPERTYs (no setter UFUNCTION).
        // FObjectPropertyBase::SetObjectPropertyValue was tried first and CONFIRMED LIVE CRASHING
        // (2026-08-22, no catchable error): it resolves through UE4SS's own vtable-offset lookup
        // (FObjectPropertyBase::VTableLayoutMap, populated from a version-specific dump at UE4SS
        // startup), and that entry is apparently wrong/unresolved for this exact game build -- a
        // bad function pointer call. ContainerPtrToValuePtr (a plain offset computation, no
        // virtual dispatch) was separately confirmed safe by the same live test that caught this.
        // Since FObjectProperty's underlying storage IS just a flat `UObject*`
        // (TFObjectPropertyBase<UObject*>, no ref-counting/smart-pointer machinery for a plain
        // object property), writing it directly at the confirmed-correct address sidesteps the
        // bad vtable call entirely instead of trusting that unverified "official" API. If this
        // ever needs extending to a non-plain-UObject* property type, re-verify the same way
        // (isolated live test) before trusting SetObjectPropertyValue again on this build.
        auto SetObjectProperty(UObject* target, const File::CharType* prop_name, UObject* value) -> bool
        {
            FProperty* prop = target->GetPropertyByNameInChain(prop_name);
            if (!prop)
            {
                return false;
            }
            auto* object_prop = CastField<FObjectProperty>(prop);
            if (!object_prop)
            {
                return false;
            }
            void* address = object_prop->ContainerPtrToValuePtr<void>(target);
            *static_cast<UObject**>(address) = value;
            return true;
        }

        // Zero-arg, zero-return UFUNCTION call (AddToViewport with ZOrder omitted would also
        // qualify, but it takes one int32 -- this helper is for the truly parameterless ones:
        // RemoveFromParent).
        auto CallVoidFunction(UObject* target, const File::CharType* function_name) -> bool
        {
            UFunction* function = target->GetFunctionByNameInChain(function_name);
            if (!function)
            {
                return false;
            }
            target->ProcessEvent(function, nullptr);
            return true;
        }

        auto EnsureBuilt() -> bool
        {
            if (g_root_widget && UObject::IsReal(g_root_widget))
            {
                return true;
            }
            g_root_widget = nullptr;

            UObject* game_instance = UObjectGlobals::FindFirstOf(STR("GameInstance"));
            if (!game_instance)
            {
                Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] InGamePanel: no GameInstance yet, deferring build\n"));
                return false;
            }

            UClass* user_widget_class = FindUMGClass(STR("UserWidget"));
            UClass* widget_tree_class = FindUMGClass(STR("WidgetTree"));
            UClass* canvas_panel_class = FindUMGClass(STR("CanvasPanel"));
            UClass* border_class = FindUMGClass(STR("Border"));
            UClass* text_block_class = FindUMGClass(STR("TextBlock"));
            if (!user_widget_class || !widget_tree_class || !canvas_panel_class || !border_class || !text_block_class)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: one or more /Script/UMG classes not found\n"));
                return false;
            }

            UObject* root = UObjectGlobals::NewObject<UObject>(game_instance, user_widget_class, FName(STR("LivingBaseInGamePanelRoot")));
            UObject* widget_tree = root ? UObjectGlobals::NewObject<UObject>(root, widget_tree_class, FName(STR("LivingBaseInGamePanelTree"))) : nullptr;
            UObject* canvas = widget_tree ? UObjectGlobals::NewObject<UObject>(widget_tree, canvas_panel_class, FName(STR("LivingBaseInGamePanelCanvas"))) : nullptr;
            UObject* border = canvas ? UObjectGlobals::NewObject<UObject>(canvas, border_class, FName(STR("LivingBaseInGamePanelBorder"))) : nullptr;
            UObject* text_block = border ? UObjectGlobals::NewObject<UObject>(border, text_block_class, FName(STR("LivingBaseInGamePanelText"))) : nullptr;
            if (!root || !widget_tree || !canvas || !border || !text_block)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: widget construction failed partway through\n"));
                return false;
            }

            if (!SetObjectProperty(root, STR("WidgetTree"), widget_tree) || !SetObjectProperty(widget_tree, STR("RootWidget"), canvas))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: WidgetTree/RootWidget property write failed\n"));
                return false;
            }

            // UCanvasPanel::AddChildToCanvas(UWidget* Content) -> UCanvasPanelSlot* -- return
            // value unused here (Phase 0 accepts default slot placement, see file header).
            {
                UFunction* add_child_fn = canvas->GetFunctionByNameInChain(STR("AddChildToCanvas"));
                if (!add_child_fn)
                {
                    Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: AddChildToCanvas UFunction not found\n"));
                    return false;
                }
                struct AddChildToCanvas_Params
                {
                    UObject* Content;
                    UObject* ReturnValue;
                };
                AddChildToCanvas_Params params{border, nullptr};
                canvas->ProcessEvent(add_child_fn, &params);
            }

            // UContentWidget::SetContent(UWidget* InContent) -- void return.
            {
                UFunction* set_content_fn = border->GetFunctionByNameInChain(STR("SetContent"));
                if (!set_content_fn)
                {
                    Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: SetContent UFunction not found\n"));
                    return false;
                }
                struct SetContent_Params
                {
                    UObject* Content;
                };
                SetContent_Params params{text_block};
                border->ProcessEvent(set_content_fn, &params);
            }

            // UTextBlock::SetText(FText InText) -- void return. FText's constructor here goes
            // through the same proven UKismetTextLibrary::Conv_StringToText call this SDK already
            // uses elsewhere (see FText.hpp), not a hand-built struct.
            {
                UFunction* set_text_fn = text_block->GetFunctionByNameInChain(STR("SetText"));
                if (!set_text_fn)
                {
                    Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: SetText UFunction not found\n"));
                    return false;
                }
                struct SetText_Params
                {
                    FText Text;
                };
                SetText_Params params{FText(STR("LivingBase In-Game Panel (Phase 0)"))};
                text_block->ProcessEvent(set_text_fn, &params);
            }

            g_root_widget = root;
            Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel: built OK\n"));
            return true;
        }
    } // namespace

    auto Start() -> void
    {
        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel::Start (lazy build on first toggle)\n"));
    }

    auto Toggle() -> void
    {
        if (g_visible && g_root_widget && UObject::IsReal(g_root_widget))
        {
            if (CallVoidFunction(g_root_widget, STR("RemoveFromParent")))
            {
                g_visible = false;
                Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel: hidden\n"));
            }
            return;
        }

        if (!EnsureBuilt())
        {
            return;
        }

        // UUserWidget::AddToViewport(int32 ZOrder = 0) -- void return.
        UFunction* add_to_viewport_fn = g_root_widget->GetFunctionByNameInChain(STR("AddToViewport"));
        if (!add_to_viewport_fn)
        {
            Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: AddToViewport UFunction not found\n"));
            return;
        }
        struct AddToViewport_Params
        {
            int32_t ZOrder;
        };
        AddToViewport_Params params{1000};
        g_root_widget->ProcessEvent(add_to_viewport_fn, &params);
        g_visible = true;
        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel: shown\n"));
    }
} // namespace RC::LivingBaseSpawnMenu::InGamePanel
