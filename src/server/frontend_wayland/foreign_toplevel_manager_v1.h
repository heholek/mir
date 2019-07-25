/*
 * Copyright © 2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: William Wold <william.wold@canonical.com>
 */

#ifndef MIR_FRONTEND_FOREIGN_TOPLEVEL_MANAGER_V1_H
#define MIR_FRONTEND_FOREIGN_TOPLEVEL_MANAGER_V1_H

#include "wlr-foreign-toplevel-management-unstable-v1_wrapper.h"
#include "mir_toolkit/common.h"

namespace mir
{
namespace scene
{
class Surface;
}
namespace frontend
{

class Shell;
class WlSeat;
class OutputManager;

/// Informs a client about toplevels from itself and other clients
/// The Wayland objects it creates for each toplevel can be used to aquire information and control that toplevel
/// Useful for task bars and app switchers
class ForeignToplevelManagerV1Global
    : public wayland::ForeignToplevelManagerV1::Global
{
public:
    ForeignToplevelManagerV1Global(
        wl_display* display,
        std::shared_ptr<Shell> shell,
        WlSeat& seat,
        OutputManager* output_manager);

    std::shared_ptr<Shell> const shell;
    WlSeat& seat;
    OutputManager* const output_manager;

private:
    void bind(wl_resource* new_resource) override;
};

/// An instance of the ForeignToplevelManagerV1 global, bound to a specific client
class ForeignToplevelManagerV1
    : public wayland::ForeignToplevelManagerV1
{
public:
    class ObserverOwner;

    ForeignToplevelManagerV1(wl_resource* new_resource, ForeignToplevelManagerV1Global& global);
    ~ForeignToplevelManagerV1();

    auto observer_owner() const -> std::shared_ptr<ObserverOwner>;

private:
    /// Wayland requests
    ///@{
    void stop() override;
    ///@}

    /// Allows weak pointers that are cleared when the Wayland object is destroyed
    /// Pointed to optional needs to be explicitly set to nullopt in the destructor
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const weak_self;

    /// The observer this owns detects when surfaces are added and removed and creates a ForeignToplevelHandleV1 for each
    std::shared_ptr<ObserverOwner> const observer;
};

/// Used by a client to aquire information about or control a specific toplevel
/// Instances of this class are created and managed by ForeignToplevelManagerV1::ObserverOwner::Observer
class ForeignToplevelHandleV1
    : public wayland::ForeignToplevelHandleV1
{
public:
    class ObserverOwner;

    /// Sends the required .state event
    void send_state(MirWindowFocusState focused, MirWindowState state);

    /// Sends the .closed event and makes this surface innert
    void has_closed();

private:
    ForeignToplevelHandleV1(
        ForeignToplevelManagerV1 const& manager,
        std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> const weak_self);
    ~ForeignToplevelHandleV1();

    /// Wayland requests
    ///@{
    void set_maximized();
    void unset_maximized();
    void set_minimized();
    void unset_minimized();
    void activate(struct wl_resource* seat);
    void close();
    void set_rectangle(struct wl_resource* surface, int32_t x, int32_t y, int32_t width, int32_t height);
    void destroy();
    void set_fullscreen(std::experimental::optional<struct wl_resource*> const& output);
    void unset_fullscreen();
    ///@}

    /// Allows weak pointers that are cleared when the Wayland object is destroyed
    /// Pointed to optional needs to be explicitly set to nullopt in the destructor
    std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> const weak_self;

    /// After the manager observer is destroyed, there is no way to know when surfaces are removed,
    /// so all surfaces observers are cleared at that point. For this reason, we need to keep the
    /// ForeignToplevelManagerV1::ObserverOwner around even after the ForeignToplevelManagerV1 has been destroyed
    std::shared_ptr<ForeignToplevelManagerV1::ObserverOwner> const manager_observer_owner;
};
}
}

#endif // MIR_FRONTEND_LAYER_SHELL_V1_H
