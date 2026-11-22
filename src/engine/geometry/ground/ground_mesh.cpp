#include "ground_mesh.h"

#include <algorithm>
#include <cmath>

GroundMesh::GroundMesh(std::vector<glm::vec3> triangles, const glm::vec3& up)
    : triangles_(std::move(triangles)), up_(glm::normalize(up)) {
  triangles_.resize(triangles_.size() / 3 * 3);
  u_ = glm::normalize(glm::cross(
      up_, std::abs(up_.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
  v_ = glm::cross(up_, u_);
  const size_t count = triangles_.size() / 3;
  if (count == 0)
    return;

  glm::vec2 high(-1e30f);
  low_ = glm::vec2(1e30f);
  for (const glm::vec3& p : triangles_) {
    const glm::vec2 q(glm::dot(p, u_), glm::dot(p, v_));
    low_ = glm::min(low_, q);
    high = glm::max(high, q);
  }
  // About two triangles a cell
  const glm::vec2 size = glm::max(high - low_, glm::vec2(1e-3f));
  cell_ = std::max(std::sqrt(size.x * size.y / static_cast<float>(count)) *
                       1.5f,
                   1e-3f);
  columns_ = static_cast<int>(size.x / cell_) + 1;
  rows_ = static_cast<int>(size.y / cell_) + 1;

  // Counted, then filled (each triangle in every cell its box covers)
  starts_.assign(static_cast<size_t>(columns_) * rows_ + 1, 0);
  const auto each_cell = [&](size_t t, auto visit) {
    glm::ivec2 a(columns_, rows_), b(-1);
    for (int k = 0; k < 3; ++k) {
      const glm::ivec2 c = Cell(triangles_[t * 3 + k]);
      a = glm::min(a, c);
      b = glm::max(b, c);
    }
    for (int y = a.y; y <= b.y; ++y)
      for (int x = a.x; x <= b.x; ++x)
        visit(static_cast<size_t>(y) * columns_ + x);
  };
  for (size_t t = 0; t < count; ++t)
    each_cell(t, [&](size_t c) { ++starts_[c + 1]; });
  for (size_t c = 1; c < starts_.size(); ++c)
    starts_[c] += starts_[c - 1];
  items_.resize(starts_.back());
  std::vector<uint32_t> fill(starts_.begin(), starts_.end() - 1);
  for (size_t t = 0; t < count; ++t)
    each_cell(t, [&](size_t c) { items_[fill[c]++] = static_cast<uint32_t>(t); });
}

glm::ivec2 GroundMesh::Cell(const glm::vec3& point) const {
  const glm::vec2 q =
      (glm::vec2(glm::dot(point, u_), glm::dot(point, v_)) - low_) / cell_;
  return glm::clamp(glm::ivec2(glm::floor(q)), glm::ivec2(0),
                    glm::ivec2(columns_ - 1, rows_ - 1));
}

bool GroundMesh::Under(const glm::vec3& point, glm::vec3& hit) const {
  if (columns_ == 0)
    return false;
  const glm::vec2 q =
      (glm::vec2(glm::dot(point, u_), glm::dot(point, v_)) - low_) / cell_;
  if (q.x < 0.0f || q.y < 0.0f || q.x >= columns_ || q.y >= rows_)
    return false;
  const size_t cell = static_cast<size_t>(Cell(point).y) * columns_ +
                      static_cast<size_t>(Cell(point).x);
  bool found = false;
  float nearest = 1e30f;
  for (uint32_t i = starts_[cell]; i < starts_[cell + 1]; ++i) {
    // The line meets the triangle's plane at `t`; inside when the barycentric
    // weights are (Moller-Trumbore, either way along the line)
    const glm::vec3* tri = &triangles_[static_cast<size_t>(items_[i]) * 3];
    const glm::vec3 e1 = tri[1] - tri[0], e2 = tri[2] - tri[0];
    const glm::vec3 p = glm::cross(up_, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f)
      continue;
    const glm::vec3 s = point - tri[0];
    const float a = glm::dot(s, p) / det;
    const glm::vec3 r = glm::cross(s, e1);
    const float b = glm::dot(up_, r) / det;
    if (a < 0.0f || b < 0.0f || a + b > 1.0f)
      continue;
    const float t = glm::dot(e2, r) / det;
    if (std::abs(t) < nearest) {
      nearest = std::abs(t);
      hit = point + up_ * t;
      found = true;
    }
  }
  return found;
}
