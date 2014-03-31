/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#ifndef MIR_COMPOSITOR_GL_RENDERER_H_
#define MIR_COMPOSITOR_GL_RENDERER_H_

#include <mir/compositor/renderer.h>
#include <mir/compositor/zoomable.h>
#include <mir/geometry/rectangle.h>
#include <mir/graphics/buffer_id.h>
#include <mir/graphics/cursor.h>
#include <GLES2/gl2.h>
#include <unordered_map>
#include <vector>
#include <memory>

namespace mir
{
namespace compositor
{
class SoftCursor;

class GLRenderer : public Renderer, public Zoomable
{
public:
    GLRenderer(geometry::Rectangle const& display_area);
    virtual ~GLRenderer() noexcept;

    // These are called with a valid GL context:
    void set_viewport(geometry::Rectangle const& rect) override;
    void set_rotation(float degrees) override;
    void begin() const override;
    void render(graphics::Renderable const& renderable,
                graphics::Buffer& buffer) const override;
    void end() const override;

    // This is called _without_ a GL context:
    void suspend() override;

    void zoom(float) override;
    bool screen_transformed() const override;
    std::weak_ptr<graphics::Cursor> cursor() const override;

    struct Vertex
    {
        GLfloat position[3];
        GLfloat texcoord[2];
    };

    struct Primitive
    {
        GLenum type; // GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_TRIANGLES etc
        GLuint tex_id;  // GL texture ID (or 0 to represent the surface itself)
        std::vector<Vertex> vertices;
    };

    /**
     * tessellate defines the list of triangles that will be used to render
     * the surface. By default it just returns 4 vertices for a rectangle.
     * However you can override its behaviour to tessellate more finely and
     * deform freely for effects like wobbly windows.
     *
     * \param [in,out] primitives The list of rendering primitives to be
     *                            grown and/or modified.
     * \param [in]     renderable The renderable surface being tessellated.
     * \param [in]     buf_size   The dimensions of the buffer being rendered,
     *                            which can be particularly useful in
     *                            calculating texcoords for a surface being
     *                            actively resized (as the buf_size doesn't
     *                            yet match renderable.size()).
     *
     * \note The cohesion of this function to GLRenderer is quite loose and it
     *       does not strictly need to reside here.
     *       However it seems a good choice under GLRenderer while this remains
     *       the only OpenGL-specific class in the display server, and
     *       tessellation is very much OpenGL-specific.
     */
    virtual void tessellate(std::vector<Primitive>& primitives,
                            graphics::Renderable const& renderable,
                            geometry::Size const& buf_size) const;

    /**
     * Load the texture for a surface any which way you like. The default
     * implementation does so with efficient GPU-side caching built in.
     *
     * \returns The OpenGL texture name for the surface.
     */
    virtual GLuint load_texture(graphics::Renderable const& renderable,
                                graphics::Buffer& buffer) const;

private:
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
    GLuint position_attr_loc;
    GLuint texcoord_attr_loc;
    GLuint centre_uniform_loc;
    GLuint display_transform_uniform_loc;
    GLuint transform_uniform_loc;
    GLuint alpha_uniform_loc;
    float rotation;

    geometry::Rectangle const screen;
    geometry::Rectangle viewport;
    bool viewport_changed;
    float zoom_mag;
    std::shared_ptr<SoftCursor> soft_cursor;

    typedef graphics::Renderable const* SurfaceID;
    struct Texture
    {
        GLuint id = 0;
        graphics::BufferID origin;
        bool used;
    };
    mutable std::unordered_map<SurfaceID, Texture> textures;
    mutable bool skipped = false;

};

}
}

#endif // MIR_COMPOSITOR_GL_RENDERER_H_
