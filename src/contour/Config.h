/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "Actions.h"

#include <terminal/Color.h>
#include <terminal/Process.h>
#include <terminal/Sequencer.h>                 // CursorDisplay
#include <terminal/Size.h>
#include <terminal_view/ShaderConfig.h>
#include <terminal_view/TextRenderer.h>         // FontDef
#include <terminal_view/DecorationRenderer.h>   // Decorator

#include <text_shaper/font.h>

#include <crispy/stdfs.h>

#include <QKeySequence>

#include <chrono>
#include <system_error>
#include <optional>
#include <string>
#include <variant>
#include <unordered_map>

namespace contour::config {

enum class ScrollBarPosition
{
    Hidden,
    Left,
    Right
};

enum class Permission
{
    Deny,
    Allow,
    Ask
};

struct TerminalProfile {
    terminal::Process::ExecInfo shell;
    bool maximized = false;
    bool fullscreen = false;

    terminal::Size terminalSize;

    std::optional<int> maxHistoryLineCount;
    int historyScrollMultiplier;
    bool autoScrollOnUpdate;

    terminal::view::FontDescriptions fonts;

    int tabWidth;

    struct {
        Permission changeFont = Permission::Ask;
    } permissions;

    terminal::ColorProfile colors;

    terminal::CursorShape cursorShape;
    terminal::CursorDisplay cursorDisplay;
    std::chrono::milliseconds cursorBlinkInterval;

    terminal::Opacity backgroundOpacity; // value between 0 (fully transparent) and 0xFF (fully visible).
    bool backgroundBlur; // On Windows 10, this will enable Acrylic Backdrop.

    struct {
        terminal::view::Decorator normal = terminal::view::Decorator::DottedUnderline;
        terminal::view::Decorator hover = terminal::view::Decorator::Underline;
    } hyperlinkDecoration;
};

using terminal::view::ShaderConfig;
using terminal::view::ShaderClass;

// NB: All strings in here must be UTF8-encoded.
struct Config {
    FileSystem::path backingFilePath;

    std::optional<FileSystem::path> logFilePath;

    std::unordered_map<std::string, terminal::ColorProfile> colorschemes;
    std::unordered_map<std::string, TerminalProfile> profiles;
    std::string defaultProfileName;

    TerminalProfile* profile(std::string const& _name)
    {
        if (auto i = profiles.find(_name); i != profiles.end())
            return &i->second;
        return nullptr;
    }

    // selection
    std::string wordDelimiters;

    // input mapping
    std::map<QKeySequence, std::vector<actions::Action>> keyMappings;
    std::unordered_map<terminal::MouseEvent, std::vector<actions::Action>> mouseMappings;

    static std::optional<ShaderConfig> loadShaderConfig(ShaderClass _shaderClass);

    ShaderConfig backgroundShader = terminal::view::defaultShaderConfig(ShaderClass::Background);
    ShaderConfig textShader = terminal::view::defaultShaderConfig(ShaderClass::Text);

    bool sixelScrolling = false;
    bool sixelCursorConformance = true;
    terminal::Size maxImageSize = {2000, 2000};
    int maxImageColorRegisters = 256;

    ScrollBarPosition scrollbarPosition = ScrollBarPosition::Right;
    bool hideScrollbarInAltScreen = true;
};

std::optional<std::string> readConfigFile(std::string const& _filename);

void loadConfigFromFile(Config& _config, FileSystem::path const& _fileName);
Config loadConfigFromFile(FileSystem::path const& _fileName);
Config loadConfig();

std::error_code createDefaultConfig(FileSystem::path const& _path);

} // namespace contour::config

namespace fmt // {{{
{
    template <>
    struct formatter<contour::config::Permission> {
        template <typename ParseContext>
        constexpr auto parse(ParseContext& ctx) { return ctx.begin(); }
        template <typename FormatContext>
        auto format(contour::config::Permission const& _perm, FormatContext& ctx)
        {
            switch (_perm)
            {
                case contour::config::Permission::Allow:
                    return format_to(ctx.out(), "allow");
                case contour::config::Permission::Deny:
                    return format_to(ctx.out(), "deny");
                case contour::config::Permission::Ask:
                    return format_to(ctx.out(), "ask");
            }
            return format_to(ctx.out(), "({})", unsigned(_perm));
        }
    };
} // }}}
