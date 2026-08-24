#include <InGamePanel.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <MenuStatus.hpp>
#include <SpawnMenu.hpp>

#include <Unreal/FField.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>

#include <memory>
#include <vector>

// Phase 0 (widget construction, 2026-08-22) -- CONFIRMED WORKING LIVE: toggles cleanly and
// repeatedly, on foot and on ship, survives a world reload.
//
// KNOWN INTERMITTENT ISSUE, not fully root-caused: in some game sessions,
// GetFunctionByNameInChain(STR("AddToViewport")) (and other UUserWidget-inherited functions)
// fails to resolve on an otherwise perfectly valid, freshly-constructed root widget -- confirmed
// NOT simply "the class hasn't finished loading yet" (stays broken for 20+ seconds/many retries
// within an affected session, then a DIFFERENT session works cleanly with the exact same code). A
// working session's user_widget_class reports 176 functions in its chain (a normal, fully-populated
// table), so it isn't a bare/empty class stub either when it fails. EnsureBuilt() defends against
// this rather than solving it: every call verifies AddToViewport is actually resolvable, not just
// that the cached root is still a live UObject, and rebuilds from scratch if not -- turns
// "permanently stuck broken for the rest of the session" into "self-heals on the next press" in
// practice.
//
// Click detection -- THREE attempts, this file's own history:
//
// Attempt 1 (Phase 1's spike, and the first pass of Phase 2): bound each Button's OnClicked
// delegate to a harmless inherited UFUNCTION (UWidget::ForceLayoutPrepass) and used
// RegisterPostHookForInstance to detect a real click. Looked solid with exactly ONE button
// (Phase 1's spike): every observed click fired exactly once. Broke the moment Phase 2 introduced
// MULTIPLE buttons -- live logging (2026-08-23) proved a SINGLE real click fired EVERY hook ever
// registered on that shared, inherited UFunction (Refresh, Spawn, Replace, and every visible tree
// row, all within ~2ms of each other), not just the clicked one. UE4SS's own header comment for
// RegisterPostHookForInstance says it DOES filter correctly by instance pointer, so the likelier
// explanation is that ForceLayoutPrepass itself just isn't a click-exclusive signal -- plausibly
// called by the engine's own normal Slate layout-invalidation pass on many widgets whenever
// ANYTHING in the tree re-layouts, which a click's own OnClicked broadcast would trigger, meaning
// every one of those 7 fires was a genuine, correctly-instance-scoped ForceLayoutPrepass call,
// just not one caused by a real click on THAT specific widget.
//
// Attempt 2: HANDOFF_INGAME_PANEL.md's own documented fallback -- per-frame poll of
// GetAsyncKeyState(VK_LBUTTON) for a rising edge, combined with UWidget::IsHovered(). CONFIRMED
// LIVE BROKEN (2026-08-23): logged EVERY button-state transition (not just a rising edge) from
// Tick(), across multiple live tests with the cursor genuinely released (game's own menu open, and
// separately via LivingBase's own '=' mouse-release key) -- zero transitions were ever logged.
// Tried routing through UE4SS's own register_keydown_event(Input::Key::LEFT_MOUSE_BUTTON, ...)
// next (same pipeline the working P toggle key uses) -- ALSO confirmed live broken, unsurprising
// in hindsight: UE4SS's Win32AsyncInputSource::process_event calls GetAsyncKeyState(key) for EVERY
// subscribed key, mouse buttons included, so it's driven by the exact same primitive that already
// failed. This game evidently doesn't expose legacy mouse-button state to GetAsyncKeyState at all
// (plausible if it uses raw/exclusive mouse input for aiming), while keyboard state remains
// completely normal (the same API is how the P toggle key itself works).
//
// Attempt 3 (current): keep Attempt 1's mechanism -- it DOES reliably fire at least once per real
// click, per the live evidence above, it's just not exclusive to the clicked widget -- and fix
// that specificity gap directly instead of abandoning it: each hook's trampoline now checks
// UWidget::IsHovered() on that SPECIFIC widget at the moment it fires, and only invokes the real
// handler if true. A real click always has the cursor over the clicked widget at that instant;
// ForceLayoutPrepass firing on some OTHER widget for unrelated layout reasons essentially never
// does. See WireButtonClick's own comment below for the mechanics.
//
// Phase 2 (2026-08-23): real content -- SpawnMenu's tree + Spawn/Replace, ported per
// HANDOFF_INGAME_PANEL.md. The tree/parsing/selection/write_request logic is NOT duplicated here;
// SpawnMenu.hpp now exposes read/write accessors over the exact same state its own ImGui Draw()
// uses (see that header's own comment), so this file only builds/rebuilds UMG rows from it. A
// title row, a Refresh button, a ScrollBox of tree rows (category rows toggle expand/collapse,
// leaf rows select), and a Spawn/Replace button row gated on SpawnMenu::CanSpawn()/CanReplace()
// and MenuStatus::IsRestoring(), refreshed once a frame via Tick() (called from
// SpawnMenuMod::on_update()) since a target-lock change elsewhere shouldn't require a click here
// to notice it.
//
// Selection highlight is a plain text-prefix marker (">> "), not SetRenderOpacity/
// SetColorAndOpacity as the handoff suggested -- deliberately simpler for this pass; real styling
// is a Phase 4 concern, matching this file's own "wonky on purpose" precedent from Phase 0.
// Category rows use "[+]"/"[-]" prefixes for the same reason.
//
// Rows are rebuilt (explicit per-row RemoveChild + fresh NewObject per visible row) on every
// selection/expand change rather than diffed/pooled -- simplest correct thing, not yet a proven
// bottleneck at the tree sizes this mod deals with (spawn_menu.ini's own scale, low hundreds of
// leaves). Revisit with pooling only if that proves too slow live.
//
// CONFIRMED LIVE BROKEN (2026-08-23): the first version of this used UPanelWidget::ClearChildren()
// to empty the ScrollBox before repopulating it -- rows visibly piled up instead of replacing on
// every click. Root cause never independently re-confirmed (superseded by the bigger click-model
// rewrite above, which changed the whole call pattern), but rather than trust that unverified
// "clear everything" call again, this file tracks exactly which widgets it added
// (g_current_row_widgets) and removes each one explicitly via UPanelWidget::RemoveChild -- the
// direct sibling of AddChild (same class, adjacent in the reflection table), which IS confirmed
// live (rows do appear). Each row is also given a unique FName (g_next_row_name_id) rather than
// reusing a literal name across rebuilds, removing any dependence on NewObject's own
// name-collision-handling behavior as a second, independent fix for the same symptom.
//
// Same primitives HUDControl's Lua BuildPanel() uses (Other\HUDControl-...\Scripts\main.lua) --
// StaticFindObject to resolve each UMG class by its /Script/UMG.<ClassName> path, NewObject to
// construct instances, GetPropertyByNameInChain for structural object-reference properties that
// only exist as properties, not functions (WidgetTree, RootWidget), and ProcessEvent for
// everything else. Every ProcessEvent params struct below mirrors a REAL, well-known stock UMG
// function signature (never an invented/guessed one) -- see HANDOFF_INGAME_PANEL.md for why that
// distinction matters. ScrollBox/VerticalBox/HorizontalBox all use the generic
// UPanelWidget::AddChild (NOT a per-type "AddChildToX" wrapper) -- CanvasPanel is the one
// confirmed-live exception (AddChildToCanvas, proven in Phase 0), used there because its slot
// needs the extra position data a generic AddChild's default slot doesn't carry.
//
// Also noted during Phase 1 live testing: unlike StandaloneWindow (a separate OS window, mouse
// released via normal window focus), this in-viewport panel needs the game's own menu open to
// release the mouse cursor for clicking -- still true here, still an open Phase 4 item (RedFalcon's
// own framing: "the equivalent of = now").

