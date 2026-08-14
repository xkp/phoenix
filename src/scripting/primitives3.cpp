#include "phoenix/scripting/primitives3.hpp"

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

#include <algorithm>
#include <cmath>

namespace phoenix::scripting {
namespace {
using Kernel=CGAL::Exact_predicates_inexact_constructions_kernel;
Kernel::Point_3 point(Point3d p){return {p.x,p.y,p.z};}
double determinant3(const Transform3d& t) noexcept {const auto&m=t.matrix;return
 m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);}
}
Transform3d Transform3d::identity() noexcept{return {};}
Transform3d Transform3d::translation(Vector3d v) noexcept {Transform3d t;t.matrix[3]=v.x;t.matrix[7]=v.y;t.matrix[11]=v.z;return t;}
Transform3d Transform3d::scaling(double x,double y,double z) noexcept {Transform3d t;t.matrix={x,0,0,0,0,y,0,0,0,0,z,0,0,0,0,1};return t;}
Transform3d Transform3d::rotation(Direction3d a,double r) noexcept {const auto unit=direction(Vector3d{a.x,a.y,a.z}).value_or(Direction3d{});const double c=std::cos(r),s=std::sin(r),q=1-c,x=unit.x,y=unit.y,z=unit.z;Transform3d t;t.matrix={c+x*x*q,x*y*q-z*s,x*z*q+y*s,0,y*x*q+z*s,c+y*y*q,y*z*q-x*s,0,z*x*q-y*s,z*y*q+x*s,c+z*z*q,0,0,0,0,1};return t;}
bool finite(Point3d p) noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool finite(Vector3d v) noexcept{return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Vector3d vector(Point3d a,Point3d b) noexcept{return {b.x-a.x,b.y-a.y,b.z-a.z};}
Point3d add(Point3d p,Vector3d v) noexcept{return {p.x+v.x,p.y+v.y,p.z+v.z};}
Point3d subtract(Point3d p,Vector3d v) noexcept{return {p.x-v.x,p.y-v.y,p.z-v.z};}
Vector3d add(Vector3d a,Vector3d b) noexcept{return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3d subtract(Vector3d a,Vector3d b) noexcept{return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vector3d multiply(Vector3d v,double s) noexcept{return {v.x*s,v.y*s,v.z*s};}
Vector3d divide(Vector3d v,double s) noexcept{return {v.x/s,v.y/s,v.z/s};}
double dot(Vector3d a,Vector3d b) noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
Vector3d cross(Vector3d a,Vector3d b) noexcept{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double squared_length(Vector3d v) noexcept{return dot(v,v);}
std::optional<Direction3d> direction(Vector3d v) noexcept {const double n=std::sqrt(squared_length(v));if(n==0||!std::isfinite(n))return {};return Direction3d{v.x/n,v.y/n,v.z/n};}
Direction3d opposite(Direction3d v) noexcept{return {-v.x,-v.y,-v.z};}
Point3d transform(Point3d p,const Transform3d&t) noexcept {const auto&m=t.matrix;const double w=m[12]*p.x+m[13]*p.y+m[14]*p.z+m[15];return {(m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3])/w,(m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7])/w,(m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11])/w};}
Vector3d transform(Vector3d v,const Transform3d&t) noexcept {const auto&m=t.matrix;return {m[0]*v.x+m[1]*v.y+m[2]*v.z,m[4]*v.x+m[5]*v.y+m[6]*v.z,m[8]*v.x+m[9]*v.y+m[10]*v.z};}
Direction3d transform(Direction3d v,const Transform3d&t) noexcept {const auto d=direction(transform(Vector3d{v.x,v.y,v.z},t));return d.value_or(Direction3d{});}
Transform3d compose(const Transform3d&a,const Transform3d&b) noexcept {Transform3d r;r.matrix.fill(0);for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)r.matrix[i*4+j]+=a.matrix[i*4+k]*b.matrix[k*4+j];return r;}
std::optional<Transform3d> inverse(const Transform3d& value) noexcept {double a[4][8]{};for(int i=0;i<4;++i){for(int j=0;j<4;++j)a[i][j]=value.matrix[i*4+j];a[i][i+4]=1;}for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[p][c]))p=r;if(std::abs(a[p][c])<1e-15)return {};for(int j=0;j<8;++j)std::swap(a[c][j],a[p][j]);const double q=a[c][c];for(double&v:a[c])v/=q;for(int r=0;r<4;++r)if(r!=c){const double f=a[r][c];for(int j=0;j<8;++j)a[r][j]-=f*a[c][j];}}Transform3d result;for(int i=0;i<4;++i)for(int j=0;j<4;++j)result.matrix[i*4+j]=a[i][j+4];return result;}
bool is_even(const Transform3d&t) noexcept{return determinant3(t)>0;}
double squared_length(Segment3d s) noexcept{return squared_length(vector(s.source,s.target));}
bool is_degenerate(Segment3d s) noexcept{return s.source.x==s.target.x&&s.source.y==s.target.y&&s.source.z==s.target.z;}
bool has_on(Segment3d s,Point3d p){return Kernel::Segment_3(point(s.source),point(s.target)).has_on(point(p));}
Point3d min(Segment3d s) noexcept{return {std::min(s.source.x,s.target.x),std::min(s.source.y,s.target.y),std::min(s.source.z,s.target.z)};}
Point3d max(Segment3d s) noexcept{return {std::max(s.source.x,s.target.x),std::max(s.source.y,s.target.y),std::max(s.source.z,s.target.z)};}
std::optional<Point3d> vertex(Segment3d s,std::size_t i) noexcept{if(i>1)return {};return i?s.target:s.source;}
Vector3d to_vector(Segment3d s) noexcept{return vector(s.source,s.target);}
std::optional<Direction3d> direction(Segment3d s) noexcept{return direction(to_vector(s));}
Segment3d opposite(Segment3d s) noexcept{return {s.target,s.source};}
Line3d supporting_line(Segment3d s) noexcept{return {s.source,direction(s).value_or(Direction3d{})};}
Segment3d transform(Segment3d s,const Transform3d&t) noexcept{return {transform(s.source,t),transform(s.target,t)};}
Point3d projection(Line3d l,Point3d p) noexcept {const Vector3d d{l.direction.x,l.direction.y,l.direction.z},v=vector(l.point,p);const double n=squared_length(d);return n==0?l.point:add(l.point,multiply(d,dot(v,d)/n));}
Plane3d perpendicular_plane(Line3d l,Point3d p) noexcept{return {l.direction.x,l.direction.y,l.direction.z,-(l.direction.x*p.x+l.direction.y*p.y+l.direction.z*p.z)};}
Vector3d to_vector(Line3d l) noexcept{return {l.direction.x,l.direction.y,l.direction.z};}
bool has_on(Line3d l,Point3d p){return Kernel::Line_3(point(l.point),Kernel::Direction_3(l.direction.x,l.direction.y,l.direction.z)).has_on(point(p));}
Line3d opposite(Line3d l) noexcept{return {l.point,opposite(l.direction)};}
Line3d transform(Line3d l,const Transform3d&t) noexcept{return {transform(l.point,t),transform(l.direction,t)};}
bool has_on(Ray3d r,Point3d p){return Kernel::Ray_3(point(r.source),Kernel::Direction_3(r.direction.x,r.direction.y,r.direction.z)).has_on(point(p));}
Line3d supporting_line(Ray3d r) noexcept{return {r.source,r.direction};}
Ray3d opposite(Ray3d r) noexcept{return {r.source,opposite(r.direction)};}
Ray3d transform(Ray3d r,const Transform3d&t) noexcept{return {transform(r.source,t),transform(r.direction,t)};}
double squared_area(Triangle3d t) noexcept {return squared_length(cross(vector(t.vertices[0],t.vertices[1]),vector(t.vertices[0],t.vertices[2])))/4;}
bool is_degenerate(Triangle3d t){return Kernel::Triangle_3(point(t.vertices[0]),point(t.vertices[1]),point(t.vertices[2])).is_degenerate();}
bool has_on(Triangle3d t,Point3d p){return Kernel::Triangle_3(point(t.vertices[0]),point(t.vertices[1]),point(t.vertices[2])).has_on(point(p));}
std::optional<Point3d> vertex(Triangle3d t,std::size_t i) noexcept{if(i>2)return {};return t.vertices[i];}
Plane3d supporting_plane(Triangle3d t) noexcept{const auto n=cross(vector(t.vertices[0],t.vertices[1]),vector(t.vertices[0],t.vertices[2]));return {n.x,n.y,n.z,-(n.x*t.vertices[0].x+n.y*t.vertices[0].y+n.z*t.vertices[0].z)};}
Triangle3d transform(Triangle3d v,const Transform3d&t) noexcept{return {{transform(v.vertices[0],t),transform(v.vertices[1],t),transform(v.vertices[2],t)}};}
bool is_degenerate(Plane3d p) noexcept{return p.a==0&&p.b==0&&p.c==0;}
bool has_on(Plane3d p,Point3d q){return !is_degenerate(p)&&Kernel::Plane_3(p.a,p.b,p.c,p.d).has_on(point(q));}
int oriented_side(Plane3d p,Point3d q){return is_degenerate(p)?0:static_cast<int>(Kernel::Plane_3(p.a,p.b,p.c,p.d).oriented_side(point(q)));}
Point3d projection(Plane3d p,Point3d q) noexcept {const double n=p.a*p.a+p.b*p.b+p.c*p.c;if(n==0)return q;const double t=(p.a*q.x+p.b*q.y+p.c*q.z+p.d)/n;return {q.x-p.a*t,q.y-p.b*t,q.z-p.c*t};}
Plane3d opposite(Plane3d p) noexcept{return {-p.a,-p.b,-p.c,-p.d};}
Vector3d orthogonal_vector(Plane3d p) noexcept{return {p.a,p.b,p.c};}
std::optional<Direction3d> orthogonal_direction(Plane3d p) noexcept{return direction(orthogonal_vector(p));}
Point3d point(Plane3d p) noexcept {const double aa=std::abs(p.a),bb=std::abs(p.b),cc=std::abs(p.c);if(aa>=bb&&aa>=cc&&p.a!=0)return {-p.d/p.a,0,0};if(bb>=cc&&p.b!=0)return {0,-p.d/p.b,0};if(p.c!=0)return {0,0,-p.d/p.c};return {};}
Vector3d base1(Plane3d p) noexcept {const auto n=orthogonal_vector(p);const auto axis=std::abs(n.x)<=std::abs(n.y)&&std::abs(n.x)<=std::abs(n.z)?Vector3d{1,0,0}:std::abs(n.y)<=std::abs(n.z)?Vector3d{0,1,0}:Vector3d{0,0,1};const auto b=cross(n,axis);const auto d=direction(b);return d?Vector3d{d->x,d->y,d->z}:Vector3d{};}
Vector3d base2(Plane3d p) noexcept {const auto b=cross(orthogonal_vector(p),base1(p));const auto d=direction(b);return d?Vector3d{d->x,d->y,d->z}:Vector3d{};}
Line3d perpendicular_line(Plane3d p,Point3d q) noexcept{return {q,orthogonal_direction(p).value_or(Direction3d{})};}
Plane3d transform(Plane3d p,const Transform3d&t) noexcept {const auto inv=inverse(t);if(!inv)return p;const auto&m=inv->matrix;const std::array<double,4> q{p.a,p.b,p.c,p.d};std::array<double,4> r{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)r[i]+=m[j*4+i]*q[j];return {r[0],r[1],r[2],r[3]};}
} // namespace phoenix::scripting
