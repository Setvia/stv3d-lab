#ifndef GAME_MODEL_H
#define GAME_MODEL_H

#include <memory>

#include "core/math/mat4.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

class MyMesh;  // forward declaration: this class only holds a reference to geometry

// A model instance = a reference to a mesh + its own transform (position/rotation/scale)
// + an optional spin. Several models can share one mesh (the GPU buffers exist once),
// while their transforms stay fully independent.
//
// Pure math with no Qt and no OpenGL, so it can be unit-tested without a window.
// The model matrix is always composed as T * R * S (see core/math/conventions.h).
class MyModel
{
public:
    MyModel() = default;
    explicit MyModel(std::shared_ptr<MyMesh> mesh) : m_mesh(std::move(mesh)) {}

    // ---------- position ----------
    void setPosition(const vec3& position) { m_position = position; }
    vec3 position() const { return m_position; }
    void translate(const vec3& delta) { m_position += delta; }

    // ---------- scale ----------
    void setScale(const vec3& scale) { m_scale = scale; }
    void setUniformScale(float scale) { m_scale = vec3{scale, scale, scale}; }
    vec3 scale() const { return m_scale; }

    // ---------- rotation ----------
    void setRotation(const quat& rotation) { m_rotation = rotation; }
    void setRotationDegrees(float degrees, const vec3& axis)
    {
        m_rotation = quat::fromAxisAngle(axis, degrees * kDegToRad);
    }
    quat rotation() const { return m_rotation; }

    // Additional rotation on top of the current one. Post-multiply, so the new rotation
    // acts in the model's own frame (same convention as quat::operator*).
    void rotateBy(float degrees, const vec3& axis)
    {
        m_rotation = quat::fromAxisAngle(axis, degrees * kDegToRad) * m_rotation;
    }

    // ---------- spin (driven once per fixed tick by the caller) ----------
    void setSpin(float degreesPerSecond, const vec3& axis = vec3{0.0f, 1.0f, 0.0f})
    {
        m_spin_degrees_per_second = degreesPerSecond;
        m_spin_axis = axis;
    }
    float spinDegreesPerSecond() const { return m_spin_degrees_per_second; }
    vec3 spinAxis() const { return m_spin_axis; }

    void updateSpin(float dt)
    {
        if (m_spin_degrees_per_second != 0.0f && dt > 0.0f)
        {
            rotateBy(m_spin_degrees_per_second * dt, m_spin_axis);
        }
    }

    // ---------- matrices ----------
    mat4 modelMatrix() const
    {
        return mat4::fromTRS(m_position, m_rotation, m_scale);
    }

    // ---------- geometry ----------
    // The method is const but hands out a mutable pointer, following smart-pointer
    // conventions: drawing a mesh is not a logical modification of the pose.
    MyMesh* mesh() const { return m_mesh.get(); }
    std::shared_ptr<MyMesh> meshPtr() const { return m_mesh; }
    bool hasMesh() const { return m_mesh != nullptr; }
    void setMesh(std::shared_ptr<MyMesh> mesh) { m_mesh = std::move(mesh); }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kDegToRad = kPi / 180.0f;

    std::shared_ptr<MyMesh> m_mesh;
    vec3 m_position{0.0f, 0.0f, 0.0f};
    quat m_rotation;  // identity = no rotation
    vec3 m_scale{1.0f, 1.0f, 1.0f};

    float m_spin_degrees_per_second = 0.0f;
    vec3 m_spin_axis{0.0f, 1.0f, 0.0f};
};

#endif  // GAME_MODEL_H
