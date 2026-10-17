// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core::Frontend {

class GraphicsContext {
public:
    virtual ~GraphicsContext() = default;

    /// Inform the driver to swap the front/back buffers and present the current image
    virtual void SwapBuffers() {}

    /// Makes the graphics context current for the caller thread
    virtual void MakeCurrent() {}

    /// Releases (dunno if this is the "right" word) the context from the caller thread
    virtual void DoneCurrent() {}

    class Scoped {
    public:
        [[nodiscard]] explicit Scoped(GraphicsContext& context_) : context(context_) {
            context.MakeCurrent();
        }
        ~Scoped() {
            if (active) {
                context.DoneCurrent();
            }
        }

        /// In the event that context was destroyed before the Scoped is destroyed, this provides a
        /// mechanism to prevent calling a destroyed object's method during the deconstructor
        void Cancel() {
            active = false;
        }

    private:
        GraphicsContext& context;
        bool active{true};
    };

    /// Calls MakeCurrent on the context and calls DoneCurrent when the scope for the returned value
    /// ends
    [[nodiscard]] Scoped Acquire() {
        return Scoped{*this};
    }
};

} // namespace Core::Frontend
