// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <map>
#include <string>
#include <typeindex>
#include "common/common_types.h"

namespace Settings {

enum class Category : u32 {
    Audio,
    Core,
    Cpu,
    CpuDebug,
    CpuUnsafe,
    Overlay,
    Renderer,
    RendererAdvanced,
    RendererHacks,
    RendererExtensions,
    RendererDebug,
    System,
    SystemAudio,
    DataStorage,
    Debugging,
    DebuggingGraphics,
    GpuDriver,
    Linux,
    Miscellaneous,
    Network,
    WebService,
    AddOns,
    Controls,
    Ui,
    UiAudio,
    UiGeneral,
    UiLayout,
    UiGameList,
    Screenshots,
    Shortcuts,
    Multiplayer,
    Services,
    Paths,
    LibraryApplet,
    MaxEnum,
};

constexpr u8 SpecializationTypeMask = 0xf;
constexpr u8 SpecializationAttributeMask = 0xf0;
constexpr u8 SpecializationAttributeOffset = 4;

// Scalar and countable could have better names
enum Specialization : u8 {
    Default = 0,
    Time = 1,        // Duration or specific moment in time
    Hex = 2,         // Hexadecimal number
    List = 3,        // Setting has specific members
    RuntimeList = 4, // Members of the list are determined during runtime
    Scalar = 5,      // Values are continuous
    Countable = 6,   // Can be stepped through
    Paired = 7,      // Another setting is associated with this setting
    Radio = 8,       // Setting should be presented in a radio group

    Percentage = (1 << SpecializationAttributeOffset), // Should be represented as a percentage
};

class BasicSetting;

class Linkage {
public:
    explicit Linkage(u32 initial_count = 0);
    ~Linkage();
    std::map<Category, std::vector<BasicSetting*>> by_category{};
    std::map<std::string, Settings::BasicSetting*> by_key{};
    std::vector<std::function<void()>> restore_functions{};
    u32 count;
};

class BasicSetting {
protected:
    explicit BasicSetting(Linkage& linkage, const std::string& name, Category category_, bool save_,
                          bool runtime_modifiable_, u32 specialization,
                          BasicSetting* other_setting);

public:
    virtual ~BasicSetting();


    [[nodiscard]] virtual std::string ToString() const = 0;

    [[nodiscard]] virtual std::string ToStringGlobal() const;

    [[nodiscard]] virtual std::string DefaultToString() const = 0;

    [[nodiscard]] virtual std::string MinVal() const = 0;

    [[nodiscard]] virtual std::string MaxVal() const = 0;

    virtual void LoadString(const std::string& load) = 0;

    [[nodiscard]] virtual std::string Canonicalize() const = 0;


    [[nodiscard]] virtual std::string_view TypeId() const = 0;

    [[nodiscard]] virtual constexpr bool IsEnum() const = 0;

    [[nodiscard]] virtual constexpr bool Switchable() const {
        return false;
    }

    [[nodiscard]] bool Save() const;

    [[nodiscard]] bool RuntimeModifiable() const;

    [[nodiscard]] constexpr u32 Id() const {
        return id;
    }

    [[nodiscard]] Category GetCategory() const;

    [[nodiscard]] u32 Specialization() const;

    [[nodiscard]] BasicSetting* PairedSetting() const;

    [[nodiscard]] const std::string& GetLabel() const;

    [[nodiscard]] virtual constexpr bool Ranged() const = 0;

    [[nodiscard]] virtual constexpr u32 EnumIndex() const = 0;

    [[nodiscard]] virtual constexpr bool IsFloatingPoint() const = 0;

    [[nodiscard]] virtual constexpr bool IsIntegral() const = 0;


    virtual void SetGlobal(bool global);

    [[nodiscard]] virtual bool UsingGlobal() const;

private:
    const std::string label; ///< The setting's label
    const Category category; ///< The setting's category AKA INI group
    const u32 id;            ///< Unique integer for the setting
    const bool save; ///< Suggests if the setting should be saved and read to a frontend config
    const bool
        runtime_modifiable;   ///< Suggests if the setting can be modified while a guest is running
    const u32 specialization; ///< Extra data to identify representation of a setting
    BasicSetting* const other_setting; ///< A paired setting
};

} // namespace Settings
