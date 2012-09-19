/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by:
 *   Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include <mir/graphics/platform.h>
#include <mir/graphics/android/android_buffer_allocator.h>
#include <mir/graphics/android/android_display.h>
#include <mir/graphics/android/android_framebuffer_window.h>

#include <ui/FramebufferNativeWindow.h>

#include <stdexcept>

namespace mg=mir::graphics;
namespace mga=mir::graphics::android;
namespace mc=mir::compositor;

std::shared_ptr<mg::Platform> mg::create_platform()
{
    return std::make_shared<mg::Platform>();
}

std::shared_ptr<mc::GraphicBufferAllocator> mg::create_buffer_allocator(
        const std::shared_ptr<Platform>& /*platform*/)
{
    return std::make_shared<mga::AndroidBufferAllocator>();
}

/* note: gralloc seems to choke when this is opened/closed more than once per process. must investigate drivers further */
std::shared_ptr<mg::Display> mg::create_display(
        const std::shared_ptr<Platform>& /*platform*/)
{
    auto android_window = std::shared_ptr<ANativeWindow>((ANativeWindow*) android_createDisplaySurface());
    if (!android_window.get())
        throw std::runtime_error("could not open FB window");
    auto window = std::make_shared<mga::AndroidFramebufferWindow> (android_window);

    return std::make_shared<mga::AndroidDisplay>(window);
}
