#include "SnapshotBuffer.hpp"
#include <algorithm>
#include <cmath>

namespace blubox::engine::replication {
namespace {
struct Quat { float x, y, z, w; };
Quat fromEuler(const std::array<float, 3>& e) {
  const float cx=std::cos(e[0]*0.5f), sx=std::sin(e[0]*0.5f);
  const float cy=std::cos(e[1]*0.5f), sy=std::sin(e[1]*0.5f);
  const float cz=std::cos(e[2]*0.5f), sz=std::sin(e[2]*0.5f);
  return {sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz,
          cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz};
}
std::array<float, 3> toEuler(Quat q) {
  const float sinr=2.0f*(q.w*q.x+q.y*q.z), cosr=1.0f-2.0f*(q.x*q.x+q.y*q.y);
  const float sinp=2.0f*(q.w*q.y-q.z*q.x);
  const float siny=2.0f*(q.w*q.z+q.x*q.y), cosy=1.0f-2.0f*(q.y*q.y+q.z*q.z);
  return {std::atan2(sinr,cosr),std::asin(std::clamp(sinp,-1.0f,1.0f)),std::atan2(siny,cosy)};
}
Quat slerp(Quat a, Quat b, float t) {
  float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
  if (dot<0.0f) { dot=-dot; b={-b.x,-b.y,-b.z,-b.w}; }
  if (dot>0.9995f) {
    Quat q{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return n>1e-8f?Quat{q.x/n,q.y/n,q.z/n,q.w/n}:a;
  }
  const float angle=std::acos(std::clamp(dot,-1.0f,1.0f)), sine=std::sin(angle);
  if (std::abs(sine)<1e-8f) return a;
  const float wa=std::sin((1.0f-t)*angle)/sine, wb=std::sin(t*angle)/sine;
  return {a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb,a.w*wa+b.w*wb};
}
}

void SnapshotBuffer::push(NetworkId id, TransformSnapshot snapshot) {
  auto& queue = snapshots_[id];
  if (!queue.empty() && snapshot.time <= queue.back().time) return;
  queue.push_back(std::move(snapshot));
  while (queue.size() > 8) queue.pop_front();
}

std::optional<TransformSnapshot> SnapshotBuffer::sample(NetworkId id, double time) const {
  auto found=snapshots_.find(id);
  if(found==snapshots_.end()||found->second.empty())return{};
  const auto& q=found->second;
  if(q.size()==1||time<=q.front().time)return q.front();
  for(std::size_t i=1;i<q.size();++i){
    if(time>q[i].time)continue;
    const auto&a=q[i-1];const auto&b=q[i];
    const double duration=b.time-a.time;
    const float alpha=static_cast<float>(duration<=0.0?0.0:std::clamp((time-a.time)/duration,0.0,1.0));
    TransformSnapshot out=b;
    for(std::size_t axis=0;axis<3;++axis){
      out.transform.position[axis]=a.transform.position[axis]+(b.transform.position[axis]-a.transform.position[axis])*alpha;
      out.transform.scale[axis]=a.transform.scale[axis]+(b.transform.scale[axis]-a.transform.scale[axis])*alpha;
      out.linearVelocity[axis]=a.linearVelocity[axis]+(b.linearVelocity[axis]-a.linearVelocity[axis])*alpha;
    }
    out.transform.rotation=toEuler(slerp(fromEuler(a.transform.rotation),fromEuler(b.transform.rotation),alpha));
    for(std::size_t c=0;c<4;++c)out.color[c]=static_cast<unsigned char>(a.color[c]+(static_cast<float>(b.color[c])-a.color[c])*alpha);
    out.time=time;
    return out;
  }
  if(q.size()>=2){
    const auto&last=q.back();
    const float dt=static_cast<float>(std::clamp(time-last.time,0.0,0.15));
    auto out=last;
    for(std::size_t axis=0;axis<3;++axis)out.transform.position[axis]+=last.linearVelocity[axis]*dt;
    out.time=last.time+dt;
    return out;
  }
  return q.back();
}
}
