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
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include "mir_test_framework/udev_environment.h"

#include "src/server/graphics/gbm/udev_wrapper.h"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include <umockdev.h>
#include <libudev.h>

namespace mg=mir::graphics;
namespace mgg=mir::graphics::gbm;
namespace mtf=mir::mir_test_framework;

class UdevWrapperTest : public ::testing::Test
{
public:
    mtf::UdevEnvironment udev_environment;
};

TEST_F(UdevWrapperTest, IteratesOverCorrectNumberOfDevices)
{
    udev_environment.add_device("drm", "fakedev1", NULL, {}, {});
    udev_environment.add_device("drm", "fakedev2", NULL, {}, {});
    udev_environment.add_device("drm", "fakedev3", NULL, {}, {});
    udev_environment.add_device("drm", "fakedev4", NULL, {}, {});
    udev_environment.add_device("drm", "fakedev5", NULL, {}, {});

    udev* ctx = udev_new();
    mgg::UdevEnumerator enumerator(ctx);

    enumerator.scan_devices();

    int device_count = 0;
    for (auto& device : enumerator)
    {
        (void)device;
        ++device_count;
    }

    ASSERT_EQ(device_count, 5);

    udev_unref(ctx);
}

TEST_F(UdevWrapperTest, EnumeratorMatchSubsystemIncludesCorrectDevices)
{
    udev_environment.add_device("drm", "fakedrm1", NULL, {}, {});
    udev_environment.add_device("scsi", "fakescsi1", NULL, {}, {});
    udev_environment.add_device("drm", "fakedrm2", NULL, {}, {});
    udev_environment.add_device("usb", "fakeusb1", NULL, {}, {});
    udev_environment.add_device("usb", "fakeusb2", NULL, {}, {});

    udev* ctx = udev_new();
    mgg::UdevEnumerator devices(ctx);

    devices.add_match_subsystem("drm");
    devices.scan_devices();
    for (auto& device : devices)
    {
        ASSERT_STREQ("drm", device.subsystem());
    }

    udev_unref(ctx);
}

TEST_F(UdevWrapperTest, UdevDeviceHasCorrectDevType)
{
    auto sysfs_path = udev_environment.add_device("drm", "card0", NULL, {}, {"DEVTYPE", "drm_minor"});

    udev* ctx = udev_new();

    mgg::UdevDevice dev(ctx, sysfs_path);
    ASSERT_STREQ("drm_minor", dev.devtype());

    udev_unref(ctx);
}

TEST_F(UdevWrapperTest, UdevDeviceHasCorrectDevPath)
{
    auto sysfs_path = udev_environment.add_device("drm", "card0", NULL, {}, {});

    udev* ctx = udev_new();

    mgg::UdevDevice dev(ctx, sysfs_path);
    ASSERT_STREQ("/devices/card0", dev.devpath());

    udev_unref(ctx);
}

TEST_F(UdevWrapperTest, EnumeratorMatchParentMatchesOnlyChildren)
{
    auto card0_syspath = udev_environment.add_device("drm", "card0", NULL, {}, {});
    udev_environment.add_device("usb", "fakeusb", NULL, {}, {});

    udev_environment.add_device("drm", "card0-HDMI1", "/sys/devices/card0", {}, {});
    udev_environment.add_device("drm", "card0-VGA1", "/sys/devices/card0", {}, {});
    udev_environment.add_device("drm", "card0-LVDS1", "/sys/devices/card0", {}, {});

    udev* ctx = udev_new();

    mgg::UdevEnumerator devices(ctx);
    mgg::UdevDevice drm_device(ctx, card0_syspath);

    devices.match_parent(drm_device);
    devices.scan_devices();

    int child_count = 0;
    for (auto& device : devices)
    {
        EXPECT_STREQ("drm", device.subsystem());
        ++child_count;
    }
    EXPECT_EQ(4, child_count);

    udev_unref(ctx);
}

TEST_F(UdevWrapperTest, EnumeratorThrowsLogicErrorIfIteratedBeforeScanned)
{
    udev* ctx = udev_new();

    mgg::UdevEnumerator devices(ctx);

    EXPECT_THROW({ devices.begin(); },
                 std::logic_error);
    udev_unref(ctx);
}

TEST_F(UdevWrapperTest, EnumeratorLogicErrorHasSensibleMessage)
{
    udev* ctx = udev_new();

    mgg::UdevEnumerator devices(ctx);
    std::string error_msg;

    try {
        devices.begin();
    }
    catch (std::logic_error& e)
    {
        error_msg = e.what();
    }
    EXPECT_STREQ("Attempted to iterate over udev devices without first scanning", error_msg.c_str());

    udev_unref(ctx);
}