namespace RC::LivingBaseSpawnMenu::InGamePanel
{
    using namespace RC::Unreal;

    namespace
    {
        UObject* g_root_widget{};
        UObject* g_scroll_box{};
        UObject* g_refresh_button{};
        UObject* g_spawn_button{};
        UObject* g_replace_button{};
        bool g_visible{};

        // Set by a click handler, consumed by Tick() -- deferred rather than rebuilding the tree
        // immediately inside the hook trampoline itself, since that trampoline runs from
        // RegisterPostHookForInstance's own callback (an engine callstack, possibly still mid the
        // real OnClicked broadcast for that widget) -- CONFIRMED LIVE FREEZING when this was tried
        // (2026-08-23, see EARLIER in this file's git history / the freeze fix): destroying the
        // in-flight clicked widget (ClearChildren, back when that was still used) out from under
        // Slate mid-broadcast hung the game. Tick() runs once per frame from on_update(), off any
        // Slate callstack, so rebuilding there instead is safe.
        bool g_tree_dirty{};

        // Exactly what's currently sitting in g_scroll_box -- removed explicitly (RemoveChild)
        // before each rebuild instead of trusting ClearChildren(). See file header comment.
        std::vector<UObject*> g_current_row_widgets;
        int32_t g_next_row_name_id{};

