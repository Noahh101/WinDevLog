#include "device_ui.h"

#include "device_log.h"

#include <imgui.h>

void ApplyDeviceMonitorStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.088f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.22f, 0.28f, 0.75f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.15f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.14f, 0.32f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.40f, 0.59f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.12f, 0.29f, 0.43f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.17f, 0.23f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.22f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.16f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.075f, 0.088f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.085f, 0.10f, 0.13f, 1.0f);
}

void DrawDeviceMonitor()
{
    size_t connectedCount = 0;
    size_t disconnectedCount = 0;
    for (const DeviceEvent& event : eventHistory)
    {
        if (event.action == "Connected")
        {
            ++connectedCount;
        }
        else
        {
            ++disconnectedCount;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(1000.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "Win Device Log", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.12f, 0.17f, 1.0f));
    ImGui::BeginChild("Header", ImVec2(0.0f, 106.0f), true);
    ImGui::SetCursorPos(ImVec2(22.0f, 19.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.76f, 1.0f, 1.0f));
    ImGui::TextUnformatted("WIN DEVICE LOG");
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(23.0f, 57.0f));
    ImGui::TextDisabled("A live history of USB connections and removals");

    const float statsStart = ImGui::GetWindowWidth() - 430.0f;
    ImGui::SetCursorPos(ImVec2(statsStart, 43.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.39f, 0.85f, 0.65f, 1.0f));
    ImGui::TextUnformatted("CONNECTIONS");
    ImGui::SameLine(statsStart + 122.0f);
    ImGui::Text("%zu", connectedCount);
    ImGui::PopStyleColor();

    ImGui::SameLine(statsStart + 190.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.48f, 0.46f, 1.0f));
    ImGui::TextUnformatted("DISCONNECTIONS");
    ImGui::SameLine(statsStart + 333.0f);
    ImGui::Text("%zu", disconnectedCount);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("ACTIVITY");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu events", eventHistory.size());
    ImGui::SameLine(ImGui::GetWindowWidth() - 125.0f);
    ImGui::BeginDisabled(eventHistory.empty());
    if (ImGui::Button("Clear history"))
    {
        eventHistory.clear();
        connectedCount = 0;
        disconnectedCount = 0;
        autoScrollRequested = false;
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    if (eventHistory.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.088f, 0.115f, 1.0f));
        ImGui::BeginChild("EmptyState", ImVec2(0.0f, 0.0f), true);
        const ImVec2 emptySize = ImGui::GetContentRegionAvail();
        const char* emptyTitle = "All quiet for now";
        const char* emptyHint = "Connect or disconnect a USB device to see it here.";
        const ImVec2 titleSize = ImGui::CalcTextSize(emptyTitle);
        const ImVec2 hintSize = ImGui::CalcTextSize(emptyHint);
        ImGui::SetCursorPos(ImVec2(
            (emptySize.x - titleSize.x) * 0.5f,
            (emptySize.y - titleSize.y - hintSize.y - 20.0f) * 0.5f));
        ImGui::TextColored(
            ImVec4(0.72f, 0.81f, 0.91f, 1.0f), "%s", emptyTitle);
        ImGui::SetCursorPosX((emptySize.x - hintSize.x) * 0.5f);
        ImGui::TextDisabled("%s", emptyHint);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(14.0f, 10.0f));
        if (ImGui::BeginTable(
                "DeviceEvents", 3,
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupColumn("TIME", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("STATUS", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("DEVICE", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < eventHistory.size(); ++i)
            {
                const DeviceEvent& event = eventHistory[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(event.timestamp.c_str());

                ImGui::TableSetColumnIndex(1);
                const ImVec4 actionColor = event.action == "Connected"
                    ? ImVec4(0.39f, 0.85f, 0.65f, 1.0f)
                    : ImVec4(1.0f, 0.48f, 0.46f, 1.0f);
                ImGui::TextColored(actionColor, "%s", event.action.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(event.deviceName.c_str());
                if (autoScrollRequested && i + 1 == eventHistory.size())
                {
                    ImGui::SetScrollHereY(1.0f);
                }
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    autoScrollRequested = false;
}
