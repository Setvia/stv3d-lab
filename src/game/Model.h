#ifndef GAME_MODEL_H
#define GAME_MODEL_H

#include "core/math/mat4.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

#include <cstdint>

// Which geometry a model draws: an index into the geometry table owned by the app, which owns the
// render resources. How that geometry is stored (CPU MeshData, GPU buffers, an atlas) is the
// renderer's business, which keeps Camera/Model/Character independent of the render backend and
// testable without a device.
using MeshId = std::uint32_t;

constexpr MeshId kNoMesh = 0xFFFFFFFFu;

// A model instance = a reference to geometry + its own transform (position/rotation/scale) + an
// optional spin. Several models can share one geometry while their transforms stay independent.
//
// Pure math with no device, so it can be unit-tested without a window.
// The model matrix is always composed as T * R * S (see core/math/conventions.h).
class Model
{
public:
    Model() = default;
    explicit Model(MeshId mesh) : mesh(mesh) {}

    // ---------- position ----------
    void setPosition(const vec3& position) { this->position = position; }
    vec3 getPosition() const { return position; }
    void translate(const vec3& delta) { position += delta; }

    // ---------- scale ----------
    void setScale(const vec3& scale) { this->scale = scale; }
    void setUniformScale(float scale) { this->scale = vec3{scale, scale, scale}; }
    vec3 getScale() const { return scale; }

    // ---------- rotation ----------
    void setRotation(const quat& rotation) { this->rotation = rotation; }
    void setRotationDegrees(float degrees, const vec3& axis)
    {
        rotation = quat::fromAxisAngle(axis, degrees * kDegToRad);
    }
    quat getRotation() const { return rotation; }

    // Additional rotation on top of the current one. Post-multiply, so the new rotation acts in the
    // model's own frame (same convention as quat::operator*).
    void rotateBy(float degrees, const vec3& axis)
    {
        rotation = quat::fromAxisAngle(axis, degrees * kDegToRad) * rotation;
    }

    // ---------- spin (driven once per fixed tick by the caller) ----------
    void setSpin(float degreesPerSecond, const vec3& axis = vec3{0.0f, 1.0f, 0.0f})
    {
        spin_degrees_per_second = degreesPerSecond;
        spin_axis = axis;
    }
    float getSpinDegreesPerSecond() const { return spin_degrees_per_second; }
    vec3 getSpinAxis() const { return spin_axis; }

    void updateSpin(float dt)
    {
        if (spin_degrees_per_second != 0.0f && dt > 0.0f)
        {
            rotateBy(spin_degrees_per_second * dt, spin_axis);
        }
    }

    // ---------- matrices ----------
    mat4 modelMatrix() const
    {
        return mat4::fromTRS(position, rotation, scale);
    }

    // ---------- geometry reference ----------
    MeshId getMesh() const { return mesh; }
    bool hasMesh() const { return mesh != kNoMesh; }
    void setMesh(MeshId id) { mesh = id; }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kDegToRad = kPi / 180.0f;

    MeshId mesh = kNoMesh;
    vec3 position{0.0f, 0.0f, 0.0f};
    quat rotation;  // identity = no rotation
    vec3 scale{1.0f, 1.0f, 1.0f};

    float spin_degrees_per_second = 0.0f;
    vec3 spin_axis{0.0f, 1.0f, 0.0f};
};

#endif  // GAME_MODEL_H
