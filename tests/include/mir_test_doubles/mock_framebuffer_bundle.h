/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#ifndef MIR_TEST_DOUBLES_MOCK_FRAMEBUFFER_BUNDLE_H_
#define MIR_TEST_DOUBLES_MOCK_FRAMEBUFFER_BUNDLE_H_

#include "src/platforms/android/framebuffer_bundle.h"
#include "stub_buffer.h"
#include <gmock/gmock.h>

namespace mir
{
namespace test
{
namespace doubles
{

struct MockFBBundle : public graphics::android::FramebufferBundle
{
    MockFBBundle(geometry::Size sz, double vsync_rate, MirPixelFormat pf)
    {
        ON_CALL(*this, last_rendered_buffer())
            .WillByDefault(testing::Return(std::make_shared<StubBuffer>()));
        ON_CALL(*this, fb_format())
            .WillByDefault(testing::Return(pf));
        ON_CALL(*this, fb_size())
            .WillByDefault(testing::Return(sz));
        ON_CALL(*this, fb_refresh_rate())
            .WillByDefault(testing::Return(vsync_rate));
    }

    MockFBBundle() :
        MockFBBundle({0,0}, 0.0f, mir_pixel_format_abgr_8888)
    {
    }

    MOCK_METHOD0(fb_format, MirPixelFormat());
    MOCK_METHOD0(fb_size, geometry::Size());
    MOCK_METHOD0(fb_refresh_rate, double());
    MOCK_METHOD0(buffer_for_render, std::shared_ptr<graphics::Buffer>());
    MOCK_METHOD0(last_rendered_buffer, std::shared_ptr<graphics::Buffer>());
    MOCK_METHOD1(wait_for_consumed_buffer, void(bool));
};
}
}
}

#endif /* MIR_TEST_DOUBLES_MOCK_FRAMEBUFFER_BUNDLE_H_ */