        // customData round-tripped through RegisterPostHookForInstance (see WireButtonClick) --
        // lets one shared hook trampoline dispatch to a DIFFERENT handler per button (row click vs.
        // Refresh vs. Spawn vs. Replace) without needing a capturing lambda (which can't convert to
        // the plain function pointer the hook API expects), AND carries the widget pointer itself
        // so the trampoline can hover-test the SPECIFIC clicked widget (see file header, Attempt 3).
        using ClickHandler = void (*)(void* payload);
        struct HookContext
        {
            UObject* widget;
            ClickHandler handler;
            void* payload;
        };
        std::vector<std::unique_ptr<HookContext>> g_hook_contexts;

        struct RowClickContext
        {
            const SpawnMenu::MenuNode* node;
            std::string full_path;
        };
        std::vector<std::unique_ptr<RowClickContext>> g_row_click_contexts;

        auto RebuildTree() -> void;

        auto FindUMGClass(const File::CharType* short_name) -> UClass*
        {
            File::StringType path{STR("/Script/UMG.")};
            path += short_name;
            return UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, path);
        }

        // spawn_menu.ini's labels are plain ASCII (roster/category names) -- a per-char widen is
        // exact for that and not meant to handle arbitrary Unicode.
        auto ToWide(const std::string& s) -> File::StringType { return File::StringType(s.begin(), s.end()); }

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
        // bad vtable call entirely instead of trusting that unverified "official" API.
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

        // Zero-arg, zero-return UFUNCTION call (RemoveFromParent).
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

        // UWidget::SetIsEnabled(bool) -- void return. Slate propagates a disabled state down to a
        // panel's children too, so disabling a container (e.g. the ScrollBox) also blocks clicks
        // on the rows inside it, same nested-disable shape as ImGui's BeginDisabled. Also expected
        // to gate hover reporting (a disabled widget shouldn't register as hovered for the
        // WireButtonClick trampoline's IsHovered() check below) -- standard Slate behavior, not yet
        // independently live-verified for this specific build; watch for a disabled button still
        // triggering if this assumption is wrong.
        auto SetWidgetEnabled(UObject* target, bool enabled) -> bool
        {
            UFunction* function = target->GetFunctionByNameInChain(STR("SetIsEnabled"));
            if (!function)
            {
                return false;
            }
            struct SetIsEnabled_Params
            {
                bool bInIsEnabled;
            };
            SetIsEnabled_Params params{enabled};
            target->ProcessEvent(function, &params);
            return true;
        }

        // UWidget::IsHovered() -- bool return, no params. Ordinary reflected getter, no
        // delegate/hook machinery -- Slate updates this from its own normal mouse-enter/leave
        // handling.
        auto IsWidgetHovered(UObject* widget) -> bool
        {
            UFunction* function = widget->GetFunctionByNameInChain(STR("IsHovered"));
            if (!function)
            {
                return false;
            }
            struct IsHovered_Params
            {
                bool ReturnValue;
            };
            IsHovered_Params params{};
            widget->ProcessEvent(function, &params);
            return params.ReturnValue;
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

        // UContentWidget::SetContent(UWidget* InContent) -- void return. Border/Button are both
        // single-child UContentWidgets.
        auto SetContent(UObject* content_widget, UObject* content) -> bool
        {
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
        }

        // UCanvasPanel::AddChildToCanvas(UWidget* Content) -> UCanvasPanelSlot* -- return value
        // unused (default slot placement, see file header). Confirmed live in Phase 0.
        auto AddChildToCanvas(UObject* canvas, UObject* content) -> bool
        {
            UFunction* add_child_fn = canvas->GetFunctionByNameInChain(STR("AddChildToCanvas"));
            if (!add_child_fn)
            {
                return false;
            }
            struct AddChildToCanvas_Params
            {
                UObject* Content;
                UObject* ReturnValue;
            };
            AddChildToCanvas_Params params{content, nullptr};
            canvas->ProcessEvent(add_child_fn, &params);
            return true;
        }

        // UPanelWidget::AddChild(UWidget* Content) -> UPanelSlot* -- the generic base-class add,
        // used for VerticalBox/HorizontalBox/ScrollBox (see file header on why the generic one was
        // chosen over per-type "AddChildToX" wrappers here).
        auto AddChildGeneric(UObject* panel, UObject* content) -> bool
        {
            UFunction* add_child_fn = panel->GetFunctionByNameInChain(STR("AddChild"));
            if (!add_child_fn)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: AddChild UFunction not found on panel\n"));
                return false;
            }
            struct AddChild_Params
            {
                UObject* Content;
                UObject* ReturnValue;
            };
            AddChild_Params params{content, nullptr};
            panel->ProcessEvent(add_child_fn, &params);
            return true;
        }

