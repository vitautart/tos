#pragma once

#include <cmath>
#include <limits>
#include <engi/gomath.hpp>
#include <optional>
#include <numbers>

namespace go
{
class Camera3D
{
public:
    //-----------------------------------------------------
    // Camera class that can be moved in editor-like style.
    //-----------------------------------------------------
    Camera3D(
            const go::vf3& eye, 
            const go::vf3& target, 
            const go::vf3& up, 
            const go::vf2& size, 
            bool is_perspective, 
            float fov_vertical = std::numbers::pi_v<float> * 0.5f) : 
        m_eye(eye), 
        m_target(target), 
        m_up(up),
        m_size(size),
        m_fov_vert(fov_vertical), 
        m_perspective(is_perspective){};

    auto get_view() const noexcept -> go::mf4 
    {
        return go::look_at_rh(m_eye, m_target, m_up);
    }

    // TODO: near and far plane hardcoded now. Maybe it would be interesting to implement "inverse z".
    // TODO: currently we linked ortho projection to target plane size, 
    // this allows to be consistent and not repeat code,
    // but if in a future we would change target plane then it can cause problem,
    // in that case we need to check if it is orhto and change another parameter to achiveour goal.
    auto get_proj() const noexcept -> go::mf4 
    {
        if (m_perspective)
            //return go::persp_proj_rh<float>(m_fov_vert, m_size[0], m_size[1], 0.01f, 100.0f);
            return go::persp_proj_rh_reverse_z_infinite<float>(m_fov_vert, m_size[0], m_size[1], 0.01f);
        else
        {
            auto size = get_target_plane_size();
            return go::ortho_proj_rh(size[0], size[1], 0.01f, 100.0f);
        }
    }

    auto pan(go::vf2 cursor_pos) -> void
    {
        if (!m_prev_pan_pos)
        {
            m_prev_pan_pos = cursor_pos;
            return;
        }

        auto mousePos = map_screen_to_target_plane(cursor_pos);
        auto prevPos = map_screen_to_target_plane(m_prev_pan_pos.value());

        auto delta = mousePos - prevPos;
        if (go::length_sq(delta) < 0.00001f)
        {
            return;
        }
        auto worldDir = delta[1] * m_up + delta[0] * get_right_dir();

        m_target = m_target - worldDir;
        m_eye = m_eye - worldDir;

        m_prev_pan_pos = cursor_pos;
    }

    auto reset_pan() -> void
    {
        m_prev_pan_pos = std::nullopt;
    }

    auto reset_arcball() -> void
    {
        m_prev_arcball_pos = std::nullopt;
    }
    // TODO: currently it is simple implemetation, need to be added tracked zoom
    auto zoom(float factor) -> void
    {
        auto d = m_target - m_eye;
        if (go::length_sq(d) > 0.00000001f)
            m_eye = m_eye + d * factor;
    }

    auto arcball(go::vf2 cursor_pos) noexcept -> void
    {
        if (!m_prev_arcball_pos)
        {
            m_prev_arcball_pos = cursor_pos;
            return;
        }

        go::vf3 current = map_screen_to_arcball(cursor_pos);
        go::vf3 prev = map_screen_to_arcball(m_prev_arcball_pos.value());
        go::vf3 axis = go::cross(prev, current);
        if (go::length_sq(axis) < 4 * std::numeric_limits<float>::epsilon())
        {
            m_prev_arcball_pos = cursor_pos;
            return;
        }

        float angle = std::acos(go::dot(prev, current));

        axis = go::norm(axis);
        auto view = get_view();
        view[3] = {0, 0, 0, 1};

        axis = (go::transpose(view) * go::grow(axis, 0.0f)).shrink<3>();

        auto rotation = go::transpose(go::rot3(axis, angle));
        //m_eye = (rotation * go::grow(m_eye, 1.0f)).shrink<3>();
        //m_up = (rotation * go::grow(m_up, 0.0f)).shrink<3>();
        m_eye = rotation * m_eye;
        m_up = rotation * m_up;

        m_prev_arcball_pos = cursor_pos;
    }

    auto set_size(float width, float height) noexcept -> void
    {
        m_size = {width, height};
    }

    auto set_perspective(bool is_perspective) noexcept -> void
    {
        m_perspective = is_perspective;
    }
    
private:

    auto map_screen_to_target_plane(go::vf2 cursor_pos) const noexcept -> go::vf2
    {
        auto targetSize = get_target_plane_size();
        return  go::linmap(cursor_pos, {0, 0}, m_size, 
                {- targetSize[0] / 2, targetSize[1] / 2}, 
                {targetSize[0] / 2, - targetSize[1] / 2});
    }

    auto map_screen_to_arcball(go::vf2 cursor_pos) const noexcept -> go::vf3
    {
        auto ar = get_aspect_ratio();
        float halfWidth = ar > 1.0f ? ar : 1.0f;
        float halfHeight = ar > 1.0f ? 1.0f : ar;

        go::vf2 v = go::linmap(cursor_pos, {0,0}, m_size, {-halfWidth, halfHeight}, {halfWidth, -halfHeight});
        float len = go::length(v);
        if (len >= 1.0f) 
            return {v[0] / len, v[1] / len, 0};
        else
            return { v[0], v[1], std::sqrt(std::abs(1.0f - (v[0] * v[0] + v[1] * v[1]))) };
    }

    // x local camera axis
    auto get_right_dir() const noexcept -> go::vf3
    {
        return go::norm(go::cross(m_up, -get_forward_dir()));
    }

    // y local camera axis
    auto get_up_dir() const noexcept -> go::vf3
    {
        return m_up;
    }

    // -z local camera axis
    auto get_forward_dir() const noexcept -> go::vf3
    {
        return go::norm(m_target - m_eye);
    }

    auto get_target_plane_size() const noexcept -> go::vf2
    {
        float d = go::length(m_eye - m_target);
        return {2 * d * std::tan(get_fov_horizontal() * 0.5f), 2 * d * std::tan(m_fov_vert * 0.5f)};
    }

    auto get_fov_horizontal() const noexcept -> float
    {
        return 2 * std::atan(get_aspect_ratio() * tan(m_fov_vert * 0.5f));
    }

    auto get_aspect_ratio() const noexcept -> float
    {
        return m_size[0] / m_size[1];
    }

public:
    go::vf3 m_eye;
    go::vf3 m_target;
    go::vf3 m_up;
    go::vf2 m_size;
    bool m_perspective;
    float m_fov_vert; // in radians

private:
    std::optional<go::vf2> m_prev_arcball_pos = std::nullopt;
    std::optional<go::vf2> m_prev_pan_pos = std::nullopt;
};
}
