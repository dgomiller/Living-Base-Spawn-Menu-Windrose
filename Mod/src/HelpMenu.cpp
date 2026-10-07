#include <HelpMenu.hpp>
#include <ImageLoader.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

#include <windows.h>

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace RC::LivingBaseSpawnMenu::HelpMenu
{
    namespace
    {
        // Lives alongside the DLL, in this mod's OWN folder -- not LivingBase's -- since this is
        // documentation for the window itself, not for LivingBase's keyboard controls (which
        // already have their own README.md).
        constexpr const char* HELP_TEXT_PATH = "ue4ss/Mods/LivingBaseSpawnMenu/help.txt";
        constexpr const char* HISTORY_PATH = "ue4ss/Mods/LivingBase/spawn_menu_history.txt";
        constexpr auto HISTORY_POLL_INTERVAL = std::chrono::milliseconds(500);
        // Own folder for the Instructions tab's inline images (2026-10-06, RedFalcon: "can we put
        // the images in their own folder for cleanliness") -- deploys from Mod/assets/images/,
        // same flat-copy convention CustomMenu.cpp's own swatches/ folder already uses.
        constexpr const char* IMAGES_DIR = "ue4ss/Mods/LivingBaseSpawnMenu/images/";

        // Table-of-contents rework (2026-10-06, RedFalcon: a documentation.docx he wrote became the
        // source for this tab's content -- "I'd like the default view to be the table of contents,
        // and it links to each matching section with images and a back button on the top right to
        // return to the ToC"; Overview folded onto the front page itself in a same-day follow-up:
        // "can we put the overview section on the same page as the ToC above it"). help.txt is now a
        // light markup, not flat text:
        //   ## Title       -- starts a new SECTION (becomes a Contents link + its own page)
        //   # Text         -- sub-header within whatever section/front-page content is current
        //   + Text         -- top-level NUMBERED item (auto 1./2./3.../ -- restarts at each Section
        //                      or front page; matches the docx's own real numbered-list paragraphs,
        //                      used by every tab page whose screenshot has numbered callouts)
        //   - Text         -- top-level PLAIN bullet (dot) -- for the handful of sections whose docx
        //                      lists were never numbered (Installation, Custom Configuration, Basic
        //                      Controls, Credits, License)
        //     - Text       -- (2-space indent) SUB-bullet, dot, one level in -- same real structure
        //   ![filename]        the docx had (ilvl 1/2 were always plain bullets even where ilvl 0 was
        //       - Text         numbered). Further 2-space indents nest further (ilvl 2).
        //   ![filename]    -- inline image, loaded from IMAGES_DIR + filename
        //   ![filename|H]  -- same, but capped to H px tall (scaled down proportionally if the
        //                      source is taller; never scaled up) -- RedFalcon, 2026-10-06: "the
        //                      second image on basic controls is much too large. Make it the same
        //                      size as the legend."
        //   > Text         -- ALIGNED text: lines up with the text (not the margin) of whichever
        //                      Bullet/NumberedItem came before it, and -- unlike every other line
        //                      kind -- does NOT reset that alignment, so an image right after it
        //                      still lines up too (2026-10-06, RedFalcon: "the Note under selfie
        //                      should be in line with the text above it, and also the image" --
        //                      Photo Mode's Selfie bullet, its own NOTE aside, and the screenshot
        //                      after it all needed to share one left edge, not just the bullet).
        //   (blank line)   -- spacing
        //   anything else  -- plain wrapped text (a bare http(s):// URL anywhere in the line renders
        //                      as a clickable hyperlink -- see DrawTextWithLink/DrawHyperlink)
        // Everything BEFORE the first "## " line is the front-page content (shown above the
        // Contents list, never its own linked page) -- that's where help.txt's own Overview text
        // now lives.
        enum class LineKind
        {
            Blank,
            Text,
            AlignedText,
            Bullet,
            NumberedItem,
            SubHeader,
            Image,
        };
        struct Line
        {
            LineKind kind;
            std::string text;
            int depth{0};           // Bullet only: 0 = top level, 1+ = nested (always dot, never numbered)
            float imageMaxH{0.0f};  // Image only: 0 = no cap (scale to fit width only)
        };
        struct Section
        {
            std::string title;
            std::vector<Line> lines;
        };

        std::vector<Line> g_intro;
        std::vector<Section> g_sections;
        // -1 = front page (intro + Contents list). 0..size-1 = viewing that section's own page.
        int g_current_section = -1;

        auto ParseLine(const std::string& rawline) -> Line
        {
            if (rawline.empty())
            {
                return { LineKind::Blank, "", 0, 0.0f };
            }
            size_t indent = 0;
            while (indent < rawline.size() && rawline[indent] == ' ')
            {
                ++indent;
            }
            std::string line = rawline.substr(indent);

            if (indent == 0 && line.rfind("# ", 0) == 0)
            {
                return { LineKind::SubHeader, line.substr(2), 0, 0.0f };
            }
            if (indent == 0 && line.rfind("+ ", 0) == 0)
            {
                return { LineKind::NumberedItem, line.substr(2), 0, 0.0f };
            }
            if (indent == 0 && line.rfind("> ", 0) == 0)
            {
                return { LineKind::AlignedText, line.substr(2), 0, 0.0f };
            }
            if (line.rfind("- ", 0) == 0)
            {
                return { LineKind::Bullet, line.substr(2), static_cast<int>(indent / 2), 0.0f };
            }
            if (indent == 0 && line.size() >= 3 && line.rfind("![", 0) == 0 && line.back() == ']')
            {
                std::string inner = line.substr(2, line.size() - 3);
                auto bar = inner.find('|');
                std::string fname = (bar == std::string::npos) ? inner : inner.substr(0, bar);
                float maxH = (bar == std::string::npos) ? 0.0f : std::strtof(inner.c_str() + bar + 1, nullptr);
                return { LineKind::Image, fname, 0, maxH };
            }
            return { LineKind::Text, rawline, 0, 0.0f };
        }

        auto ParseHelpText(const std::string& text) -> void
        {
            g_intro.clear();
            g_sections.clear();
            g_current_section = -1; // a reload always lands back on the front page

            std::stringstream ss(text);
            std::string rawline;
            Section* current = nullptr;
            while (std::getline(ss, rawline))
            {
                if (!rawline.empty() && rawline.back() == '\r')
                {
                    rawline.pop_back();
                }
                if (rawline.rfind("## ", 0) == 0)
                {
                    g_sections.push_back(Section{ rawline.substr(3), {} });
                    current = &g_sections.back();
                    continue;
                }
                Line parsed = ParseLine(rawline);
                if (current)
                {
                    current->lines.push_back(std::move(parsed));
                }
                else
                {
                    g_intro.push_back(std::move(parsed));
                }
            }
        }

        std::string g_help_text;

        std::vector<std::string> g_history;
        std::chrono::steady_clock::time_point g_last_history_poll{};

        auto LoadHelpText() -> void
        {
            std::ifstream f(HELP_TEXT_PATH);
            if (!f)
            {
                g_help_text = "(help.txt not found -- expected at ue4ss/Mods/LivingBaseSpawnMenu/help.txt)";
                ParseHelpText(g_help_text);
                return;
            }
            std::stringstream buffer;
            buffer << f.rdbuf();
            g_help_text = buffer.str();
            ParseHelpText(g_help_text);
        }

        auto PollHistory(bool force) -> void
        {
            auto now = std::chrono::steady_clock::now();
            if (!force && now - g_last_history_poll < HISTORY_POLL_INTERVAL)
            {
                return;
            }
            g_last_history_poll = now;
            std::ifstream f(HISTORY_PATH);
            if (!f)
            {
                return;
            }
            g_history.clear();
            std::string line;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    g_history.push_back(line);
                }
            }
        }

        constexpr ImVec4 kHeaderColor = ImVec4(0.85f, 0.64f, 0.20f, 1.0f);
        constexpr ImVec4 kLinkColor = ImVec4(0.40f, 0.68f, 1.0f, 1.0f);
        constexpr float kBulletIndentPx = 20.0f;

        // Clickable hyperlink (2026-10-06, RedFalcon: "Can we make links clickable") -- ImGui has no
        // native hyperlink widget, so this draws colored text, underlines it on hover with a hand
        // cursor, and opens it in the OS default browser via ShellExecuteA on click. shell32.lib is
        // already linked (used elsewhere in this DLL), so no new link dependency.
        auto DrawHyperlink(const std::string& url) -> void
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kLinkColor);
            ImGui::TextUnformatted(url.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImVec2 lineStart = ImGui::GetItemRectMin();
                ImVec2 lineEnd = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(lineStart.x, lineEnd.y), lineEnd, ImGui::ColorConvertFloat4ToU32(kLinkColor));
                ImGui::SetTooltip("Open %s", url.c_str());
            }
            if (ImGui::IsItemClicked())
            {
                ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }

        // Finds the first http(s):// URL in `text` (at most one per line across every line in this
        // file's help.txt, so "first" is also "only" in practice). Trims trailing sentence
        // punctuation (". ," etc right after the URL) back into the suffix so "...mods/519. This
        // project" doesn't turn the period into part of the link.
        auto FindUrl(const std::string& text, size_t& start, size_t& end) -> bool
        {
            size_t pos = text.find("http://");
            size_t posS = text.find("https://");
            if (posS != std::string::npos && (pos == std::string::npos || posS < pos))
            {
                pos = posS;
            }
            if (pos == std::string::npos)
            {
                return false;
            }
            size_t stop = text.find_first_of(" \t", pos);
            if (stop == std::string::npos)
            {
                stop = text.size();
            }
            while (stop > pos && (text[stop - 1] == '.' || text[stop - 1] == ',' || text[stop - 1] == ')'))
            {
                --stop;
            }
            start = pos;
            end = stop;
            return true;
        }

        // Renders one logical line of body text, splitting out a bare URL (if any) as a real
        // clickable hyperlink. The link gets its OWN line rather than being chained in with
        // SameLine() either side (2026-10-06, RedFalcon: "The Credits and Attribution screen
        // descriptions are smooshed all the way to the right side, can we have them be their own
        // text below, like using a <br/> after the url" -- a long URL mid-sentence used to push
        // everything after it hard against the right edge with almost no room left to wrap in).
        // Any text before the URL, the URL itself, and any text after it now each wrap on their own
        // line instead.
        auto DrawTextWithLink(const std::string& text) -> void
        {
            size_t s = 0, e = 0;
            if (!FindUrl(text, s, e))
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopTextWrapPos();
                return;
            }
            std::string before = text.substr(0, s);
            std::string url = text.substr(s, e - s);
            std::string after = text.substr(e);
            if (!before.empty())
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(before.c_str());
                ImGui::PopTextWrapPos();
            }
            DrawHyperlink(url);
            if (!after.empty())
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(after.c_str());
                ImGui::PopTextWrapPos();
            }
        }

        // Deliberately minimal -- ImGui has no real markdown renderer, and pulling one in for a
        // handful of headers/bullets/images isn't worth it. See ParseLine/ParseHelpText's own
        // header comment for the line-kind grammar this renders. `numberCounter` is local to one
        // call (i.e. one section's/the front page's own lines) so numbering always restarts at 1
        // per page, matching the docx's own per-section numbered lists.
        auto DrawLines(const std::vector<Line>& lines) -> void
        {
            int numberCounter = 0;
            // Tracks where the TEXT (not the bullet/number glyph) of the most recent top-level item
            // started, so an image directly below it can line its own left edge up with that text
            // instead of the window margin (2026-10-06, RedFalcon: "images that are directly below a
            // numbered or bulleted line [should] have its left edge inline with the start of the
            // text above"). Reset on anything that isn't a Bullet/NumberedItem, so an image only
            // ever inherits alignment from a line immediately above it, never from something higher
            // up the page; left ON across consecutive images (e.g. Customize's two spawn-preview
            // screenshots back to back) so they all share that same alignment.
            float lastTextStartX = 0.0f;
            bool haveTextStartX = false;
            for (const auto& line : lines)
            {
                switch (line.kind)
                {
                    case LineKind::Blank:
                        ImGui::Spacing();
                        haveTextStartX = false;
                        break;
                    case LineKind::SubHeader:
                        ImGui::Spacing();
                        ImGui::TextColored(kHeaderColor, "%s", line.text.c_str());
                        ImGui::Separator();
                        haveTextStartX = false;
                        break;
                    case LineKind::NumberedItem:
                    {
                        ++numberCounter;
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%d.", numberCounter);
                        ImGui::TextUnformatted(buf);
                        ImGui::SameLine();
                        lastTextStartX = ImGui::GetCursorPosX();
                        haveTextStartX = true;
                        DrawTextWithLink(line.text);
                        break;
                    }
                    case LineKind::Bullet:
                        if (line.depth > 0)
                        {
                            ImGui::Indent(static_cast<float>(line.depth) * kBulletIndentPx);
                        }
                        // Depth 2+ uses a dash instead of a dot (2026-10-06, RedFalcon: "Is there a
                        // different bullet type we can use for the second indent to help
                        // differentiate between levels") -- depth 1 keeps the normal filled-dot
                        // Bullet() marker, depth 2 gets a plain "-" so a 3-level list (Customize's
                        // Body/Hair/Belts and Straps/Poses sub-items) reads as visually distinct
                        // levels instead of three identical dots at different indents.
                        if (line.depth >= 2)
                        {
                            ImGui::TextUnformatted("-");
                            ImGui::SameLine();
                        }
                        else
                        {
                            ImGui::Bullet();
                            ImGui::SameLine();
                        }
                        lastTextStartX = ImGui::GetCursorPosX();
                        haveTextStartX = true;
                        DrawTextWithLink(line.text);
                        if (line.depth > 0)
                        {
                            ImGui::Unindent(static_cast<float>(line.depth) * kBulletIndentPx);
                        }
                        break;
                    case LineKind::Text:
                        DrawTextWithLink(line.text);
                        haveTextStartX = false;
                        break;
                    case LineKind::AlignedText:
                        if (haveTextStartX)
                        {
                            ImGui::SetCursorPosX(lastTextStartX);
                        }
                        DrawTextWithLink(line.text);
                        // Deliberately does NOT clear haveTextStartX -- see this kind's own grammar
                        // comment: an image right after it must still inherit the same alignment.
                        break;
                    case LineKind::Image:
                    {
                        std::string path = std::string(IMAGES_DIR) + line.text;
                        int w = 0, h = 0;
                        ImTextureID tex = ImageLoader::GetOrLoad(path, w, h);
                        if (tex && w > 0 && h > 0)
                        {
                            if (haveTextStartX)
                            {
                                ImGui::SetCursorPosX(lastTextStartX);
                            }
                            float dispW = static_cast<float>(w);
                            float dispH = static_cast<float>(h);
                            if (line.imageMaxH > 0.0f && dispH > line.imageMaxH)
                            {
                                float capScale = line.imageMaxH / dispH;
                                dispW *= capScale;
                                dispH *= capScale;
                            }
                            // Cap at 600px wide, but never wider than the text column actually is
                            // from this (possibly indented/aligned) cursor position to the right
                            // edge (2026-10-06, RedFalcon: "Limit max image width to 600px but also
                            // don't let it go wider than the text's edge"), AND cap at 550px tall
                            // (same day, separate ask: "also dont let any images be over 550px
                            // tall"). Both caps computed as scale factors and the SMALLEST one wins,
                            // so a very tall-and-narrow or very wide-and-short image only ever
                            // shrinks as much as whichever single constraint actually bites.
                            constexpr float kMaxImageWidth = 600.0f;
                            constexpr float kMaxImageHeight = 550.0f;
                            float avail = ImGui::GetContentRegionAvail().x;
                            float widthCap = (avail > 0.0f) ? ((avail < kMaxImageWidth) ? avail : kMaxImageWidth) : kMaxImageWidth;
                            float scale = 1.0f;
                            if (dispW > widthCap)
                            {
                                scale = widthCap / dispW;
                            }
                            if (dispH * scale > kMaxImageHeight)
                            {
                                scale = kMaxImageHeight / dispH;
                            }
                            if (scale < 1.0f)
                            {
                                dispW *= scale;
                                dispH *= scale;
                            }
                            ImVec2 cursorBefore = ImGui::GetCursorScreenPos();
                            ImGui::Image(tex, ImVec2(dispW, dispH));
                            // Outline (2026-10-06, RedFalcon: "Can we put an outline around images?
                            // Many of them show parts of the menu and they blend into the background
                            // color") -- this ImGui build dropped Image()'s own border_col parameter
                            // in favor of a style color/var combo that isn't set up here, so the
                            // border is drawn by hand on the draw list instead of relying on either.
                            ImGui::GetWindowDrawList()->AddRect(
                                cursorBefore, ImVec2(cursorBefore.x + dispW, cursorBefore.y + dispH),
                                IM_COL32(150, 150, 150, 200), 0.0f, 0, 1.5f);
                        }
                        else
                        {
                            ImGui::TextDisabled("(image not found: %s)", line.text.c_str());
                        }
                        break;
                    }
                    default:
                        DrawTextWithLink(line.text);
                        haveTextStartX = false;
                        break;
                }
            }
        }
    } // namespace

    auto ReloadNow() -> void
    {
        LoadHelpText();
        PollHistory(true);
    }

    auto DrawInstructionsTab() -> void
    {
        ImGui::BeginChild("##help_text", ImVec2(0.0f, 0.0f), false);
        if (g_current_section < 0 || g_current_section >= static_cast<int>(g_sections.size()))
        {
            // Front page: intro (Overview) content, then the clickable Contents list.
            ImGui::TextColored(kHeaderColor, "Living Base Enhanced");
            ImGui::Separator();
            DrawLines(g_intro);
            ImGui::Spacing();
            ImGui::TextColored(kHeaderColor, "Contents");
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(g_sections.size()); ++i)
            {
                ImGui::PushID(i);
                if (ImGui::Selectable(g_sections[static_cast<size_t>(i)].title.c_str()))
                {
                    g_current_section = i;
                }
                ImGui::PopID();
            }
        }
        else
        {
            // Section page: title + a right-aligned "Back to Contents" button on the SAME line
            // (SameLine with an absolute offset places it at the right edge regardless of the
            // title's own length -- same idiom TargetListMenu.cpp's right panel already uses).
            // CRASH FIX (2026-10-06): clicking Back used to set g_current_section = -1 and then
            // fall straight through to the DrawLines call below in the SAME frame, which cast that
            // -1 to size_t (a huge out-of-range index) to index g_sections -- reliable crash. Cache
            // the section index BEFORE the button can change it, so this frame always finishes
            // rendering the page it started rendering; the index change only takes effect next frame.
            constexpr float kBackButtonWidth = 150.0f;
            const int sectionIdx = g_current_section;
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::TextColored(kHeaderColor, "%s", g_sections[static_cast<size_t>(sectionIdx)].title.c_str());
            ImGui::SameLine(avail - kBackButtonWidth);
            if (ImGui::Button("Back to Contents##top", ImVec2(kBackButtonWidth, 0.0f)))
            {
                g_current_section = -1;
            }
            ImGui::Separator();
            DrawLines(g_sections[static_cast<size_t>(sectionIdx)].lines);
            // A second Back button at the bottom too (2026-10-06, RedFalcon: "Some pages are very
            // long, can we also put a back to contents button at the bottom") -- same guard as the
            // top one: only takes effect next frame, never indexes g_sections with the new value
            // this frame. Right-aligned to match the top one (RedFalcon, same day: "I'd like the
            // second ... button on the right as well to match"), and given its own "##bottom" ID --
            // sharing the literal label "Back to Contents" with the top button with no ID suffix at
            // all was a real ID collision (RedFalcon: "the buttons have conflicting IDs"): ImGui
            // identifies a widget by its label hashed with the ID stack, and neither button pushed
            // anything onto that stack to tell them apart.
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - kBackButtonWidth);
            if (ImGui::Button("Back to Contents##bottom", ImVec2(kBackButtonWidth, 0.0f)))
            {
                g_current_section = -1;
            }
        }
        ImGui::EndChild();
    }

    auto DrawHistoryTab() -> void
    {
        PollHistory(false);

        ImGui::TextDisabled("Every message shown as an on-screen toast this session.");
        ImGui::Separator();
        ImGui::BeginChild("##help_history", ImVec2(0.0f, 0.0f), false);
        // Auto-follow the bottom ONLY if the view was already there before this frame's entries
        // were added -- otherwise a user who scrolled up to read older history would get yanked
        // back down the next time something toasts.
        bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        for (const auto& entry : g_history)
        {
            ImGui::TextWrapped("%s", entry.c_str());
        }
        if (wasAtBottom)
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
} // namespace RC::LivingBaseSpawnMenu::HelpMenu