        // UPanelWidget::RemoveChild(UWidget* Content) -> bool -- AddChildGeneric's direct sibling
        // on the same base class (see file header on why this replaced ClearChildren()).
        auto RemoveChildFromPanel(UObject* panel, UObject* content) -> bool
        {
            UFunction* remove_child_fn = panel->GetFunctionByNameInChain(STR("RemoveChild"));
            if (!remove_child_fn)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: RemoveChild UFunction not found on panel\n"));
                return false;
            }
            struct RemoveChild_Params
            {
                UObject* Content;
                bool ReturnValue;
            };
            RemoveChild_Params params{content, false};
            panel->ProcessEvent(remove_child_fn, &params);
            return true;
        }

        // Attempt 3's click-detection mechanism -- CONFIRMED WORKING LIVE (2026-08-23): expand,
        // select, and Spawn all functioned correctly; log evidence from that same test showed
        // ~50+ hook fires per real click (routine ForceLayoutPrepass noise across many widgets)
        // with exactly one hovered=true match each time, precisely the design intent. See file
        // header for the two prior attempts and why they failed. Binds the button's OnClicked
        // delegate to a UFunction it already inherits and is harmless to actually invoke
        // (UWidget::ForceLayoutPrepass), then registers a native post-hook scoped to this one
        // button instance on that same UFunction, EXACTLY like Attempt 1 -- the mechanism itself
        // was never the problem. The fix is in the trampoline: it hover-tests THIS SPECIFIC widget
        // before dispatching to the real handler, filtering out ForceLayoutPrepass fires caused by
        // something other than a real click on this exact widget.
        //
        // Deliberately does NOT go through FMulticastDelegateProperty::AddDelegate -- that's a
        // "Virtual Function" per its own header comment, backed by the SAME
        // VTableLayoutMap-resolved-vtable-call mechanism that already crashed once in Phase 0
        // (FObjectPropertyBase::SetObjectPropertyValue, see SetObjectProperty's own comment
        // above). TScriptDelegate::BindUFunction and TArray::Add, by contrast, are plain
        // non-virtual template methods -- no vtable dependency.
        auto WireButtonClick(UObject* button, const File::CharType* bound_function_name, ClickHandler handler, void* payload) -> bool
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

            g_hook_contexts.push_back(std::make_unique<HookContext>(HookContext{button, handler, payload}));
            HookContext* ctx = g_hook_contexts.back().get();

            bound_function->RegisterPostHookForInstance(
                    [](UnrealScriptFunctionCallableContext&, void* data) {
                        auto* hook_ctx = static_cast<HookContext*>(data);
                        if (!hook_ctx || !hook_ctx->handler)
                        {
                            return;
                        }
                        const bool real = hook_ctx->widget && UObject::IsReal(hook_ctx->widget);
                        const bool hovered = real && IsWidgetHovered(hook_ctx->widget);
                        if (hovered)
                        {
                            hook_ctx->handler(hook_ctx->payload);
                        }
                    },
                    ctx,
                    button);
            return true;
        }

        auto OnRowClicked(void* payload) -> void
        {
            auto* ctx = static_cast<RowClickContext*>(payload);
            if (!ctx || !ctx->node)
            {
                Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] InGamePanel: OnRowClicked fired with null ctx/node\n"));
                return;
            }
            if (SpawnMenu::IsLeaf(*ctx->node))
            {
                SpawnMenu::SelectLeaf(*ctx->node, ctx->full_path);
            }
            else
            {
                SpawnMenu::ToggleExpanded(*ctx->node);
            }
            g_tree_dirty = true;
        }

        auto OnRefreshClicked(void*) -> void
        {
            SpawnMenu::Reload();
            g_tree_dirty = true;
        }

        auto OnSpawnClicked(void*) -> void { SpawnMenu::SpawnSelected(); }
        auto OnReplaceClicked(void*) -> void { SpawnMenu::ReplaceSelected(); }

        struct FlatRow
        {
            const SpawnMenu::MenuNode* node;
            std::string full_path;
            int depth;
        };

        // Category rows only recurse into their children when expanded -- same "flatten what's
        // currently visible" shape a TreeNode's implicit recursion gives ImGui for free.
        auto FlattenRows(const SpawnMenu::MenuNode& node, const std::string& path_prefix, int depth, std::vector<FlatRow>& out) -> void
        {
            for (int i = 0; i < SpawnMenu::ChildCount(node); ++i)
            {
                const SpawnMenu::MenuNode& child = SpawnMenu::ChildAt(node, i);
                std::string full_path = path_prefix.empty() ? SpawnMenu::Label(child) : path_prefix + " / " + SpawnMenu::Label(child);
                out.push_back(FlatRow{&child, full_path, depth});
                if (!SpawnMenu::IsLeaf(child) && SpawnMenu::IsExpanded(child))
                {
                    FlattenRows(child, full_path, depth + 1, out);
                }
            }
        }

        auto MakeTextRow(const File::StringType& text) -> UObject*
        {
            UClass* text_block_class = FindUMGClass(STR("TextBlock"));
            if (!text_block_class || !g_root_widget)
            {
                return nullptr;
            }
            File::StringType name{STR("LivingBaseEmptyRow")};
            name += std::to_wstring(g_next_row_name_id++);
            UObject* label = UObjectGlobals::NewObject<UObject>(g_root_widget, text_block_class, FName(name));
            if (!label || !SetTextBlockText(label, text))
            {
                return nullptr;
            }
            return label;
        }

        // One row = one Button (full-width default slot in the ScrollBox) with a TextBlock label
        // -- indentation and expand/selection state are shown as a plain text prefix, not real
        // layout padding (see file header on why).
        auto MakeRowButton(const FlatRow& row) -> UObject*
        {
            UClass* button_class = FindUMGClass(STR("Button"));
            UClass* text_block_class = FindUMGClass(STR("TextBlock"));
            if (!button_class || !text_block_class || !g_root_widget)
            {
                return nullptr;
            }
            const int32_t name_id = g_next_row_name_id++;
            File::StringType button_name{STR("LivingBaseRow")};
            button_name += std::to_wstring(name_id);
            File::StringType label_name{STR("LivingBaseRowLabel")};
            label_name += std::to_wstring(name_id);
            UObject* button = UObjectGlobals::NewObject<UObject>(g_root_widget, button_class, FName(button_name));
            UObject* label = button ? UObjectGlobals::NewObject<UObject>(button, text_block_class, FName(label_name)) : nullptr;
            if (!button || !label || !SetContent(button, label))
            {
                return nullptr;
            }

            const bool leaf = SpawnMenu::IsLeaf(*row.node);
            File::StringType text(static_cast<size_t>(row.depth) * 2, STR(' '));
            if (!leaf)
            {
                text += SpawnMenu::IsExpanded(*row.node) ? STR("[-] ") : STR("[+] ");
            }
            else
            {
                text += SpawnMenu::IsSelected(*row.node) ? STR(">> ") : STR("    ");
            }
            text += ToWide(SpawnMenu::Label(*row.node));
            if (!SetTextBlockText(label, text))
            {
                return nullptr;
            }

            g_row_click_contexts.push_back(std::make_unique<RowClickContext>(RowClickContext{row.node, row.full_path}));
            WireButtonClick(button, STR("ForceLayoutPrepass"), &OnRowClicked, g_row_click_contexts.back().get());
            return button;
        }

        auto RefreshActionButtons() -> void
        {
            const bool restoring = MenuStatus::IsRestoring();
            if (g_scroll_box)
            {
                SetWidgetEnabled(g_scroll_box, !restoring);
            }
            if (g_refresh_button)
            {
                SetWidgetEnabled(g_refresh_button, !restoring);
            }
            if (g_spawn_button)
            {
                SetWidgetEnabled(g_spawn_button, !restoring && SpawnMenu::CanSpawn());
            }
            if (g_replace_button)
            {
                SetWidgetEnabled(g_replace_button, !restoring && SpawnMenu::CanReplace());
            }
        }

        auto RebuildTree() -> void
        {
            if (!g_scroll_box)
            {
                return;
            }
            // Explicit per-row removal, NOT ClearChildren() -- see file header comment on why.
            for (UObject* old_row : g_current_row_widgets)
            {
                RemoveChildFromPanel(g_scroll_box, old_row);
            }
            g_current_row_widgets.clear();
            // g_row_click_contexts is deliberately NOT cleared here -- g_hook_contexts (see
            // WireButtonClick) holds raw pointers into it, and g_hook_contexts is itself
            // deliberately grow-only (only dropped on a full panel rebuild, see EnsureBuilt) to
            // avoid a native hook firing after its own context was freed. Clearing this here while
            // that invariant holds would leave dangling payload pointers. Same "slow, bounded, but
            // safe" tradeoff as everything else in this file that grows across rebuilds.

            std::vector<FlatRow> rows;
            FlattenRows(SpawnMenu::RootNode(), "", 0, rows);

            if (rows.empty())
            {
                UObject* empty_row = MakeTextRow(STR("(no entries -- check spawn_menu.ini exists and Refresh)"));
                if (empty_row && AddChildGeneric(g_scroll_box, empty_row))
                {
                    g_current_row_widgets.push_back(empty_row);
                }
            }
            else
            {
                for (const FlatRow& row : rows)
                {
                    UObject* row_button = MakeRowButton(row);
                    if (row_button && AddChildGeneric(g_scroll_box, row_button))
                    {
                        g_current_row_widgets.push_back(row_button);
                    }
                }
            }
            RefreshActionButtons();
        }

        auto EnsureBuilt() -> bool
        {
            // IsReal() only confirms the pointer itself is a live, non-garbage-collected UObject
            // -- it says nothing about whether the object's own reflection actually works. See
            // this file's own header comment on the intermittent AddToViewport-resolution issue.
            if (g_root_widget && UObject::IsReal(g_root_widget) && g_root_widget->GetFunctionByNameInChain(STR("AddToViewport")) != nullptr)
            {
                return true;
            }
            if (g_root_widget)
            {
                Output::send<LogLevel::Warning>(STR("[LivingBaseSpawnMenu] InGamePanel: existing root failed a usability check, rebuilding from scratch\n"));
            }
            g_root_widget = nullptr;
            g_scroll_box = nullptr;
            g_refresh_button = nullptr;
            g_spawn_button = nullptr;
            g_replace_button = nullptr;
            // Full rebuild -- the only point g_hook_contexts/g_row_click_contexts are dropped (see
            // RebuildTree's own comment on why they're not cleared per row-rebuild).
            g_hook_contexts.clear();
            g_row_click_contexts.clear();
            g_current_row_widgets.clear();
            g_tree_dirty = false;

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
            UClass* vertical_box_class = FindUMGClass(STR("VerticalBox"));
            UClass* horizontal_box_class = FindUMGClass(STR("HorizontalBox"));
            UClass* scroll_box_class = FindUMGClass(STR("ScrollBox"));
            if (!user_widget_class || !widget_tree_class || !canvas_panel_class || !border_class || !text_block_class || !button_class
                || !vertical_box_class || !horizontal_box_class || !scroll_box_class)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: one or more /Script/UMG classes not found\n"));
                return false;
            }

            // TEMP diagnostic, kept from Phase 1 (2026-08-23): count how many UFunctions
            // user_widget_class's own function chain actually has -- distinguishes "the class
            // object is a near-empty stub" (count near 0) from "AddToViewport specifically is
            // missing from an otherwise normal function table" (a large count, just not this one).
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
            UObject* outer_vbox = border ? UObjectGlobals::NewObject<UObject>(border, vertical_box_class, FName(STR("LivingBaseInGamePanelVBox"))) : nullptr;
            UObject* title_text = outer_vbox ? UObjectGlobals::NewObject<UObject>(outer_vbox, text_block_class, FName(STR("LivingBaseInGamePanelTitle"))) : nullptr;
            UObject* refresh_button = outer_vbox ? UObjectGlobals::NewObject<UObject>(outer_vbox, button_class, FName(STR("LivingBaseInGamePanelRefresh"))) : nullptr;
            UObject* refresh_label = refresh_button ? UObjectGlobals::NewObject<UObject>(refresh_button, text_block_class, FName(STR("LivingBaseInGamePanelRefreshLabel"))) : nullptr;
            UObject* scroll_box = outer_vbox ? UObjectGlobals::NewObject<UObject>(outer_vbox, scroll_box_class, FName(STR("LivingBaseInGamePanelScroll"))) : nullptr;
            UObject* action_row = outer_vbox ? UObjectGlobals::NewObject<UObject>(outer_vbox, horizontal_box_class, FName(STR("LivingBaseInGamePanelActionRow"))) : nullptr;
            UObject* spawn_button = action_row ? UObjectGlobals::NewObject<UObject>(action_row, button_class, FName(STR("LivingBaseInGamePanelSpawn"))) : nullptr;
            UObject* spawn_label = spawn_button ? UObjectGlobals::NewObject<UObject>(spawn_button, text_block_class, FName(STR("LivingBaseInGamePanelSpawnLabel"))) : nullptr;
            UObject* replace_button = action_row ? UObjectGlobals::NewObject<UObject>(action_row, button_class, FName(STR("LivingBaseInGamePanelReplace"))) : nullptr;
            UObject* replace_label = replace_button ? UObjectGlobals::NewObject<UObject>(replace_button, text_block_class, FName(STR("LivingBaseInGamePanelReplaceLabel"))) : nullptr;
            if (!root || !widget_tree || !canvas || !border || !outer_vbox || !title_text || !refresh_button || !refresh_label || !scroll_box
                || !action_row || !spawn_button || !spawn_label || !replace_button || !replace_label)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: widget construction failed partway through\n"));
                return false;
            }

            if (!SetObjectProperty(root, STR("WidgetTree"), widget_tree) || !SetObjectProperty(widget_tree, STR("RootWidget"), canvas))
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: WidgetTree/RootWidget property write failed\n"));
                return false;
            }

            const bool wired = AddChildToCanvas(canvas, border) && SetContent(border, outer_vbox) && AddChildGeneric(outer_vbox, title_text)
                    && SetContent(refresh_button, refresh_label) && AddChildGeneric(outer_vbox, refresh_button) && AddChildGeneric(outer_vbox, scroll_box)
                    && AddChildGeneric(outer_vbox, action_row) && SetContent(spawn_button, spawn_label) && AddChildGeneric(action_row, spawn_button)
                    && SetContent(replace_button, replace_label) && AddChildGeneric(action_row, replace_button);
            if (!wired)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: widget tree assembly failed\n"));
                return false;
            }

            const bool texted = SetTextBlockText(title_text, STR("LivingBase Spawn Menu")) && SetTextBlockText(refresh_label, STR("Refresh"))
                    && SetTextBlockText(spawn_label, STR("Spawn")) && SetTextBlockText(replace_label, STR("Replace"));
            if (!texted)
            {
                Output::send<LogLevel::Error>(STR("[LivingBaseSpawnMenu] InGamePanel: SetText UFunction not found\n"));
                return false;
            }

            WireButtonClick(refresh_button, STR("ForceLayoutPrepass"), &OnRefreshClicked, nullptr);
            WireButtonClick(spawn_button, STR("ForceLayoutPrepass"), &OnSpawnClicked, nullptr);
            WireButtonClick(replace_button, STR("ForceLayoutPrepass"), &OnReplaceClicked, nullptr);

            g_root_widget = root;
            g_scroll_box = scroll_box;
            g_refresh_button = refresh_button;
            g_spawn_button = spawn_button;
            g_replace_button = replace_button;

            SpawnMenu::Reload();
            RebuildTree();

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
        RefreshActionButtons();
        Output::send<LogLevel::Normal>(STR("[LivingBaseSpawnMenu] InGamePanel: shown\n"));
    }

    // Call once per frame from SpawnMenuMod::on_update(). Polls the Lua status bridge (same
    // self-throttled Poll() StandaloneWindow's own loop already calls, safe to call from both);
    // while visible, applies any tree change queued by a click (see WireButtonClick/OnRowClicked)
    // and keeps Spawn/Replace's enabled state in sync with a target-lock/restore-lock change that
    // happened from somewhere OTHER than a click on this panel (e.g. the in-game Num+ key).
    auto Tick() -> void
    {
        MenuStatus::Poll();
        if (g_visible)
        {
            if (g_tree_dirty)
            {
                g_tree_dirty = false;
                RebuildTree();
            }
            RefreshActionButtons();
        }
    }
} // namespace RC::LivingBaseSpawnMenu::InGamePanel
