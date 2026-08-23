#include <InGamePanel.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <Unreal/FField.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

// Phase 0 (widget construction) and Phase 1 (click detection) -- BOTH CONFIRMED WORKING LIVE
// (2026-08-22/23). Phase 0: toggles cleanly and repeatedly, on foot and on ship, survives a world
// reload. Phase 1: a real Button's OnClicked fires exactly once per click, confirmed under 100+
// rapid clicks across two separate live sessions with zero double-fires/misses, no click leaking
// through to the game underneath, no crash from repeated clicking.
//
// KNOWN INTERMITTENT ISSUE, not fully root-caused: in some game sessions,
// GetFunctionByNameInChain(STR("AddToViewport")) (and other UUserWidget-inherited functions)
// fails to resolve on an otherwise perfectly valid, freshly-constructed root widget -- confirmed
// NOT caused by WireButtonClick specifically (reproduced with that call fully disabled), and NOT
// simply "the class hasn't finished loading yet" (stays broken for 20+ seconds/many retries within
// an affected session, then a DIFFERENT session works cleanly with the exact same code, unrelated
// to whether extra diagnostic logging was present). A working session's user_widget_class reports
// 176 functions in its chain (a normal, fully-populated table), so it isn't a bare/empty class
// stub either when it fails. EnsureBuilt() now defends against this rather than solving it:
// every call verifies AddToViewport is actually resolvable, not just that the cached root is
// still a live UObject, and rebuilds from scratch if not -- turns "permanently stuck broken for
// the rest of the session" into "self-heals on the next press" in practice. Revisit if this
// recurs with the function-count diagnostic still in place below (compare an affected session's
// count against the known-good 176).
//
// Same primitives HUDControl's Lua BuildPanel() uses (Other\HUDControl-...\Scripts\main.lua) --
// StaticFindObject to resolve each UMG class by its /Script/UMG.<ClassName> path, NewObject to
// construct instances, GetPropertyByNameInChain for structural object-reference properties that
// only exist as properties, not functions (WidgetTree, RootWidget, OnClicked), and ProcessEvent
// for everything else (AddChildToCanvas, SetContent, SetText, AddToViewport, RemoveFromParent).
// Every ProcessEvent params struct below mirrors a REAL, well-known stock UMG function signature
// (never an invented/guessed one) -- see this project's own HANDOFF_INGAME_PANEL.md for why that
// distinction matters here (a wrong params struct layout is a real crash risk, not just a wrong
// result).
//
// Deliberately minimal so far: one Border with one TextBlock marker, one Button with its own
// TextBlock label, default (unstyled, unpositioned) canvas slot placement -- looks "wonky"
// (overlaps the native HUD, top-left corner) on purpose. Slot anchoring/positioning and real
// styling are Phase 4 concerns once the real content (Phase 2/3) is proven out on top of this
// shell -- see HANDOFF_INGAME_PANEL.md. Also noted during Phase 1 live testing: unlike
// StandaloneWindow (a separate OS window, mouse released via normal window focus), this in-viewport
// panel needed the game's own menu to be open to release the mouse cursor for clicking -- Phase 4
// needs its own mouse-release mechanism (RedFalcon's own framing: "the equivalent of = now").

namespace RC::LivingBaseSpawnMenu::InGamePanel
{
    using namespace RC::Unreal;

    namespace
    {
        UObject* g_root_widget{};
        UObject* g_marker_text{};
        bool g_visible{};
        int g_click_count{};

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

        // UTextBlock::SetText(FText InText) -- void return. FText's constructor here goes through
        // the same proven UKismetTextLibrary::Conv_StringToText call this SDK already uses
        // elsewhere (see FText.hpp), not a hand-built struct.
        auto SetTextBlockText(UObject* text_block, const File::StringType& text) -> bool
        {
            UFunction* set_text_fn = text_block->GetFunctionByNameInChain(STR("SetText"));
            if (!set_text_fn)
            {
                return false;
            }
            struct SetText_Params
            {
                FText Text;
            };
            SetText_Params params{FText(text.c_str())};
            text_block->ProcessEvent(set_text_fn, &params);
            return true;
        }

