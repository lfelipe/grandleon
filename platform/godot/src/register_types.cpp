// SPDX-License-Identifier: MIT
#include <grandleon/godot/grandleon_session.hpp>

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize_grandleon_module(
    godot::ModuleInitializationLevel level
) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(grandleon::godot_frontend::GrandleonSession);
}

void uninitialize_grandleon_module(
    godot::ModuleInitializationLevel level
) {
    static_cast<void>(level);
}

}  // namespace

extern "C" GDExtensionBool GDE_EXPORT grandleon_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    GDExtensionClassLibraryPtr library,
    GDExtensionInitialization* initialization
) {
    godot::GDExtensionBinding::InitObject init(
        get_proc_address, library, initialization
    );
    init.register_initializer(initialize_grandleon_module);
    init.register_terminator(uninitialize_grandleon_module);
    init.set_minimum_library_initialization_level(
        godot::MODULE_INITIALIZATION_LEVEL_SCENE
    );
    return init.init();
}
