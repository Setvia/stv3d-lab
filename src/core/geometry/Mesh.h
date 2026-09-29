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

    uint32_t m_vao = 0;
    uint32_t m_vbo = 0;
    uint32_t m_ebo = 0;
    int32_t m_idc = 0;

    // Constructor
    constexpr Mesh() noexcept {}
    constexpr Mesh(const Mesh &) = delete;
    constexpr Mesh(Mesh &&other) noexcept
        : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo), m_idc(other.m_idc)
    {
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_idc = 0;
    }

    // Assign
    Mesh &operator=(const Mesh &) = delete;
    Mesh &operator=(Mesh &&other) noexcept
    {
        if (this != &other) {
            destroy();

            m_vao = other.m_vao;
            m_vbo = other.m_vbo;
            m_ebo = other.m_ebo;
            m_idc = other.m_idc;

            other.m_vao = 0;
            other.m_vbo = 0;
            other.m_ebo = 0;
            other.m_idc = 0;
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
        return m_vao != 0 && m_idc > 0;
    }

    void draw()
    {

    }
};


#endif // MESH_H