        // Phase 1 click-detection spike (see HANDOFF_INGAME_PANEL.md's "Technical grounding"
        // section for the full reasoning): bind the button's OnClicked delegate to a UFunction it
        // already inherits and is harmless to actually invoke (UWidget::ForceLayoutPrepass --
        // forces a Slate layout recalculation, something the widget system already does
        // routinely, chosen over an invented function), then register a native post-hook scoped
        // to this one button instance on that same UFunction. A real click routes through Slate's
        // normal input handling -> broadcasts OnClicked -> calls the bound function on this
        // instance -> our hook fires.
        //
        // Deliberately does NOT go through FMulticastDelegateProperty::AddDelegate -- that's a
        // "Virtual Function" per its own header comment, backed by the SAME
        // VTableLayoutMap-resolved-vtable-call mechanism that already crashed once in Phase 0
        // (FObjectPropertyBase::SetObjectPropertyValue, see SetObjectProperty's own comment
        // above). TScriptDelegate::BindUFunction and TArray::Add, by contrast, are plain
        // non-virtual template methods (confirmed by reading their own definitions in
        // UnrealType.hpp/Array.hpp) -- no vtable dependency, so writing the delegate's invocation
        // list directly at its confirmed-correct property address sidesteps that whole risk class
        // again, same as the WidgetTree/RootWidget fix.
        auto WireButtonClick(UObject* button, const File::CharType* bound_function_name) -> bool
        {
            auto* onclicked_prop = CastField<FMulticastInlineDelegateProperty>(button->GetPropertyByNameInChain(STR("OnClicked")));
            if (!onclicked_prop)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: OnClicked property not found or not FMulticastInlineDelegateProperty\n"));
                return false;
            }

            UFunction* bound_function = button->GetFunctionByNameInChain(bound_function_name);
            if (!bound_function)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: bound function not found\n"));
                return false;
            }

            // Sanity-check the property's REPORTED size against our assumed C++ struct layout
            // before writing anything -- a mismatch here means FMulticastScriptDelegate's layout
            // in this SDK doesn't match what this specific build's OnClicked property actually
            // stores, and writing through a wrong-sized reinterpret would corrupt adjacent memory
            // (heap or otherwise) rather than just fail cleanly. Bail instead of guessing.
            const int32_t reported_size = onclicked_prop->GetSize();
            if (reported_size != static_cast<int32_t>(sizeof(FMulticastScriptDelegate)))
            {
                Output::send<LogLevel::Error>(
                        STR("[LivingBaseSpawnMenu] InGamePanel: OnClicked property size mismatch (reported {}, expected {}) -- refusing to write\n"),
                        reported_size,
                        static_cast<int32_t>(sizeof(FMulticastScriptDelegate)));
                return false;
            }

            void* address = onclicked_prop->ContainerPtrToValuePtr<void>(button);
            auto* delegate = static_cast<FMulticastScriptDelegate*>(address);
            FScriptDelegate entry;
            entry.BindUFunction(button, FName(bound_function_name));
            delegate->InvocationList.Add(entry);

            bound_function->RegisterPostHookForInstance(
                    [](UnrealScriptFunctionCallableContext&, void*) {
                        ++g_click_count;
                        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel: BUTTON CLICKED (fire #{})\n"), g_click_count);
                        if (g_marker_text && UObject::IsReal(g_marker_text))
                        {
                            File::StringType msg{STR("LivingBase In-Game Panel -- Clicked ")};
                            msg += std::to_wstring(g_click_count);
                            msg += STR(" time(s)");
                            SetTextBlockText(g_marker_text, msg);
                        }
                    },
                    nullptr,
                    button);
            return true;
        }

