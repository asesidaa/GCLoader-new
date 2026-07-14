#include <Windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // Necessary for some platforms

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "misc/cpp/imgui_stdlib.h"
#include "NesysNetworkConfig.h"
#include "RegistryConfig.h"

#include <rfl/toml.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <variant>
#include "config.h"


// --- Global State (for simplicity in example) ---
SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Gamepad *g_gamepad = nullptr;
InputConfig g_config;
std::string g_config_path = "config.toml";
bool g_config_dirty = false; // Flag to indicate if changes need saving
bool g_saved = false;
bool g_open_bind_popup_requested = false;

// Rebinding state
enum class RebindType { None, Keyboard, GamepadButton, GamepadAxis };

struct RebindTarget {
    std::string label;
    RebindType type = RebindType::None;
    // Use std::variant to hold a pointer to the actual config member
    std::variant<SDL_Keycode *, SDL_GamepadButton *, SDL_GamepadAxis *> target_ptr;
};

std::optional<RebindTarget> g_rebinding_target;

// --- UI Drawing Functions ---

// Helper to create a row for key binding
void DrawKeybindingRow(const char *label, SDL_Keycode &keycode_ref) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%s", KeycodeToString(keycode_ref).c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(label); // Ensure unique ID for button
    if (ImGui::Button("Bind")) {
        g_rebinding_target = RebindTarget{label, RebindType::Keyboard, &keycode_ref};
        // ImGui::OpenPopup("Bind Input");
        g_open_bind_popup_requested = true;
    }
    ImGui::PopID();
}

// Helper to create a row for gamepad button binding
void DrawGamepadButtonBindingRow(const char *label, SDL_GamepadButton &button_ref) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%s", GamepadButtonToString(button_ref).c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(label);
    if (ImGui::Button("Bind")) {
        g_rebinding_target = RebindTarget{label, RebindType::GamepadButton, &button_ref};
        // ImGui::OpenPopup("Bind Input");
        g_open_bind_popup_requested = true;
    }
    ImGui::PopID();
}

// Helper to create a row for gamepad axis binding
void DrawGamepadAxisBindingRow(const char *label, SDL_GamepadAxis &axis_ref) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%s", GamepadAxisToString(axis_ref).c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::PushID(label);
    if (ImGui::Button("Bind")) {
        g_rebinding_target = RebindTarget{label, RebindType::GamepadAxis, &axis_ref};
        // ImGui::OpenPopup("Bind Input");
        g_open_bind_popup_requested = true;
    }
    ImGui::PopID();
}

