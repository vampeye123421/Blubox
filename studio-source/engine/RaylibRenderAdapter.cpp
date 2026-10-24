#include "RaylibRenderAdapter.hpp"
#include <raylib.h>
#include <rlgl.h>
namespace blubox::engine {
void RaylibRenderAdapter::draw(const InstanceRegistry& registry) const {
  for (auto h : registry.renderables()) {
    const auto* r = registry.get(h);
    if (!r || !r->visible) continue;
    rlPushMatrix();
    rlMultMatrixf(r->world.matrix.data());
    Color c{r->color[0], r->color[1], r->color[2], r->color[3]};
    if (r->classId == ClassId::Sphere)
      DrawSphere({0, 0, 0}, 0.5f, c);
    else if (r->classId == ClassId::Part || r->classId == ClassId::WedgePart || r->classId == ClassId::CornerWedgePart)
      DrawCubeV({0, 0, 0}, {r->size.x, r->size.y, r->size.z}, c);
    rlPopMatrix();
  }
}
}  // namespace blubox::engine