        auto EnsureBuilt() -> bool
        {
            // IsReal() only confirms the pointer itself is a live, non-garbage-collected UObject
            // -- it says nothing about whether the object's own reflection actually works.
            // Confirmed live (2026-08-23): AddToViewport/RemoveFromParent intermittently fail to
            // resolve via GetFunctionByNameInChain on an otherwise-valid, freshly-built root, with
            // no code change between a failing and a working attempt -- root cause not pinned
            // down (not memory corruption from WireButtonClick, confirmed by testing with that
            // call fully disabled; not simply "still loading", since it stays broken for the rest
            // of that session even after 20+ seconds). Since caching a root that LOOKS valid but
            // can't actually be shown would permanently break the panel for the rest of the
            // session, verify it's genuinely usable every time, not just alive -- if not, discard
            // and rebuild from scratch rather than getting stuck.
            if (g_root_widget && UObject::IsReal(g_root_widget) && g_root_widget->GetFunctionByNameInChain(STR("AddToViewport")) != nullptr)
            {
                return true;
            }
            if (g_root_widget)
            {
                Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] InGamePanel: existing root failed a usability check, rebuilding from scratch\n"));
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
            UClass* button_class = FindUMGClass(STR("Button"));
            if (!user_widget_class || !widget_tree_class || !canvas_panel_class || !border_class || !text_block_class || !button_class)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: one or more /Script/UMG classes not found\n"));
                return false;
            }

            // TEMP diagnostic (2026-08-23): count how many UFunctions user_widget_class's own
            // function chain actually has -- distinguishes "the class object is a near-empty
            // stub" (count near 0) from "AddToViewport specifically is missing from an otherwise
            // normal function table" (a large count, just not this one).
            {
                int32_t func_count = 0;
                for ([[maybe_unused]] UFunction* fn : TFieldRange<UFunction>(user_widget_class, EFieldIterationFlags::IncludeAll))
                {
                    ++func_count;
                }
                Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel: diag user_widget_class function count = {}\n"), func_count);
            }

            UObject* root = UObjectGlobals::NewObject<UObject>(game_instance, user_widget_class, FName(STR("LivingBaseInGamePanelRoot")));
            UObject* widget_tree = root ? UObjectGlobals::NewObject<UObject>(root, widget_tree_class, FName(STR("LivingBaseInGamePanelTree"))) : nullptr;
            UObject* canvas = widget_tree ? UObjectGlobals::NewObject<UObject>(widget_tree, canvas_panel_class, FName(STR("LivingBaseInGamePanelCanvas"))) : nullptr;
            UObject* border = canvas ? UObjectGlobals::NewObject<UObject>(canvas, border_class, FName(STR("LivingBaseInGamePanelBorder"))) : nullptr;
            UObject* text_block = border ? UObjectGlobals::NewObject<UObject>(border, text_block_class, FName(STR("LivingBaseInGamePanelText"))) : nullptr;
            // Phase 1: one Button (sibling of Border on the canvas) with its own TextBlock label.
            UObject* button = canvas ? UObjectGlobals::NewObject<UObject>(canvas, button_class, FName(STR("LivingBaseInGamePanelButton"))) : nullptr;
            UObject* button_label = button ? UObjectGlobals::NewObject<UObject>(button, text_block_class, FName(STR("LivingBaseInGamePanelButtonLabel"))) : nullptr;
            if (!root || !widget_tree || !canvas || !border || !text_block || !button || !button_label)
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

            // UContentWidget::SetContent(UWidget* InContent) -- void return. Button is also a
            // UContentWidget (holds one child, its label) -- same call, same proven pattern.
            auto set_content = [](UObject* content_widget, UObject* content) -> bool {
                UFunction* set_content_fn = content_widget->GetFunctionByNameInChain(STR("SetContent"));
                if (!set_content_fn)
                {
                    return false;
                }
                struct SetContent_Params
                {
                    UObject* Content;
                };
                SetContent_Params params{content};
                content_widget->ProcessEvent(set_content_fn, &params);
                return true;
            };
            if (!set_content(border, text_block) || !set_content(button, button_label))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: SetContent UFunction not found\n"));
                return false;
            }

            // UCanvasPanel::AddChildToCanvas for the button too -- same proven pattern as border
            // above, sibling slot (default placement, see file header).
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
                AddChildToCanvas_Params params{button, nullptr};
                canvas->ProcessEvent(add_child_fn, &params);
            }

            if (!SetTextBlockText(text_block, STR("LivingBase In-Game Panel (Phase 1)")) || !SetTextBlockText(button_label, STR("Click Me")))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: SetText UFunction not found\n"));
                return false;
            }

            if (!WireButtonClick(button, STR("ForceLayoutPrepass")))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: click wiring failed, button will be inert\n"));
            }

            g_root_widget = root;
            g_marker_text = text_block;
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