void DrawInlineValidationError(bool valid, const char* message) {
    if (!valid) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "%s",
            message);
    }
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    // 1. Initialize SDL
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_VIDEO) != true) {
        SDL_Log("Error initializing SDL: %s", SDL_GetError());
        return -1;
    }

    // 2. Create Window and Renderer
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    g_window = SDL_CreateWindow("Input Configurator", 800, 600, window_flags);
    if (!g_window) {
        SDL_Log("Error creating window: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    g_renderer = SDL_CreateRenderer(g_window, nullptr);
    if (!g_renderer) {
        SDL_Log("Error creating renderer: %s", SDL_GetError());
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return -1;
    }

    // 3. Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows (optional)

    ImGui::StyleColorsDark(); // or ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(g_window, g_renderer);
    ImGui_ImplSDLRenderer3_Init(g_renderer);

    // 4. Load Initial Configuration
    std::ifstream ifs(g_config_path);
    if (ifs.is_open()) {
        std::string toml_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        auto load_result = rfl::toml::read<InputConfig>(toml_content);
        if (load_result) {
            g_config = *load_result;
            SDL_Log("Loaded configuration from %s", g_config_path.c_str());
        } else {
            SDL_Log("Error parsing %s: %s", g_config_path.c_str(), load_result.error().what());
            SDL_DestroyRenderer(g_renderer);
            SDL_DestroyWindow(g_window);
            SDL_Quit();
            return 1;
        }
    } else {
        SDL_Log("Could not open %s for reading.", g_config_path.c_str());
        SDL_DestroyRenderer(g_renderer);
        SDL_DestroyWindow(g_window);
        SDL_Quit();
        return 1;
    }


    // 5. Initialize Gamepad
    SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
    int num_joysticks;
    auto joystick_id = SDL_GetJoysticks(&num_joysticks);
    SDL_Log("Found %d joystick(s).", num_joysticks);
    if (g_config.gamepad_index() >= 0) {
        if (SDL_IsGamepad(g_config.gamepad_index())) {
            g_gamepad = SDL_OpenGamepad(g_config.gamepad_index());
            if (g_gamepad) {
                SDL_Log("Opened gamepad %d: %s", g_config.gamepad_index(), SDL_GetGamepadName(g_gamepad));
            } else {
                SDL_Log("Could not open gamepad %d: %s", g_config.gamepad_index(), SDL_GetError());
            }
        } else {
            SDL_Log("Device %d is not a recognized gamepad.", g_config.gamepad_index());
        }
    } else if (num_joysticks > 0) {
        SDL_Log("Configured gamepad_index %d is out of range. No gamepad opened.", g_config.gamepad_index());
    }
    SDL_free(joystick_id);

    // 6. Main Loop
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event); // Forward events to ImGui

            // Handle our own events *after* ImGui
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(g_window)) {
                done = true;
            }

            // --- Rebinding Logic ---
            if (g_rebinding_target) {
                bool bound = false;
                switch (g_rebinding_target->type) {
                    case RebindType::Keyboard:
                        if (event.type == SDL_EVENT_KEY_DOWN) {
                            // Ignore modifier keys for binding? Maybe add config later.
                            // if (event.key.keysym.sym != SDLK_LCTRL && ...)
                            SDL_Keycode *target = std::get<SDL_Keycode *>(g_rebinding_target->target_ptr);
                            if (target) {
                                *target = event.key.key;
                                bound = true;
                                g_config_dirty = true;
                                SDL_Log("Bound %s to key: %s", g_rebinding_target->label.c_str(),
                                        SDL_GetKeyName(event.key.key));
                            }
                        }
                        break;
                    case RebindType::GamepadButton:
                        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                            // Check if the event is from the correct gamepad (if multiple)
                            // SDL3 uses SDL_JoystickID, need to get it from the opened gamepad

                            SDL_JoystickID target_instance_id = SDL_GetGamepadID(g_gamepad);
                            if (g_gamepad && event.gbutton.which == target_instance_id) {
                                SDL_GamepadButton *target = std::get<SDL_GamepadButton *>(
                                    g_rebinding_target->target_ptr);
                                if (target) {
                                    *target = (SDL_GamepadButton) event.gbutton.button;
                                    bound = true;
                                    g_config_dirty = true;
                                    SDL_Log("Bound %s to button: %s", g_rebinding_target->label.c_str(),
                                            GamepadButtonToString(*target).c_str());
                                }
                            }
                        }
                        break;
                    case RebindType::GamepadAxis:
                        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                            SDL_JoystickID target_instance_id = SDL_GetGamepadID(g_gamepad);
                            if (g_gamepad && event.gaxis.which == target_instance_id) {
                                // Bind only if axis moves significantly from center
                                const Sint16 BIND_THRESHOLD = 10000; // Adjust as needed
                                if (SDL_abs(event.gaxis.value) > BIND_THRESHOLD) {
                                    SDL_GamepadAxis *target = std::get<SDL_GamepadAxis *>(
                                        g_rebinding_target->target_ptr);
                                    if (target) {
                                        *target = (SDL_GamepadAxis) event.gaxis.axis;
                                        bound = true;
                                        g_config_dirty = true;
                                        SDL_Log("Bound %s to axis: %s", g_rebinding_target->label.c_str(),
                                                GamepadAxisToString(*target).c_str());
                                    }
                                }
                            }
                        }
                        break;
                    case RebindType::None: break; // Should not happen
                }

                // Close popup on successful bind or Esc key
                if (bound || (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                    SDL_Log("Bound or esc pressed");
                    ImGui::CloseCurrentPopup();
                    g_rebinding_target.reset();
                }
            } // end if g_rebinding_target

            // --- Gamepad connection handling ---
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                SDL_Log("Gamepad Added: Device %d", event.gdevice.which);
                // num_joysticks = SDL_GetNumJoysticks(); // Update count
                // If no gamepad is open OR the preferred one just connected, try opening it
                if (!g_gamepad || event.gdevice.which == g_config.gamepad_index()) {
                    if (SDL_IsGamepad(event.gdevice.which)) {
                        if (g_gamepad) SDL_CloseGamepad(g_gamepad); // Close previous if any
                        g_gamepad = SDL_OpenGamepad(event.gdevice.which);
                        if (g_gamepad) {
                            SDL_Log("Opened newly added gamepad %d: %s", event.gdevice.which,
                                    SDL_GetGamepadName(g_gamepad));
                            g_config.gamepad_index = event.gdevice.which; // Update config if we auto-opened it
                            g_config_dirty = true;
                        } else {
                            SDL_Log("Could not open newly added gamepad %d: %s", event.gdevice.which, SDL_GetError());
                        }
                    }
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                SDL_Log("Gamepad Removed: Device %d", event.gdevice.which);
                // num_joysticks = SDL_GetNumJoysticks(); // Update count
                if (g_gamepad && event.gdevice.which == SDL_GetGamepadID(g_gamepad)) {
                    SDL_Log("Closing currently used gamepad %d.", event.gdevice.which);
                    SDL_CloseGamepad(g_gamepad);
                    g_gamepad = nullptr;
                    // Maybe try to open gamepad 0 if available? Or leave it null.
                }
            }
        } // End event loop

        // 7. Start ImGui Frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // --- ADDED: Configure main window to fill SDL window ---
        const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(main_viewport->WorkPos);
        ImGui::SetNextWindowSize(main_viewport->WorkSize);
        ImGuiWindowFlags window_flags =
                ImGuiWindowFlags_NoDecoration | // No title bar, borders, etc.
                ImGuiWindowFlags_NoMove | // Cannot be moved
                ImGuiWindowFlags_NoResize | // Cannot be resized
                ImGuiWindowFlags_NoSavedSettings | // Don't save position/size
                ImGuiWindowFlags_NoBringToFrontOnFocus | // Important for fixed windows
                ImGuiWindowFlags_NoNavFocus; // Avoid nav focus taking over the whole window initially (optional)
        // Add ImGuiWindowFlags_MenuBar if you intend to have a menu bar

        // 8. Build ImGui UI
        ImGui::Begin("Input Configuration", nullptr, window_flags); // Main Window

        // --- General Settings ---
        ImGui::SeparatorText("General");

        // Input Mode Selection
        const char *modes[] = {"Keyboard", "Gamepad"};
        int current_mode_idx = static_cast<int>(g_config.input_mode());
        if (ImGui::Combo("Input Mode", &current_mode_idx, modes, IM_ARRAYSIZE(modes))) {
            g_config.input_mode = static_cast<InputMode>(current_mode_idx);
            g_config_dirty = true;
        }

        const char* gameplay_input_styles[] = {"Arcade", "Switch"};
        int current_gameplay_input_style =
            static_cast<int>(g_config.gameplay_input_style());
        if (ImGui::Combo(
                "Gameplay Input Style",
                &current_gameplay_input_style,
                gameplay_input_styles,
                IM_ARRAYSIZE(gameplay_input_styles))) {
            g_config.gameplay_input_style =
                static_cast<GameplayInputStyle>(current_gameplay_input_style);
            g_config_dirty = true;
        }

        constexpr std::array<InputPollHertzConfigValue, 4> input_poll_rates{
            125, 250, 500, 1000};
        constexpr const char* input_poll_rate_labels[]{
            "125 Hz", "250 Hz", "500 Hz", "1000 Hz"};
        auto& input_poll_hz = g_config.input_poll_hz();
        const auto rate_it = std::find(
            input_poll_rates.begin(),
            input_poll_rates.end(),
            input_poll_hz);
        int current_rate = rate_it == input_poll_rates.end()
            ? 3
            : static_cast<int>(
                std::distance(input_poll_rates.begin(), rate_it));
        if (ImGui::Combo(
                "Input Polling Rate",
                &current_rate,
                input_poll_rate_labels,
                IM_ARRAYSIZE(input_poll_rate_labels))) {
            input_poll_hz =
                input_poll_rates[static_cast<std::size_t>(current_rate)];
            g_config_dirty = true;
        }

        // Gamepad Index (Only relevant if gamepads exist)
        if (num_joysticks > 0) {
            // Create a list of available gamepad names + indices
            std::vector<std::string> gamepad_names;
            std::vector<int> gamepad_indices;
            int current_gamepad_selection = -1; // Index in our list, not SDL index

            gamepad_names.push_back("None (-1)"); // Option to select none
            gamepad_indices.push_back(-1);
            if (g_config.gamepad_index() == -1) current_gamepad_selection = 0;

            auto sticks = SDL_GetJoysticks(&num_joysticks);
            for (int i = 0; i < num_joysticks; ++i) {
                if (auto id = sticks[i]; SDL_IsGamepad(id)) {
                    const char *name = SDL_GetJoystickNameForID(id); // Use Joystick name before opening
                    gamepad_names.push_back(
                        std::string(name ? name : "Unknown Gamepad") + " (" + std::to_string(id) + ")");
                    gamepad_indices.push_back(id);
                    if (id == g_config.gamepad_index()) {
                        current_gamepad_selection = gamepad_names.size() - 1;
                    }
                }
            }
            SDL_free(sticks);

            // Convert vector<string> to vector<const char*> for ImGui::Combo
            std::vector<const char *> gamepad_names_cstr;
            for (const auto &name: gamepad_names) {
                gamepad_names_cstr.push_back(name.c_str());
            }


            if (ImGui::Combo("Gamepad Device", &current_gamepad_selection, gamepad_names_cstr.data(),
                             gamepad_names_cstr.size())) {
                int selected_sdl_index = gamepad_indices[current_gamepad_selection];
                if (selected_sdl_index != g_config.gamepad_index()) {
                    g_config.gamepad_index = selected_sdl_index;
                    g_config_dirty = true;
                    // Close current gamepad if open
                    if (g_gamepad) {
                        SDL_CloseGamepad(g_gamepad);
                        g_gamepad = nullptr;
                        SDL_Log("Closed previous gamepad.");
                    }
                    // Open the new one if selected index is valid
                    if (g_config.gamepad_index() >= 0) {
                        if (SDL_IsGamepad(g_config.gamepad_index())) {
                            g_gamepad = SDL_OpenGamepad(g_config.gamepad_index());
                            if (g_gamepad) {
                                SDL_Log("Opened newly selected gamepad %d: %s", g_config.gamepad_index(),
                                        SDL_GetGamepadName(g_gamepad));
                            } else {
                                SDL_Log("Could not open selected gamepad %d: %s", g_config.gamepad_index(),
                                        SDL_GetError());
                            }
                        } else {
                            SDL_Log("Selected device %d is not a gamepad.", g_config.gamepad_index());
                            g_config.gamepad_index = -1; // Reset selection if invalid
                        }
                    } else {
                        SDL_Log("Selected 'None' for gamepad.");
                    }
                }
            }
        } else {
            ImGui::Text("Gamepad Device: No gamepads detected.");
        }


        // Axis Threshold
        int threshold_int = g_config.axis_threshold();
        if (ImGui::SliderInt("Axis Threshold", &threshold_int, 0, 32767, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            g_config.axis_threshold = static_cast<Sint16>(threshold_int);
            g_config_dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Deadzone for analog sticks (0-32767).\nHigher values require more stick movement to register.");
        }

        ImGui::SeparatorText("NESYS");
        auto& nesys_server_ip = g_config.nesys().server_ip();
        if (ImGui::InputText("NESYS Server IPv4", &nesys_server_ip)) {
            g_config_dirty = true;
        }
        const bool nesys_server_ip_valid =
            gc::nesys_service::IsDottedDecimalIpv4(nesys_server_ip);
        if (!nesys_server_ip_valid) {
            ImGui::TextColored(
                ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
                "Enter a dotted-decimal IPv4 address without a port.");
        }

        ImGui::SeparatorText("Registry");
        auto& registry = g_config.registry();

        bool registry_enabled = registry.enabled();
        if (ImGui::Checkbox(
                "Registry configuration overrides",
                &registry_enabled)) {
            registry.enabled = registry_enabled;
            g_config_dirty = true;
        }

        constexpr const char* country_items[] = {
            "GrooveCoasterJpn - GROOVE COASTER, Japanese branding",
            "Rhythmvaders - RHYTHMVADERS, English branding",
            "GrooveCoasterEng - GROOVE COASTER, English branding",
        };
        int country_index = static_cast<int>(registry.game().country());
        if (ImGui::Combo(
                "Game country",
                &country_index,
                country_items,
                IM_ARRAYSIZE(country_items))) {
            registry.game().country = static_cast<GameCountry>(country_index);
            g_config_dirty = true;
        }

        auto& registry_nesys = registry.nesys();
        auto& game_kind = registry_nesys.game_kind();
        if (ImGui::InputScalar(
                "Registry GameKind",
                ImGuiDataType_S64,
                &game_kind)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryDword(game_kind),
            "Enter an integer from 0 through 4294967295.");

        auto& event_next_time = registry_nesys.event_next_time();
        if (ImGui::InputScalar(
                "Registry EventNextTime",
                ImGuiDataType_S64,
                &event_next_time)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryDword(event_next_time),
            "Enter an integer from 0 through 4294967295.");

        auto& condition_time = registry_nesys.condition_time();
        if (ImGui::InputScalar(
                "Registry ConditionTime",
                ImGuiDataType_S64,
                &condition_time)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryDword(condition_time),
            "Enter an integer from 0 through 4294967295.");

        auto& log_level = registry_nesys.log_level();
        if (ImGui::InputScalar(
                "Registry LogLevel",
                ImGuiDataType_S64,
                &log_level)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryLogLevel(log_level),
            "Enter an integer from 0 through 3.");

        auto& news_path = registry_nesys.news_path();
        if (ImGui::InputText("Registry NewsPath", &news_path)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryPath(news_path),
            "Path must contain 1-259 encoded bytes before the terminating NUL.");

        auto& event_path = registry_nesys.event_path();
        if (ImGui::InputText("Registry EventPath", &event_path)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryPath(event_path),
            "Path must contain 1-259 encoded bytes before the terminating NUL.");

        auto& log_path = registry_nesys.log_path();
        if (ImGui::InputText("Registry LogPath", &log_path)) {
            g_config_dirty = true;
        }
        DrawInlineValidationError(
            gc::registry_config::IsRegistryPath(log_path),
            "Path must contain 1-259 encoded bytes before the terminating NUL.");

        const auto registry_validation =
            gc::registry_config::ValidateRegistryConfig(registry);

        ImGui::SeparatorText("Experimental");
        bool enable_120fps_timer_patches = g_config.experimental().enable_120fps_timer_patches();
        if (ImGui::Checkbox("120 FPS timer patches", &enable_120fps_timer_patches)) {
            g_config.experimental().enable_120fps_timer_patches = enable_120fps_timer_patches;
            g_config_dirty = true;
        }
        bool enable_timer_freeze_patches = g_config.experimental().enable_timer_freeze_patches();
        if (ImGui::Checkbox("Timer freeze patches", &enable_timer_freeze_patches)) {
            g_config.experimental().enable_timer_freeze_patches = enable_timer_freeze_patches;
            g_config_dirty = true;
        }
        bool enable_testmode_storage_redirect = g_config.experimental().enable_testmode_storage_redirect();
        if (ImGui::Checkbox("Test-mode storage redirect", &enable_testmode_storage_redirect)) {
            g_config.experimental().enable_testmode_storage_redirect = enable_testmode_storage_redirect;
            g_config_dirty = true;
        }
        bool enable_nesys_service_adapter_patch = g_config.experimental().enable_nesys_service_adapter_patch();
        if (ImGui::Checkbox("NESYS service adapter patch", &enable_nesys_service_adapter_patch)) {
            g_config.experimental().enable_nesys_service_adapter_patch = enable_nesys_service_adapter_patch;
            g_config_dirty = true;
        }
        bool enable_wasapi_exclusive_audio =
            g_config.experimental().enable_wasapi_exclusive_audio();
        if (ImGui::Checkbox(
                "WASAPI exclusive low-latency audio",
                &enable_wasapi_exclusive_audio)) {
            g_config.experimental().enable_wasapi_exclusive_audio =
                enable_wasapi_exclusive_audio;
            g_config_dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Uses the default console endpoint in exclusive 44.1 kHz PCM16 mode.\n"
                "Disable this option if exclusive endpoint initialization fails.");
        }
        auto& wasapi_exclusive_buffer_ms =
            g_config.experimental().wasapi_exclusive_buffer_ms();
        if (ImGui::InputScalar(
                "WASAPI exclusive buffer (ms)",
                ImGuiDataType_U32,
                &wasapi_exclusive_buffer_ms)) {
            g_config_dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", kWasapiExclusiveBufferTooltip);
        }

        // --- Mode Specific Settings ---
        if (ImGui::BeginTable("Bindings", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Current Binding", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Rebind", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            if (g_config.input_mode() == InputMode::Keyboard) {
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Keyboard");
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                DrawKeybindingRow("L Booster Up", g_config.keyboard().p1_up());
                DrawKeybindingRow("L Booster Left", g_config.keyboard().p1_down());
                DrawKeybindingRow("L Booster Down", g_config.keyboard().p2_up());
                DrawKeybindingRow("L Booster Right", g_config.keyboard().p2_down());
                DrawKeybindingRow("L Booster Button", g_config.keyboard().p1_button1());

                DrawKeybindingRow("R Booster Up", g_config.keyboard().p1_left());
                DrawKeybindingRow("R Booster Left", g_config.keyboard().p1_right());
                DrawKeybindingRow("R Booster Down", g_config.keyboard().p2_left());
                DrawKeybindingRow("R Booster Right", g_config.keyboard().p2_right());
                DrawKeybindingRow("R Booster Button", g_config.keyboard().p2_button1());

                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Keyboard - System");
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                DrawKeybindingRow("Test", g_config.keyboard().test());
                DrawKeybindingRow("Service 1", g_config.keyboard().service1());
                DrawKeybindingRow("Service 2", g_config.keyboard().service2());
                DrawKeybindingRow("Service 3", g_config.keyboard().service3());
                DrawKeybindingRow("P1 Start", g_config.keyboard().p1_start());
                DrawKeybindingRow("P2 Start", g_config.keyboard().p2_start());
                DrawKeybindingRow("P2 Service", g_config.keyboard().p2_service());
                DrawKeybindingRow("Card Read", g_config.keyboard().card_read());
            } else {
                // Gamepad Mode
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Gamepad");
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                DrawGamepadButtonBindingRow("L Booster Up", g_config.gamepad().p1_dpad_up());
                DrawGamepadButtonBindingRow("L Booster Left", g_config.gamepad().p1_dpad_down());
                DrawGamepadButtonBindingRow("L Booster Down", g_config.gamepad().p2_button_up());
                DrawGamepadButtonBindingRow("L Booster Right", g_config.gamepad().p2_button_down());
                DrawGamepadButtonBindingRow("R Booster Up", g_config.gamepad().p1_dpad_left());
                DrawGamepadButtonBindingRow("R Booster Left", g_config.gamepad().p1_dpad_right());
                DrawGamepadButtonBindingRow("R Booster Down", g_config.gamepad().p2_button_left());
                DrawGamepadButtonBindingRow("R Booster Right", g_config.gamepad().p2_button_right());
                //DrawGamepadAxisBindingRow("L Booster Axis", g_config.gamepad().p1_axis_horizontal());
                //DrawGamepadAxisBindingRow("P1 Stick Y Axis", g_config.gamepad().p1_axis_vertical());
                DrawGamepadButtonBindingRow("L Booster Button", g_config.gamepad().p1_button1());

                //DrawGamepadAxisBindingRow("R Booster Axis", g_config.gamepad().p2_axis_horizontal());
                // DrawGamepadAxisBindingRow("P2 Stick Y Axis", g_config.gamepad().p2_axis_vertical());
                DrawGamepadButtonBindingRow("R Booster Button", g_config.gamepad().p2_button1());

                // P2 Start & Service are Keyboard only

                // --- Always show these Keyboard binds (even in Gamepad mode) ---
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("System Keys (Keyboard)");
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                DrawKeybindingRow("Test", g_config.keyboard().test());
                DrawKeybindingRow("Service 1", g_config.keyboard().service1());
                DrawKeybindingRow("Service 2", g_config.keyboard().service2());
                DrawKeybindingRow("Service 3", g_config.keyboard().service3());
                DrawKeybindingRow("P1 Start", g_config.keyboard().p1_start());
                DrawKeybindingRow("P2 Start", g_config.keyboard().p2_start());
                DrawKeybindingRow("P2 Service", g_config.keyboard().p2_service());
                DrawKeybindingRow("Card Read", g_config.keyboard().card_read());
            }

            ImGui::EndTable();
        }

        if (g_open_bind_popup_requested) {
            ImGui::OpenPopup("Bind Input");
            g_open_bind_popup_requested = false; // Reset flag
        }

        bool popup_open = true;
        // --- Rebinding Popup ---
        if (ImGui::BeginPopupModal("Bind Input", &popup_open,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            if (g_rebinding_target) {
                ImGui::Text("Press the desired input for:");
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", g_rebinding_target->label.c_str());
                ImGui::Separator();
                switch (g_rebinding_target->type) {
                    case RebindType::Keyboard: ImGui::Text("Waiting for KEY press...");
                        break;
                    case RebindType::GamepadButton: ImGui::Text("Waiting for GAMEPAD BUTTON press...");
                        break;
                    case RebindType::GamepadAxis: ImGui::Text("Waiting for GAMEPAD AXIS movement...");
                        break;
                    case RebindType::None: break; // Should not show popup
                }
                ImGui::Separator();
                ImGui::Text("Press ESC to cancel.");
                // No explicit cancel button needed as ESC key works, handled in event loop.
                // We could add one: if (ImGui::Button("Cancel")) { g_rebinding_target.reset(); ImGui::CloseCurrentPopup(); }
            } else {
                // Should not happen if popup is open, but just in case:
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- Save Button ---
        ImGui::Separator();
        if (g_config_dirty) {
            ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4) ImColor::HSV(0.3f, 0.6f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4) ImColor::HSV(0.3f, 0.7f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4) ImColor::HSV(0.3f, 0.8f, 0.8f));
        }
        const bool configuration_valid =
            nesys_server_ip_valid && registry_validation.valid();
        ImGui::BeginDisabled(!configuration_valid);
        if (ImGui::Button("Save Configuration") && g_config_dirty) {
            try {
                std::string toml_output = rfl::toml::write(g_config);
                std::ofstream ofs(g_config_path);
                if (ofs.is_open()) {
                    ofs << toml_output;
                    ofs.close();
                    SDL_Log(
                        "Configuration saved successfully to %s",
                        g_config_path.c_str());
                    g_config_dirty = false;
                    g_saved = true;
                } else {
                    SDL_Log(
                        "Error: Could not open %s for writing.",
                        g_config_path.c_str());
                }
            } catch (const std::exception& error) {
                SDL_Log(
                    "Error serializing configuration to TOML: %s",
                    error.what());
            }
        }
        ImGui::EndDisabled();
        if (g_config_dirty) {
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Unsaved changes!");
        }
        if (g_saved) {
            ImGui::PopStyleColor(3);
            g_saved = false;
        }


        ImGui::End(); // End Main Window

        // 9. Rendering
        ImGui::Render(); // Generate ImGui draw data

        SDL_SetRenderDrawColor(g_renderer, 114, 144, 154, 255); // Clear color
        SDL_RenderClear(g_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_renderer); // Render ImGui draw data
        SDL_RenderPresent(g_renderer); // Present frame
    }

    // 10. Cleanup
    SDL_Log("Shutting down...");
    if (g_gamepad) {
        SDL_CloseGamepad(g_gamepad);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();

    return 0;
}
