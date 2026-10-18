#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <happly.h>
#include <mesh.h>

// Loads triangle faces with their original winding and coordinates. Imported
// normals, colors, and other properties are ignored; empty normals select flat
// shading. Degenerate faces are skipped. Polygonal faces and point clouds must
// be converted before loading.
template <std::floating_point T>
[[nodiscard]] Mesh<T> load_ply(const std::filesystem::path &path,
                               std::unique_ptr<Material<T>> surface_material) {
  const auto fail = [&path](const std::string &message) -> void {
    throw std::runtime_error("PLY '" + path.string() + "': " + message);
  };

  if (!surface_material) {
    fail("a non-null material is required");
  }

  // happly handles ASCII/binary parsing and the standard vertex/face
  // properties.
  auto data = [&path]() {
    try {
      return happly::PLYData(path.string());
    } catch (const std::exception &error) {
      throw std::runtime_error("PLY '" + path.string() +
                               "': could not read file: " + error.what());
    }
  }();

  auto source_positions = [&]() {
    try {
      return data.getVertexPositions();
    } catch (const std::exception &error) {
      fail(std::string("could not read vertex positions: ") + error.what());
      throw;
    }
  }();

  auto source_faces = [&]() {
    try {
      return data.template getFaceIndices<std::size_t>();
    } catch (const std::exception &error) {
      fail(std::string("could not read face indices: ") + error.what());
      throw;
    }
  }();

  if (source_positions.empty() || source_faces.empty()) {
    fail("at least one vertex and one triangle face are required");
  }

  // Match the index/count limits enforced when Scene combines mesh buffers.
  constexpr auto max_index = std::numeric_limits<std::uint32_t>::max();
  if (source_positions.size() >= max_index ||
      source_faces.size() >= max_index) {
    fail("vertex or face count exceeds the renderer's 32-bit limits");
  }

  std::vector<point3<T>> positions;
  positions.reserve(source_positions.size());

  const long double max_coordinate = std::numeric_limits<T>::max();
  for (std::size_t i = 0; i < source_positions.size(); ++i) {
    const auto &source = source_positions[i];
    point3<T> position;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const long double coordinate = source[axis];
      if (!std::isfinite(coordinate) || std::abs(coordinate) > max_coordinate) {
        fail("vertex " + std::to_string(i) +
             " contains a non-finite or out-of-range coordinate");
      }
      position[axis] = static_cast<T>(source[axis]);
    }
    positions.push_back(position);
  }

  std::vector<TriangleIndices> triangles;
  triangles.reserve(source_faces.size());

  for (std::size_t i = 0; i < source_faces.size(); ++i) {
    const auto &face = source_faces[i];
    if (face.size() != 3) {
      fail("face " + std::to_string(i) +
           " is not a triangle; triangulate the model before loading");
    }
    for (const auto index : face) {
      if (index >= positions.size()) {
        fail("face " + std::to_string(i) +
             " references a vertex outside the vertex buffer");
      }
    }

    // Validate using the converted coordinates and the same normal calculation
    // as Scene: conversion can collapse vertices or overflow this calculation.
    const auto normal = cross(positions[face[1]] - positions[face[0]],
                              positions[face[2]] - positions[face[0]]);
    const T normal_squared = normal.squared_norm();
    if (!std::isfinite(normal_squared)) {
      fail("face " + std::to_string(i) +
           " has a normal outside the numeric range");
    }
    // Reconstructed meshes can contain zero-area faces. They have no surface
    // to render and cannot provide the unit normal required by Scene::hit().
    if (normal_squared == T{0}) {
      continue;
    }

    triangles.push_back({static_cast<std::uint32_t>(face[0]),
                         static_cast<std::uint32_t>(face[1]),
                         static_cast<std::uint32_t>(face[2])});
  }

  if (triangles.empty()) {
    fail("no non-degenerate triangle faces remain after loading");
  }

  return Mesh<T>{std::move(positions),
                 {},
                 std::move(triangles),
                 std::move(surface_material)};
}
