/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "real_kms_output.h"
#include "page_flipper.h"
#include "kms-utils/kms_connector.h"
#include "mir/fatal.h"
#include "mir/log.h"
#include <string.h> // strcmp

#include <boost/throw_exception.hpp>
#include <system_error>

namespace mg = mir::graphics;
namespace mgm = mg::mesa;
namespace mgk = mg::kms;
namespace geom = mir::geometry;

class mgm::FBHandle
{
public:
    FBHandle(gbm_bo* bo, uint32_t drm_fb_id)
        : bo{bo}, drm_fb_id{drm_fb_id}
    {
    }

    ~FBHandle()
    {
        if (drm_fb_id)
        {
            int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
            drmModeRmFB(drm_fd, drm_fb_id);
        }
    }

    uint32_t get_drm_fb_id() const
    {
        return drm_fb_id;
    }

private:
    gbm_bo *bo;
    uint32_t drm_fb_id;
};

namespace
{
void bo_user_data_destroy(gbm_bo* /*bo*/, void *data)
{
    auto bufobj = static_cast<mgm::FBHandle*>(data);
    delete bufobj;
}

}

mgm::RealKMSOutput::RealKMSOutput(int drm_fd, uint32_t connector_id,
                                  std::shared_ptr<PageFlipper> const& page_flipper)
    : drm_fd{drm_fd}, connector_id{connector_id}, page_flipper{page_flipper},
      connector(), mode_index{0}, current_crtc(), saved_crtc(),
      using_saved_crtc{true}, has_cursor_{false},
      power_mode(mir_power_mode_on)
{
    reset();

    kms::DRMModeResources resources{drm_fd};

    if (connector->encoder_id)
    {
        auto encoder = resources.encoder(connector->encoder_id);
        if (encoder->crtc_id)
        {
            saved_crtc = *resources.crtc(encoder->crtc_id);
        }
    }
}

mgm::RealKMSOutput::~RealKMSOutput()
{
    restore_saved_crtc();
}

void mgm::RealKMSOutput::reset()
{
    kms::DRMModeResources resources{drm_fd};

    /* Update the connector to ensure we have the latest information */
    try
    {
        connector = resources.connector(connector_id);
    }
    catch (std::exception const& e)
    {
        fatal_error(e.what());
    }

    // TODO: What if we can't locate the DPMS property?
    for (int i = 0; i < connector->count_props; i++)
    {
        auto prop = drmModeGetProperty(drm_fd, connector->props[i]);
        if (prop && (prop->flags & DRM_MODE_PROP_ENUM)) {
            if (!strcmp(prop->name, "DPMS"))
            {
                dpms_enum_id = connector->props[i];
                drmModeFreeProperty(prop);
                break;
            }
            drmModeFreeProperty(prop);
        }
    }

    /* Discard previously current crtc */
    current_crtc = nullptr;
}

geom::Size mgm::RealKMSOutput::size() const
{
    drmModeModeInfo const& mode(connector->modes[mode_index]);
    return {mode.hdisplay, mode.vdisplay};
}

int mgm::RealKMSOutput::max_refresh_rate() const
{
    drmModeModeInfo const& current_mode = connector->modes[mode_index];
    return current_mode.vrefresh;
}

void mgm::RealKMSOutput::configure(geom::Displacement offset, size_t kms_mode_index)
{
    fb_offset = offset;
    mode_index = kms_mode_index;
}

bool mgm::RealKMSOutput::set_crtc(FBHandle const& fb)
{
    if (!ensure_crtc())
    {
        mir::log_error("Output %s has no associated CRTC to set a framebuffer on",
                       mgk::connector_name(connector).c_str());
        return false;
    }

    auto ret = drmModeSetCrtc(drm_fd, current_crtc->crtc_id,
                              fb.get_drm_fb_id(), fb_offset.dx.as_int(), fb_offset.dy.as_int(),
                              &connector->connector_id, 1,
                              &connector->modes[mode_index]);
    if (ret)
    {
        current_crtc = nullptr;
        return false;
    }

    using_saved_crtc = false;
    return true;
}

void mgm::RealKMSOutput::clear_crtc()
{
    try
    {
        ensure_crtc();
    }
    catch (...)
    {
        /*
         * In order to actually clear the output, we need to have a crtc
         * connected to the output/connector so that we can disconnect
         * it. However, not being able to get a crtc is OK, since it means
         * that the output cannot be displaying anything anyway.
         */
        return;
    }

    auto result = drmModeSetCrtc(drm_fd, current_crtc->crtc_id,
                                 0, 0, 0, nullptr, 0, nullptr);
    if (result)
    {
        fatal_error("Couldn't clear output %s (drmModeSetCrtc = %d)",
                   mgk::connector_name(connector).c_str(), result);
    }

    current_crtc = nullptr;
}

bool mgm::RealKMSOutput::schedule_page_flip(FBHandle const& fb)
{
    std::unique_lock<std::mutex> lg(power_mutex);
    if (power_mode != mir_power_mode_on)
        return true;
    if (!current_crtc)
    {
        mir::log_error("Output %s has no associated CRTC to schedule page flips on",
                       mgk::connector_name(connector).c_str());
        return false;
    }
    return page_flipper->schedule_flip(current_crtc->crtc_id, fb.get_drm_fb_id(), connector_id);
}

void mgm::RealKMSOutput::wait_for_page_flip()
{
    std::unique_lock<std::mutex> lg(power_mutex);
    if (power_mode != mir_power_mode_on)
        return;
    if (!current_crtc)
    {
        fatal_error("Output %s has no associated CRTC to wait on",
                   mgk::connector_name(connector).c_str());
    }

    last_frame_.store(page_flipper->wait_for_flip(current_crtc->crtc_id));
}

