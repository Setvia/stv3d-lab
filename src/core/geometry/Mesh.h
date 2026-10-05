#pragma once

#ifndef MESH_H
#define MESH_H

#include <cstddef>
#include <vector>
#include <cstdint>

#include "Vertex.h"

struct Mesh
{
    static constexpr uint8_t kAttribPos = 0;
    static constexpr uint8_t kAttribColor = 1;
    static constexpr uint8_t kBindingInterLeaved = 0;

    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    int32_t idc = 0;

    // Constructor
    constexpr Mesh() noexcept {}
    constexpr Mesh(const Mesh &) = delete;
    constexpr Mesh(Mesh &&other) noexcept
        : vao(other.vao), vbo(other.vbo), ebo(other.ebo), idc(other.idc)
    {
        other.vao = 0;
        other.vbo = 0;
        other.ebo = 0;
        other.idc = 0;
    }

    // Assign
    Mesh &operator=(const Mesh &) = delete;
    Mesh &operator=(Mesh &&other) noexcept
    {
        if (this != &other) {
            destroy();

            vao = other.vao;
            vbo = other.vbo;
            ebo = other.ebo;
            idc = other.idc;

            other.vao = 0;
            other.vbo = 0;
            other.ebo = 0;
            other.idc = 0;
        }
        return *this;
    }
    
    // Create function
    void create(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices)
    {
        if (vertices.empty() || indices.empty()) {
            return;
        }

       destroy();

       
    }

    // Destroy function
    void destroy()
    {

    }

    bool isValid() const
    {
        return vao != 0 && idc > 0;
    }

    void draw()
    {

    }
};


#endif // MESH_H