mg::Frame mgm::RealKMSOutput::last_frame() const
{
    return last_frame_.load();
}

bool mgm::RealKMSOutput::set_cursor(gbm_bo* buffer)
{
    int result = 0;
    if (current_crtc)
    {
        has_cursor_ = true;
        result = drmModeSetCursor(
                drm_fd,
                current_crtc->crtc_id,
                gbm_bo_get_handle(buffer).u32,
                gbm_bo_get_width(buffer),
                gbm_bo_get_height(buffer));
        if (result)
        {
            has_cursor_ = false;
            mir::log_warning("set_cursor: drmModeSetCursor failed (%s)",
                             strerror(-result));
        }
    }
    return !result;
}

void mgm::RealKMSOutput::move_cursor(geometry::Point destination)
{
    if (current_crtc)
    {
        if (auto result = drmModeMoveCursor(drm_fd, current_crtc->crtc_id,
                                            destination.x.as_uint32_t(),
                                            destination.y.as_uint32_t()))
        {
            mir::log_warning("move_cursor: drmModeMoveCursor failed (%s)",
                             strerror(-result));
        }
    }
}

bool mgm::RealKMSOutput::clear_cursor()
{
    int result = 0;
    if (current_crtc)
    {
        result = drmModeSetCursor(drm_fd, current_crtc->crtc_id, 0, 0, 0);

        if (result)
            mir::log_warning("clear_cursor: drmModeSetCursor failed (%s)",
                             strerror(-result));
        has_cursor_ = false;
    }

    return !result;
}

bool mgm::RealKMSOutput::has_cursor() const
{
    return has_cursor_;
}

bool mgm::RealKMSOutput::ensure_crtc()
{
    /* Nothing to do if we already have a crtc */
    if (current_crtc)
        return true;

    /* If the output is not connected there is nothing to do */
    if (connector->connection != DRM_MODE_CONNECTED)
        return false;

    current_crtc = mgk::find_crtc_for_connector(drm_fd, connector);


    return (current_crtc != nullptr);
}

void mgm::RealKMSOutput::restore_saved_crtc()
{
    if (!using_saved_crtc)
    {
        drmModeSetCrtc(drm_fd, saved_crtc.crtc_id, saved_crtc.buffer_id,
                       saved_crtc.x, saved_crtc.y,
                       &connector->connector_id, 1, &saved_crtc.mode);

        using_saved_crtc = true;
    }
}

void mgm::RealKMSOutput::set_power_mode(MirPowerMode mode)
{
    std::lock_guard<std::mutex> lg(power_mutex);

    if (power_mode != mode)
    {
        power_mode = mode;
        drmModeConnectorSetProperty(drm_fd, connector_id,
                                   dpms_enum_id, mode);
    }
}

void mgm::RealKMSOutput::set_gamma(mg::GammaCurves const& gamma)
{
    if (!ensure_crtc())
    {
        mir::log_warning("Output %s has no associated CRTC to set gamma on",
                         mgk::connector_name(connector).c_str());
        return;
    }

    if (gamma.red.size() != gamma.green.size() ||
        gamma.green.size() != gamma.blue.size())
    {
        BOOST_THROW_EXCEPTION(
            std::invalid_argument("set_gamma: mismatch gamma LUT sizes"));
    }

    int ret = drmModeCrtcSetGamma(
        drm_fd,
        current_crtc->crtc_id,
        gamma.red.size(),
        const_cast<uint16_t*>(gamma.red.data()),
        const_cast<uint16_t*>(gamma.green.data()),
        const_cast<uint16_t*>(gamma.blue.data()));

    int err = -ret;
    if (err)
        mir::log_warning("drmModeCrtcSetGamma failed: %s", strerror(err));

    // TODO: return bool in future? Then do what with it?
}

mgm::FBHandle* mgm::RealKMSOutput::fb_for(gbm_bo* bo, uint32_t width, uint32_t height) const
{
    if (!bo)
        return nullptr;

    /*
     * Check if we have already set up this gbm_bo (the gbm implementation is
     * free to reuse gbm_bos). If so, return the associated FBHandle.
     */
    auto bufobj = static_cast<FBHandle*>(gbm_bo_get_user_data(bo));
    if (bufobj)
        return bufobj;

    uint32_t fb_id{0};
    uint32_t handles[4] = {gbm_bo_get_handle(bo).u32, 0, 0, 0};
    uint32_t strides[4] = {gbm_bo_get_stride(bo), 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};

    auto format = gbm_bo_get_format(bo);
    /*
     * Mir might use the old GBM_BO_ enum formats, but KMS and the rest of
     * the world need fourcc formats, so convert...
     */
    if (format == GBM_BO_FORMAT_XRGB8888)
        format = GBM_FORMAT_XRGB8888;
    else if (format == GBM_BO_FORMAT_ARGB8888)
        format = GBM_FORMAT_ARGB8888;

    /* Create a KMS FB object with the gbm_bo attached to it. */
    auto ret = drmModeAddFB2(drm_fd, width, height, format,
                             handles, strides, offsets, &fb_id, 0);
    if (ret)
        return nullptr;

    /* Create a FBHandle and associate it with the gbm_bo */
    bufobj = new FBHandle{bo, fb_id};
    gbm_bo_set_user_data(bo, bufobj, bo_user_data_destroy);

    return bufobj;
}

bool mgm::RealKMSOutput::buffer_requires_migration(gbm_bo* bo) const
{
    /*
     * It's possible that some devices will not require migration -
     * Intel GPUs can obviously scanout from main memory, as can USB outputs such as
     * DisplayLink.
     *
     * For a first go, just say that *every* device scans out of GPU-private memory.
     */
    return gbm_device_get_fd(gbm_bo_get_device(bo)) != drm_fd;
}
